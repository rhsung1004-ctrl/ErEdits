#include "Highlights.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

// ───────────────────────── 채팅 종류 ─────────────────────────

bool isLaughChat(const QString &t)
{
	const int k = int(t.count(QChar(0x314B))); // ㅋ
	const int h = int(t.count(QChar(0x314E))); // ㅎ
	if (k >= 2 || h >= 3)
		return true;
	static const char *words[] = {"웃겨", "웃기", "개웃", "빵터", "터졌", "lol", "LOL"};
	for (const char *w : words)
		if (t.contains(QString::fromUtf8(w)))
			return true;
	return false;
}

bool isHypeChat(const QString &t)
{
	static const char *words[] = {"ㄷㄷ", "미쳤", "미친", "개잘", "지렸", "지린", "나이스", "나이쓰", "캬", "ㅗㅜㅑ",
				      "오오", "레전드", "대박", "클라스", "폼 ", "실화", "ㅅㅂ", "굿", "갓",
				      "nice", "GG", "gg", "!!"};
	for (const char *w : words)
		if (t.contains(QString::fromUtf8(w)))
			return true;
	// "와", "와아", "와!!" 처럼 짧은 감탄만 (와서·와이파이 제외)
	return t.size() <= 6 && t.startsWith(QString::fromUtf8("와"));
}

// ───────────────────────── 신호 ─────────────────────────

float Signals::at(const QVector<float> &v, double t) const
{
	const int i = int(std::floor(t));
	return i >= 0 && i < v.size() ? v[i] : 0.f;
}

float Signals::maxIn(const QVector<float> &v, double a, double b) const
{
	float m = 0;
	for (int i = std::max(0, int(std::floor(a))); i <= int(std::floor(b)) && i < v.size(); ++i)
		m = std::max(m, v[i]);
	return m;
}

namespace {

constexpr int kSum = 5;        // 채팅은 5초 합
constexpr int kBaseHalf = 300; // 평소 수준 = 앞뒤 5분 (방송 분위기가 바뀌어도 따라감)
constexpr int kBaseStep = 10;

double median(std::vector<float> &v)
{
	if (v.empty())
		return 0;
	const size_t m = v.size() / 2;
	std::nth_element(v.begin(), v.begin() + long(m), v.end());
	return v[m];
}

// 평소보다 얼마나 튀는지: (값 - 앞뒤 5분 중앙값) / 흩어짐
//  흩어짐 하한 floorFn(중앙값) — 채팅이 적은 방송에서 1~2개 차이로 튀지 않게
template <class F> QVector<float> robustZ(const QVector<float> &x, F floorFn)
{
	const int n = int(x.size());
	QVector<float> z(n, 0.f);
	if (n == 0)
		return z;
	const int steps = (n + kBaseStep - 1) / kBaseStep;
	std::vector<float> med(static_cast<size_t>(steps)), spr(static_cast<size_t>(steps));
	std::vector<float> win;
	for (int s = 0; s < steps; ++s) {
		const int c = s * kBaseStep;
		const int a = std::max(0, c - kBaseHalf), b = std::min(n, c + kBaseHalf);
		win.assign(x.begin() + a, x.begin() + b);
		const double m = median(win);
		for (float &v : win)
			v = float(std::abs(v - m));
		const double mad = median(win) * 1.4826;
		med[size_t(s)] = float(m);
		spr[size_t(s)] = float(std::max(mad, floorFn(m)));
	}
	for (int i = 0; i < n; ++i) {
		const double f = double(i) / kBaseStep;
		const int s0 = std::min(steps - 1, int(f)), s1 = std::min(steps - 1, s0 + 1);
		const double w = f - s0;
		const double m = med[size_t(s0)] * (1 - w) + med[size_t(s1)] * w;
		const double sp = spr[size_t(s0)] * (1 - w) + spr[size_t(s1)] * w;
		z[i] = float((x[i] - m) / sp);
	}
	return z;
}

QVector<float> windowSum(const QVector<float> &perSec)
{
	QVector<float> out(perSec.size(), 0.f);
	double acc = 0;
	for (int i = int(perSec.size()) - 1; i >= 0; --i) {
		acc += perSec[i];
		if (i + kSum < perSec.size())
			acc -= perSec[i + kSum];
		out[i] = float(acc);
	}
	return out;
}

} // namespace

