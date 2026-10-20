#include "Games.h"

#include <QJsonObject>
#include <QMap>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kClusterGap = 45.0; // 결과 화면 사이가 이보다 벌어지면 다른 판
constexpr double kKillSlack = 1.0;   // 킬 감지는 숫자가 1초 그대로일 때 확정되므로 약간의 여유
} // namespace

QVector<GameResult> buildGames(const QVector<ResultCandidate> &candsIn, const QVector<RankReading> &readings,
			       const QVector<KillEvent> &events, double duration)
{
	QVector<ResultCandidate> cands = candsIn;
	std::sort(cands.begin(), cands.end(),
		  [](const ResultCandidate &a, const ResultCandidate &b) { return a.t0 < b.t0; });

	// 후보 묶기: 가까이 이어진 결과 화면은 한 판. 사이에 킬이 있으면 무조건 다른 판 (결과 화면·대기실에선 킬이 없음)
	auto killBetween = [&](double a, double b) {
		for (const KillEvent &e : events)
			if ((e.kill || e.assist) && e.time > a && e.time < b)
				return true;
		return false;
	};
	QVector<QVector<ResultCandidate>> clusters;
	for (const ResultCandidate &c : cands) {
		if (clusters.isEmpty() || c.t0 - clusters.back().back().t1 > kClusterGap ||
		    killBetween(clusters.back().back().t1, c.t0))
			clusters.push_back({});
		clusters.back().push_back(c);
	}

	QVector<GameResult> games;
	double prevEnd = 0;
	for (const auto &cl : clusters) {
		const double from = cl.front().t0 - 1.0, to = cl.back().t1 + 1.0;
		QMap<int, double> weight;    // 등수 → 표 (확실 1, 흐릿 0.5)
		QMap<int, int> strong;       // 등수 → 확실한 읽기 수
		QStringList ev;
		bool fromBoard = false;
		int boardRank = 0;
		for (const RankReading &r : readings) {
			if (r.time < from || r.time > to || r.rank <= 0)
				continue;
			weight[r.rank] += r.weak ? 0.5 : 1.0;
			if (!r.weak)
				++strong[r.rank];
			if (r.screen == rank::Screen::Scoreboard) {
				fromBoard = true;
				boardRank = r.rank;
			}
			const QString e = QString("%1 %2위%3").arg(rank::screenName(r.screen)).arg(r.rank).arg(r.weak ? "?" : "");
			if (!ev.contains(e))
				ev << e;
		}
		GameResult g;
		g.start = prevEnd;
		g.end = cl.front().t0;
		if (weight.isEmpty()) {
			if (cl.size() < 2)
				continue; // 버튼 하나만 잠깐 → 결과 화면이 아닐 가능성
			g.rank = 0;
			g.status = GameResult::Check;
			g.evidence = "결과 화면은 보였지만 숫자를 못 읽음";
		} else {
			int best = 0;
			double bw = -1;
			for (auto it = weight.begin(); it != weight.end(); ++it) {
				const bool better = it.value() > bw + 1e-9 ||
						    (std::abs(it.value() - bw) < 1e-9 && fromBoard && it.key() == boardRank);
				if (better) {
					best = it.key();
					bw = it.value();
				}
			}
			g.rank = best;
			const bool conflict = weight.size() > 1;
			g.status = (!conflict && strong.value(best) >= 2) ? GameResult::Sure : GameResult::Check;
			g.evidence = ev.join(" · ");
			if (conflict)
				g.evidence += " (서로 다름)";
		}
		games.push_back(g);
		prevEnd = g.end;
	}

	// 마지막 결과 화면 뒤에도 킬이 있으면 결과 없는 판
	bool trailing = false;
	for (const KillEvent &e : events)
		if (e.time > prevEnd + kKillSlack)
			trailing = true;
	if (trailing) {
		GameResult g;
		g.start = prevEnd;
		g.end = std::max(duration, prevEnd);
		g.status = GameResult::NoResult;
		g.evidence = games.isEmpty() ? "결과 화면을 찾지 못함" : "영상이 판 중간에 끝남";
		games.push_back(g);
	}

	for (const KillEvent &e : events) {
		if (!e.kill)
			continue;
		const int i = gameIndexAt(games, e.time);
		if (i >= 0)
			++games[i].kills;
	}
	return games;
}

