#include "KillVerifier.h"

#include "FrameGrabber.h"
#include "HudKillWatcher.h"
#include "ResultScreen.h"
#include "Util.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTextStream>

#include <algorithm>

namespace {
constexpr int kW = HudKillWatcher::kW * rank::kHudScale; // 960
constexpr int kH = HudKillWatcher::kH * rank::kHudScale; // 192
constexpr double kMinSpan = 4.0; // "그대로"라고 하려면 앞 장면들이 이만큼 넓게 걸쳐 있어야 (앞 킬에 막힌 경우 제외)

bool sameKA(const int *a, const int *b) { return a[1] == b[1] && a[2] == b[2]; }
} // namespace

KillVerifier::KillVerifier(QObject *parent) : QObject(parent), m_grab(new FrameGrabber(this))
{
	connect(m_grab, &FrameGrabber::frame, this, [this](int k, const QByteArray &rgb) {
		if (!m_running || k < 0 || k >= m_asked.size())
			return;
		const auto [i, si] = m_asked[k];
		Shot &s = m_shots[i][si];
		if (rgb.size() >= qsizetype(kW) * kH * 3) {
			const auto *px = reinterpret_cast<const uint8_t *>(rgb.constData());
			s.read.ok = rank::readHudCounts(px, kW, kH, s.read.v, &s.read.weak);
			// 기록용 작은 그림 (반 크기 JPG)
			const QImage img = QImage(px, kW, kH, kW * 3, QImage::Format_RGB888).scaled(kW / 2, kH / 2, Qt::IgnoreAspectRatio,
												      Qt::SmoothTransformation);
			QBuffer b(&s.jpg);
			b.open(QIODevice::WriteOnly);
			img.save(&b, "JPG", 80);
		}
		emit progress(++m_done, m_total);
	});
	connect(m_grab, &FrameGrabber::finished, this, [this] {
		if (!m_running)
			return;
		if (m_round == 1) {
			runRound(2);
			return;
		}
		m_record.clear();
		for (int i = 0; i < m_events.size(); ++i) {
			decide(i);
			Record r;
			r.original = m_original[i];
			r.result = m_events[i];
			r.reason = m_reason[i];
			r.shots = m_shots[i];
			std::sort(r.shots.begin(), r.shots.end(), [](const Shot &a, const Shot &b) { return a.time < b.time; });
			m_record.push_back(r);
		}
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
	// 다시 확인할 때는 이전 확인 결과를 지우고 처음 찾은 그대로에서 시작
	for (KillEvent &e : m_events) {
		e.check = KillEvent::Unchecked;
		if (e.origTime >= 0)
			e.time = e.origTime;
		e.origTime = e.time;
	}
	m_original = m_events;
	m_shots = QVector<QVector<Shot>>(m_events.size());
	m_reason = QVector<QString>(m_events.size());
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

QVector<double> KillVerifier::beforeTimes(int i, int round) const
{
	const double t = m_events[i].time;
	// 앞 킬의 새 숫자가 자리 잡은 뒤보다 앞으로는 가지 않음
	const double low = i > 0 ? m_events[i - 1].time + 0.4 : 0.0;
	QVector<double> want = round == 1 ? QVector<double>{0.6} : QVector<double>{2.5, 5.0, 9.0};
	QVector<double> out;
	for (double back : want) {
		const double x = std::max({0.0, low, t - back});
		if (x >= t - 0.1)
			continue;
		bool dup = false;
		for (const Shot &s : m_shots[i])
			dup = dup || (s.before && std::abs(s.time - x) < 0.3);
		for (double o : out)
			dup = dup || std::abs(o - x) < 0.3;
		if (!dup)
			out.push_back(x);
	}
	return out;
}

QVector<double> KillVerifier::afterTimes(int i, int round) const
{
	const double t = m_events[i].time;
	const double next = i + 1 < m_events.size() ? m_events[i + 1].time : 1e18;
	// TK 와 K·A 는 3초까지 차이 나게 바뀌므로 넉넉히 뒤, 다만 다음 킬 전까지
	const double fwd = round == 1 ? 3.5 : 6.0;
	const double x = std::max(t + 1.2, std::min(t + fwd, next - 0.3));
	for (const Shot &s : m_shots[i])
		if (!s.before && std::abs(s.time - x) < 0.3)
			return {};
	return {x};
}

void KillVerifier::runRound(int round)
{
	m_round = round;
	m_asked.clear();
	QVector<double> times;
	for (int i = 0; i < m_events.size(); ++i) {
		if (round == 2) {
			// 1차에서 바로 확인된 킬은 더 볼 필요 없음
			const auto &sh = m_shots[i];
			const Read *b = nullptr, *a = nullptr;
			for (const Shot &s : sh)
				(s.before ? b : a) = &s.read;
			if (b && a && b->ok && a->ok && !sameKA(b->v, a->v) && a->v[1] >= b->v[1] && a->v[2] >= b->v[2])
				continue;
		}
		for (double x : beforeTimes(i, round)) {
			m_shots[i].push_back({x, true, {}, {}});
			m_asked.push_back({i, int(m_shots[i].size()) - 1});
			times.push_back(x);
		}
		for (double x : afterTimes(i, round)) {
			m_shots[i].push_back({x, false, {}, {}});
			m_asked.push_back({i, int(m_shots[i].size()) - 1});
			times.push_back(x);
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
	QVector<Shot> sh = m_shots[index];
	std::sort(sh.begin(), sh.end(), [](const Shot &a, const Shot &b) { return a.time < b.time; });
	QVector<const Shot *> before, after;
	for (const Shot &s : sh)
		if (s.read.ok)
			(s.before ? before : after).push_back(&s);
	QString &why = m_reason[index];
	if (before.isEmpty() || after.isEmpty()) {
		e.check = KillEvent::Unreadable;
		why = "숫자를 못 읽음 → 그대로 둠";
		return;
	}
	// 뒤: 가장 늦은 장면 (자리 잡은 값). 두 장이 다르면 늦은 쪽
	const Shot *a = after.back();
	// 앞: 뒤와 값이 다른 장면 중 가장 늦은 것 = 이 킬 바로 전 값
	const Shot *diff = nullptr, *sameAfter = nullptr;
	for (const Shot *b : before) {
		if (b->read.v[1] > a->read.v[1] || b->read.v[2] > a->read.v[2]) {
			e.check = KillEvent::Unreadable; // 판이 바뀌었거나 잘못 읽음
			why = QString("앞(%1) 숫자가 뒤보다 큼 → 그대로 둠").arg(formatTime(b->time));
			return;
		}
		if (!sameKA(b->read.v, a->read.v))
			diff = b;
	}
	if (diff) {
		for (const Shot *b : before)
			if (b->time > diff->time && sameKA(b->read.v, a->read.v)) {
				sameAfter = b;
				break;
			}
		const int dk = a->read.v[1] - diff->read.v[1], da = a->read.v[2] - diff->read.v[2];
		if (dk > 5 || da > 5) {
			e.check = KillEvent::Unreadable;
			why = "한 번에 너무 많이 바뀜 → 그대로 둠";
			return;
		}
		e.check = KillEvent::Confirmed;
		e.kill = dk > 0;
		e.kills = std::max(1, dk);
		e.assist = da > 0;
		e.assists = std::max(1, da);
		// 늦게 잡힌 킬: 실제로 바뀐 곳(바뀌기 전 장면과 바뀐 뒤 장면 사이)으로 시각을 당김
		if (sameAfter && sameAfter->time < e.time - 0.3) {
			e.time = (diff->time + sameAfter->time) / 2;
			why = QString("K %1→%2, A %3→%4 · 킬 시각을 %5로 당김")
				      .arg(diff->read.v[1])
				      .arg(a->read.v[1])
				      .arg(diff->read.v[2])
				      .arg(a->read.v[2])
				      .arg(formatTime(e.time));
		} else {
			why = QString("K %1→%2, A %3→%4").arg(diff->read.v[1]).arg(a->read.v[1]).arg(diff->read.v[2]).arg(a->read.v[2]);
		}
		return;
	}
	// 앞뒤 모두 같음: 충분히 앞까지 봤고 모두 확실할 때만 "그대로"
	const double low = index > 0 ? m_events[index - 1].time + 0.4 : 0.0;
	const double span = e.time - before.front()->time;
	const bool reachedLow = before.front()->time <= std::max(0.0, low) + 0.05;
	bool allSure = !a->read.weak;
	for (const Shot *b : before)
		allSure = allSure && !b->read.weak;
	if (before.size() < 2 || !allSure || (span < kMinSpan && !reachedLow)) {
		e.check = KillEvent::Unreadable;
		why = "그대로처럼 보이지만 확실하지 않음 → 그대로 둠";
		return;
	}
	e.check = m_drop ? KillEvent::Rejected : KillEvent::Doubtful;
	why = QString("K %1, A %2 가 %3초 전부터 그대로%4")
		      .arg(a->read.v[1])
		      .arg(a->read.v[2])
		      .arg(span, 0, 'f', 1)
		      .arg(m_drop ? " → 뺌" : " → 확인 필요 (빼지 않음)");
}

QString KillVerifier::saveRecord(const QString &dir) const
{
	QDir().mkpath(dir);
	QDir d(dir);
	for (const QString &f : d.entryList({"*.jpg"}, QDir::Files))
		d.remove(f);
	QFile f(d.filePath("index.html"));
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return {};
	QTextStream h(&f);
	h.setEncoding(QStringConverter::Utf8);
	static const char *names[] = {"미확인", "확인됨", "잘못 잡힘 (뺌)", "못 읽음", "확인 필요"};
	h << "<!doctype html><meta charset=utf-8><title>킬 숫자 확인 기록</title>"
	     "<style>body{font-family:sans-serif;background:#1b1b1f;color:#ddd;margin:16px}"
	     "table{border-collapse:collapse}td{border-bottom:1px solid #333;padding:6px;vertical-align:top}"
	     ".s1{color:#7c7}.s2{color:#f77}.s3{color:#aaa}.s4{color:#fb5}img{display:block;height:48px}"
	     ".shot{display:inline-block;margin-right:8px;font-size:12px}</style>"
	     "<h2>킬 숫자 확인 기록</h2><p>"
	  << m_path.toHtmlEscaped()
	  << "</p><p>킬마다 앞·뒤 장면에서 읽은 K·A 값. 빨간 '잘못 잡힘'이 실제 킬이면 이 파일을 보내 주세요.</p><table>";
	int n = 0;
	for (int i = 0; i < m_record.size(); ++i) {
		const Record &r = m_record[i];
		h << QString("<tr><td>%1</td><td>%2%3</td><td class=s%4><b>%5</b><br>%6</td><td>")
			     .arg(i + 1)
			     .arg(formatTime(r.original.time))
			     .arg(std::abs(r.result.time - r.original.time) > 0.05 ? " → " + formatTime(r.result.time) : QString())
			     .arg(r.result.check)
			     .arg(names[std::clamp(r.result.check, 0, 4)])
			     .arg(r.reason.toHtmlEscaped());
		for (const Shot &s : r.shots) {
			const QString name = QString("%1.jpg").arg(++n, 4, 10, QChar('0'));
			QFile img(d.filePath(name));
			if (!s.jpg.isEmpty() && img.open(QIODevice::WriteOnly))
				img.write(s.jpg);
			const QString val = s.read.ok ? QString("TK %1 K %2 A %3%4")
								.arg(s.read.v[0])
								.arg(s.read.v[1])
								.arg(s.read.v[2])
								.arg(s.read.weak ? " ?" : "")
						      : QString("못 읽음");
			h << QString("<div class=shot>%1 %2<img src='%3'>%4</div>")
				     .arg(s.before ? "앞" : "뒤", formatTime(s.time), name, val);
		}
		h << "</td></tr>";
	}
	h << "</table>";
	return d.filePath("index.html");
}