Signals computeSignals(const QVector<ChatMessage> &chat, const QVector<float> &screenLines,
		       const QVector<float> &loudDb, double duration, const SignalParams &p)
{
	Signals s;
	s.seconds = std::max(1, int(std::ceil(duration)));
	const int n = s.seconds;
	QVector<float> c(n, 0.f), l(n, 0.f), h(n, 0.f);
	for (const ChatMessage &m : chat) {
		const int i = int(std::floor(m.time + p.chatOffset - p.chatDelay));
		if (i < 0 || i >= n)
			continue;
		c[i] += 1;
		if (isLaughChat(m.text))
			l[i] += 1;
		else if (isHypeChat(m.text))
			h[i] += 1;
	}
	if (chat.isEmpty()) // 화면 채팅: 줄 수만 (웃음·감탄은 모름)
		for (int sec = 0; sec < screenLines.size(); ++sec) {
			const int i = int(std::floor(sec - p.chatDelay));
			if (i >= 0 && i < n)
				c[i] += screenLines[sec];
		}
	s.chatHasText = !chat.isEmpty();
	s.hasChat = s.chatHasText || !screenLines.isEmpty();
	s.chat = windowSum(c);
	s.laugh = windowSum(l);
	s.hype = windowSum(h);
	auto poisson = [](double m) { return std::sqrt(m + 1.0) + 0.5; };
	s.chatZ = robustZ(s.chat, poisson);
	s.laughZ = robustZ(s.laugh, poisson);
	s.hypeZ = robustZ(s.hype, poisson);

	s.hasAudio = !loudDb.isEmpty();
	QVector<float> db(n, -90.f);
	for (int i = 0; i < n && i < loudDb.size(); ++i)
		db[i] = loudDb[i];
	s.loudZ = s.hasAudio ? robustZ(db, [](double) { return 2.5; }) : QVector<float>(n, 0.f);
	return s;
}

// ───────────────────────── 잘한 장면 점수 ─────────────────────────

void scoreSegments(QVector<Segment> *segs, const QVector<KillEvent> &events, const QVector<GameResult> &games,
		   const Signals &sig)
{
	for (Segment &sg : *segs) {
		if (sg.kind != Segment::Kill)
			continue;
		QVector<double> killTimes;
		int kills = 0;
		for (const KillEvent &e : events) {
			if (!e.counts() || !e.kill || e.time < sg.start || e.time > sg.end)
				continue;
			for (int k = 0; k < std::max(1, e.kills); ++k)
				killTimes.push_back(e.time);
			kills += std::max(1, e.kills);
		}
		if (kills == 0) {
			sg.score = sg.assists > 0 ? 10 : -1;
			sg.note = sg.assists > 0 ? "어시스트" : QString();
			continue;
		}
		std::sort(killTimes.begin(), killTimes.end());
		int burst = 1;
		for (int i = 0; i < killTimes.size(); ++i) {
			int j = i;
			while (j + 1 < killTimes.size() && killTimes[j + 1] - killTimes[i] <= 15.0)
				++j;
			burst = std::max(burst, j - i + 1);
		}
		QStringList why;
		double score = 15 + std::min(kills, 6) * 10;
		why << (burst >= 2 ? QString("%1킬 (%2킬 몰아서)").arg(kills).arg(burst) : QString("%1킬").arg(kills));
		score += burst >= 4 ? 22 : burst == 3 ? 15 : burst == 2 ? 8 : 0;

		if (sig.hasChat) {
			const double a = killTimes.front() - 2, b = sg.end + 3;
			const double rz = std::max(sig.maxIn(sig.chatZ, a, b), sig.maxIn(sig.hypeZ, a, b));
			score += std::clamp(rz * 4.0, 0.0, 20.0);
			if (rz >= 3)
				why << "채팅 폭발";
			else if (rz >= 1.5)
				why << "채팅 반응";
		}
		if (sig.hasAudio) {
			double lz = 0;
			for (double t : killTimes)
				lz = std::max(lz, double(sig.maxIn(sig.loudZ, t - 2, t + 4)));
			score += std::clamp(lz * 3.0, 0.0, 12.0);
			if (lz >= 2.5)
				why << "큰 리액션";
		}
		const int gi = gameIndexAt(games, killTimes.back());
		if (gi >= 0) {
			const GameResult &g = games[gi];
			const int r = g.shownRank();
			if (r == 1 && g.status != GameResult::NoResult && g.end - killTimes.back() <= 60) {
				score += 15;
				why << "1등 마지막 교전";
			} else if (r == 1) {
				score += 5;
				why << "1등 판";
			} else if (r > 0 && r <= 3) {
				score += 3;
			}
		}
		sg.score = int(std::lround(std::clamp(score, 0.0, 100.0)));
		sg.note = why.join(" · ");
	}
}

