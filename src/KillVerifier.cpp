#include "KillVerifier.h"

#include "FrameGrabber.h"
#include "HudKillWatcher.h"
#include "ResultScreen.h"
#include "Util.h"

#include <algorithm>

namespace {
constexpr int kW = HudKillWatcher::kW * rank::kHudScale; // 960
constexpr int kH = HudKillWatcher::kH * rank::kHudScale; // 192
} // namespace

KillVerifier::KillVerifier(QObject *parent) : QObject(parent), m_grab(new FrameGrabber(this))
{
	connect(m_grab, &FrameGrabber::frame, this, [this](int k, const QByteArray &rgb) {
		if (!m_running || k < 0 || k >= m_asked.size())
			return;
		const auto [i, slot] = m_asked[k];
		Read r;
		if (rgb.size() >= qsizetype(kW) * kH * 3)
			r.ok = rank::readHudCounts(reinterpret_cast<const uint8_t *>(rgb.constData()), kW, kH, r.v, &r.weak);
		m_reads[i][size_t(slot)] = r;
		emit progress(++m_done, m_total);
	});
	connect(m_grab, &FrameGrabber::finished, this, [this] {
		if (!m_running)
			return;
		if (m_round == 1) {
			runRound(2);
			return;
		}
		for (int i = 0; i < m_events.size(); ++i)
			decide(i);
		m_running = false;
		emit finished(m_events);
	});
}

KillVerifier::~KillVerifier() = default;

void KillVerifier::start(const QString &path, const QRectF &roi, const QVector<KillEvent> &events)
{
	cancel();
	m_path = path;
	m_roi = roi;
	m_events = events;
	std::sort(m_events.begin(), m_events.end(), [](const KillEvent &a, const KillEvent &b) { return a.time < b.time; });
	m_reads = QVector<std::array<Read, 4>>(m_events.size());
	m_done = 0;
	m_total = int(m_events.size()) * 2;
	m_running = true;
	runRound(1);
}

void KillVerifier::cancel()
{
	if (!m_running)
		return;
	m_running = false;
	m_grab->cancel();
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

void KillVerifier::runRound(int round)
{
	m_round = round;
	m_asked.clear();
	QVector<double> times;
	for (int i = 0; i < m_events.size(); ++i) {
		QVector<int> want;
		if (round == 1) {
			want = {0, 1};
		} else {
			const Read &b = m_reads[i][0], &a = m_reads[i][1];
			const bool sameKA = b.ok && a.ok && b.v[1] == a.v[1] && b.v[2] == a.v[2];
			if (!b.ok || sameKA)
				want.push_back(2); // 앞을 다시 (못 읽었거나, 그대로라서 확인)
			if (!a.ok || sameKA)
				want.push_back(3);
		}
		for (int s : want) {
			m_asked.push_back({i, s});
			times.push_back(slotTime(i, s));
		}
	}
	if (round == 2)
		m_total = m_done + int(times.size());
	const QString vf = QString("crop=w='iw*%1':h='ih*%2':x='iw*%3':y='ih*%4',scale=%5:%6:flags=bicubic")
				   .arg(num(m_roi.width()), num(m_roi.height()), num(m_roi.x()), num(m_roi.y()))
				   .arg(kW)
				   .arg(kH);
	m_grab->start(m_path, times, vf, kW, kH); // 비어 있어도 finished 가 옴
}

void KillVerifier::decide(int index)
{
	KillEvent &e = m_events[index];
	const auto &rd = m_reads[index];
	const Read &b = rd[0].ok ? rd[0] : rd[2];
	const Read &a = rd[1].ok ? rd[1] : rd[3];
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
		const bool sure = rd[0].same(rd[2]) && rd[1].same(rd[3]) && !rd[0].weak && !rd[1].weak && !rd[2].weak &&
				  !rd[3].weak;
		e.check = sure ? KillEvent::Rejected : KillEvent::Unreadable;
		return;
	}
	e.check = KillEvent::Confirmed;
	e.kill = dk > 0;
	e.kills = std::max(1, dk);
	e.assist = da > 0;
	e.assists = std::max(1, da);
}
