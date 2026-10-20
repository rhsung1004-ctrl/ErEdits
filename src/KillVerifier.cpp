#include "KillVerifier.h"

#include "HudKillWatcher.h"
#include "ResultScreen.h"
#include "Util.h"

#include <algorithm>

namespace {
constexpr int kW = HudKillWatcher::kW * rank::kHudScale; // 960
constexpr int kH = HudKillWatcher::kH * rank::kHudScale; // 192
} // namespace

KillVerifier::KillVerifier(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] { m_buf += m_proc.readAllStandardOutput(); });
	connect(&m_proc, &QProcess::finished, this, &KillVerifier::onFinished);
}

KillVerifier::~KillVerifier()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

void KillVerifier::start(const QString &path, const QRectF &roi, const QVector<KillEvent> &events)
{
	cancel();
	m_path = path;
	m_roi = roi;
	m_events = events;
	std::sort(m_events.begin(), m_events.end(), [](const KillEvent &a, const KillEvent &b) { return a.time < b.time; });
	m_index = 0;
	m_slot = 0;
	for (Read &r : m_reads)
		r = Read{};
	m_cancelled = false;
	m_running = true;
	next();
}

void KillVerifier::cancel()
{
	if (!m_running)
		return;
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
	m_running = false;
}

double KillVerifier::slotTime(int i, int slot) const
{
	const double t = m_events[i].time;
	const double prev = i > 0 ? m_events[i - 1].time : -1e9;
	const double next = i + 1 < m_events.size() ? m_events[i + 1].time : 1e18;
	// 앞: 숫자가 바뀌기 직전 (앞 킬의 새 숫자가 자리 잡은 뒤)
	auto before = [&](double back) { return std::max({0.0, t - back, std::min(prev + 0.4, t - 0.15)}); };
	// 뒤: TK 와 K·A 는 3초까지 차이 나게 바뀌므로 넉넉히 뒤, 다만 다음 킬 전까지
	auto after = [&](double fwd) { return std::max(t + 1.2, std::min(t + fwd, next - 0.3)); };
	switch (slot) {
	case 0:
		return before(0.6);
	case 1:
		return after(3.5);
	case 2:
		return before(1.8);
	default:
		return after(5.0);
	}
}

void KillVerifier::next()
{
	if (m_cancelled)
		return;
	if (m_index >= m_events.size()) {
		m_running = false;
		emit progress(int(m_events.size()), int(m_events.size()));
		emit finished(m_events);
		return;
	}
	grab(slotTime(m_index, m_slot));
}

void KillVerifier::grab(double t)
{
	const QString vf = QString("crop=w='iw*%1':h='ih*%2':x='iw*%3':y='ih*%4',scale=%5:%6:flags=bicubic")
				   .arg(num(m_roi.width()), num(m_roi.height()), num(m_roi.x()), num(m_roi.y()))
				   .arg(kW)
				   .arg(kH);
	m_buf.clear();
	setQuietProcess(m_proc, false);
	m_proc.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-ss", num(t), "-i", m_path,
				    "-frames:v", "1", "-an", "-sn", "-dn", "-vf", vf, "-f", "rawvideo", "-pix_fmt",
				    "rgb24", "-"});
}

void KillVerifier::onFinished(int, QProcess::ExitStatus)
{
	if (m_cancelled)
		return;
	m_buf += m_proc.readAllStandardOutput();
	Read r;
	if (m_buf.size() >= qsizetype(kW) * kH * 3)
		r.ok = rank::readHudCounts(reinterpret_cast<const uint8_t *>(m_buf.constData()), kW, kH, r.v, &r.weak);
	m_buf.clear();
	m_reads[m_slot] = r;

	// 다음에 읽을 장면 정하기
	int nextSlot = -1;
	const Read &b = m_reads[0], &a = m_reads[1];
	if (m_slot == 0) {
		nextSlot = 1;
	} else if (m_slot == 1) {
		const bool same = b.ok && a.ok && b.v[1] == a.v[1] && b.v[2] == a.v[2];
		if (!b.ok || same)
			nextSlot = 2; // 앞을 다시 (못 읽었거나, 그대로라서 확인)
		else if (!a.ok)
			nextSlot = 3;
	} else if (m_slot == 2) {
		const bool same = b.ok && a.ok && b.v[1] == a.v[1] && b.v[2] == a.v[2];
		if (!a.ok || same)
			nextSlot = 3;
	}
	if (nextSlot >= 0) {
		m_slot = nextSlot;
		next();
		return;
	}
	decide();
	for (Read &x : m_reads)
		x = Read{};
	m_slot = 0;
	++m_index;
	emit progress(m_index, int(m_events.size()));
	next();
}

void KillVerifier::decide()
{
	KillEvent &e = m_events[m_index];
	const Read &b = m_reads[0].ok ? m_reads[0] : m_reads[2];
	const Read &a = m_reads[1].ok ? m_reads[1] : m_reads[3];
	if (!b.ok || !a.ok) {
		e.check = KillEvent::Unreadable;
		return;
	}
	const int dk = a.v[1] - b.v[1], da = a.v[2] - b.v[2];
	if (dk < 0 || da < 0 || dk > 5 || da > 5) {
		e.check = KillEvent::Unreadable; // 판이 바뀌었거나 잘못 읽음 → 원래 판단 유지
		return;
	}
	if (dk == 0 && da == 0) {
		// 두 번씩 읽어 모두 같고 확실할 때만 "잘못 잡힌 킬"
		const bool sure = m_reads[0].same(m_reads[2]) && m_reads[1].same(m_reads[3]) && !m_reads[0].weak &&
				  !m_reads[1].weak && !m_reads[2].weak && !m_reads[3].weak;
		e.check = sure ? KillEvent::Rejected : KillEvent::Unreadable;
		return;
	}
	e.check = KillEvent::Confirmed;
	e.kill = dk > 0;
	e.kills = std::max(1, dk);
	e.assist = da > 0;
	e.assists = std::max(1, da);
}