// ───────────────────────── 재밌는 장면 ─────────────────────────

QVector<FunCandidate> findFunCandidates(const Signals &sig, const QVector<ChatMessage> &chat, const SignalParams &p,
					const QVector<Segment> &killSegs, double duration)
{
	QVector<FunCandidate> out;
	if (!sig.hasChat && !sig.hasAudio)
		return out;
	const int n = sig.seconds;
	auto inKill = [&](double t) {
		for (const Segment &s : killSegs)
			if (s.kind == Segment::Kill && t >= s.start - 3 && t <= s.end + 3)
				return true;
		return false;
	};
	struct Peak {
		int t;
		float f;
	};
	std::vector<Peak> peaks;
	for (int t = 0; t < n; ++t) {
		float f;
		const float loud = std::max(0.f, sig.hasAudio ? sig.loudZ[t] : 0.f);
		if (sig.chatHasText) {
			if (sig.laugh[t] < 3 || sig.laughZ[t] < 2.5f)
				continue;
			f = sig.laughZ[t] + 0.3f * loud;
		} else if (sig.hasChat) { // 화면 채팅: 채팅이 확 빨라진 곳
			if (sig.chat[t] < 4 || sig.chatZ[t] < 3.0f)
				continue;
			f = sig.chatZ[t] + 0.3f * loud;
		} else {
			if (sig.loudZ[t] < 3.5f)
				continue;
			f = sig.loudZ[t];
		}
		peaks.push_back({t, f});
	}
	std::sort(peaks.begin(), peaks.end(), [](const Peak &a, const Peak &b) { return a.f > b.f; });
	std::vector<int> taken;
	for (const Peak &pk : peaks) {
		if (inKill(pk.t))
			continue;
		bool near = false;
		for (int t : taken)
			near = near || std::abs(t - pk.t) < 40;
		if (near)
			continue;
		taken.push_back(pk.t);

		FunCandidate c;
		c.peak = pk.t;
		c.start = std::max(0.0, c.peak - 22);
		c.end = std::min(duration, c.peak + 8);
		// 킬 클립과 겹치면 경계에서 자름
		for (const Segment &s : killSegs) {
			if (s.kind != Segment::Kill)
				continue;
			if (s.end > c.start && s.end <= c.peak)
				c.start = s.end;
			if (s.start < c.end && s.start >= c.peak)
				c.end = s.start;
		}
		if (c.end - c.start < 8)
			continue;
		if (sig.chatHasText) {
			QSet<QString> seen;
			for (const ChatMessage &m : chat) {
				const double t = m.time + p.chatOffset - p.chatDelay;
				if (t >= c.peak - 5 && t <= c.peak + 10 && isLaughChat(m.text))
					++c.laughs;
				if (t >= c.peak - 8 && t <= c.peak + 12 && c.chatSample.size() < 14 && !seen.contains(m.text)) {
					seen.insert(m.text);
					c.chatSample << m.text.left(60);
				}
			}
			c.score = int(std::clamp(20.0 + sig.laughZ[pk.t] * 8.0 + c.laughs * 0.5, 0.0, 100.0));
			c.reason = QString("ㅋㅋ 채팅 %1개").arg(c.laughs);
			if (sig.hasAudio && sig.maxIn(sig.loudZ, c.peak - 15, c.peak) >= 2.5f)
				c.reason += " · 큰 웃음/소리";
		} else if (sig.hasChat) {
			const double z = sig.chatZ[pk.t];
			c.score = int(std::clamp(15.0 + z * 7.0, 0.0, 85.0));
			c.reason = QString("채팅이 빨라짐 (5초에 %1줄)").arg(int(std::lround(sig.chat[pk.t])));
			if (sig.hasAudio && sig.maxIn(sig.loudZ, c.peak - 15, c.peak) >= 2.5f)
				c.reason += " · 큰 웃음/소리";
		} else {
			c.score = int(std::clamp(10.0 + pk.f * 8.0, 0.0, 70.0));
			c.reason = "큰 소리 (채팅 없음)";
		}
		out.push_back(c);
		if (out.size() >= 80)
			break;
	}
	std::sort(out.begin(), out.end(), [](const FunCandidate &a, const FunCandidate &b) { return a.peak < b.peak; });
	return out;
}