void carryOverUserEdits(const QVector<GameResult> &old, QVector<GameResult> *games)
{
	for (GameResult &g : *games)
		for (const GameResult &o : old)
			if (std::abs(o.end - g.end) <= 20.0 && (o.status == GameResult::NoResult) == (g.status == GameResult::NoResult)) {
				g.userRank = o.userRank;
				g.userKeep = o.userKeep;
				break;
			}
}

bool keepGame(const GameResult &g, const GameFilter &f)
{
	if (g.userKeep >= 0)
		return g.userKeep == 1;
	if (!f.enabled)
		return true;
	const int r = g.shownRank();
	return r > 0 ? r <= f.maxRank : f.keepUnknown;
}

int gameIndexAt(const QVector<GameResult> &games, double t)
{
	for (int i = 0; i < games.size(); ++i)
		if (t <= games[i].end + kKillSlack)
			return i;
	return -1;
}

QVector<KillEvent> filterEvents(const QVector<KillEvent> &events, const QVector<GameResult> &games,
				const GameFilter &f)
{
	if (games.isEmpty())
		return events;
	QVector<KillEvent> out;
	for (const KillEvent &e : events) {
		const int i = gameIndexAt(games, e.time);
		if (i < 0 || keepGame(games[i], f))
			out.push_back(e);
	}
	return out;
}

QString statusText(GameResult::Status s)
{
	switch (s) {
	case GameResult::Sure:
		return "확실";
	case GameResult::Check:
		return "확인 필요";
	default:
		return "결과 없음";
	}
}

QJsonArray candidatesToJson(const QVector<ResultCandidate> &c)
{
	QJsonArray a;
	for (const ResultCandidate &x : c)
		a.append(QJsonArray{x.t0, x.t1});
	return a;
}

QVector<ResultCandidate> candidatesFromJson(const QJsonArray &a)
{
	QVector<ResultCandidate> out;
	for (const auto &v : a) {
		const QJsonArray p = v.toArray();
		if (p.size() == 2)
			out.push_back({p[0].toDouble(), p[1].toDouble()});
	}
	return out;
}

QJsonArray readingsToJson(const QVector<RankReading> &r)
{
	QJsonArray a;
	for (const RankReading &x : r)
		a.append(QJsonObject{{"t", x.time}, {"s", int(x.screen)}, {"r", x.rank}, {"w", x.weak}});
	return a;
}

QVector<RankReading> readingsFromJson(const QJsonArray &a)
{
	QVector<RankReading> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		RankReading r;
		r.time = o["t"].toDouble();
		r.screen = rank::Screen(o["s"].toInt());
		r.rank = o["r"].toInt();
		r.weak = o["w"].toBool(true);
		out.push_back(r);
	}
	return out;
}

QJsonArray gameEditsToJson(const QVector<GameResult> &g)
{
	QJsonArray a;
	for (const GameResult &x : g)
		if (x.userRank > 0 || x.userKeep >= 0)
			a.append(QJsonObject{{"end", x.end},
					     {"nr", x.status == GameResult::NoResult},
					     {"rank", x.userRank},
					     {"keep", x.userKeep}});
	return a;
}

QVector<GameResult> gameEditsFromJson(const QJsonArray &a)
{
	QVector<GameResult> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		GameResult g;
		g.end = o["end"].toDouble();
		g.status = o["nr"].toBool() ? GameResult::NoResult : GameResult::Check;
		g.userRank = o["rank"].toInt();
		g.userKeep = o["keep"].toInt(-1);
		out.push_back(g);
	}
	return out;
}