void carryOverFun(const QVector<FunCandidate> &old, QVector<FunCandidate> *cands)
{
	for (FunCandidate &c : *cands)
		for (const FunCandidate &o : old)
			if (std::abs(o.peak - c.peak) <= 10) {
				c.ai = o.ai;
				c.aiScore = o.aiScore;
				c.title = o.title;
				c.transcript = o.transcript;
				c.aiStart = o.aiStart;
				c.aiEnd = o.aiEnd;
				c.added = o.added;
				break;
			}
}

QJsonArray funToJson(const QVector<FunCandidate> &c)
{
	QJsonArray a;
	for (const FunCandidate &x : c)
		a.append(QJsonObject{{"peak", x.peak}, {"ai", x.ai}, {"as", x.aiScore}, {"title", x.title},
				     {"tr", x.transcript}, {"a0", x.aiStart}, {"a1", x.aiEnd}, {"add", x.added}});
	return a;
}

QVector<FunCandidate> funFromJson(const QJsonArray &a)
{
	QVector<FunCandidate> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		FunCandidate c;
		c.peak = o["peak"].toDouble();
		c.ai = o["ai"].toInt();
		c.aiScore = o["as"].toInt();
		c.title = o["title"].toString();
		c.transcript = o["tr"].toString();
		c.aiStart = o["a0"].toDouble(-1);
		c.aiEnd = o["a1"].toDouble(-1);
		c.added = o["add"].toBool();
		out.push_back(c);
	}
	return out;
}

QJsonArray chatToJson(const QVector<ChatMessage> &c)
{
	QJsonArray a;
	for (const ChatMessage &m : c)
		a.append(QJsonArray{std::round(m.time * 100) / 100, m.text});
	return a;
}

QVector<ChatMessage> chatFromJson(const QJsonArray &a)
{
	QVector<ChatMessage> out;
	out.reserve(a.size());
	for (const auto &v : a) {
		const QJsonArray p = v.toArray();
		if (p.size() == 2)
			out.push_back({p[0].toDouble(), p[1].toString()});
	}
	return out;
}

// ───────────────────────── 분량 · 환기 ─────────────────────────

void fillToLength(QVector<Segment> *segs, double target)
{
	double funLen = 0;
	QVector<int> kill;
	for (int i = 0; i < segs->size(); ++i) {
		if ((*segs)[i].kind == Segment::Fun) {
			if ((*segs)[i].enabled)
				funLen += (*segs)[i].length();
		} else {
			kill.push_back(i);
		}
	}
	auto rank = [&](int i) {
		const Segment &s = (*segs)[i];
		return s.score >= 0 ? s.score : s.kills * 10;
	};
	std::stable_sort(kill.begin(), kill.end(), [&](int a, int b) { return rank(a) > rank(b); });
	double used = funLen;
	bool any = false;
	for (int i : kill) {
		Segment &s = (*segs)[i];
		const bool fits = used + s.length() <= target || !any;
		s.enabled = fits;
		if (fits) {
			used += s.length();
			any = true;
		}
	}
}

QVector<int> pickFunForBreaks(const QVector<Segment> &segs, const QVector<FunCandidate> &cands, double every)
{
	QVector<int> picked;
	if (every <= 0)
		return picked;
	QVector<Segment> on;
	for (const Segment &s : segs)
		if (s.enabled)
			on.push_back(s);
	std::sort(on.begin(), on.end(), [](const Segment &a, const Segment &b) { return a.start < b.start; });
	auto resultPos = [&](double t) {
		double p = 0;
		for (const Segment &s : on) {
			if (s.end <= t)
				p += s.length();
			else if (s.start < t)
				p += t - s.start;
		}
		return p;
	};
	double total = 0;
	for (const Segment &s : on)
		total += s.length();
	const int nSlots = int(total / every) + 1;
	QVector<bool> filled(nSlots, false);
	for (const Segment &s : on)
		if (s.kind == Segment::Fun)
			filled[std::min(nSlots - 1, int(resultPos(s.start) / every))] = true;
	QVector<int> best(nSlots, -1);
	for (int i = 0; i < cands.size(); ++i) {
		const FunCandidate &c = cands[i];
		if (c.added || c.ai == FunCandidate::Drop)
			continue;
		bool overlap = false;
		for (const Segment &s : on)
			overlap = overlap || (s.start < c.clipEnd() && c.clipStart() < s.end);
		if (overlap)
			continue;
		const int k = std::min(nSlots - 1, int(resultPos(c.peak) / every));
		if (filled[k])
			continue;
		if (best[k] < 0 || c.rankScore() > cands[best[k]].rankScore())
			best[k] = i;
	}
	for (int k = 0; k < nSlots; ++k)
		if (best[k] >= 0)
			picked.push_back(best[k]);
	return picked;
}
