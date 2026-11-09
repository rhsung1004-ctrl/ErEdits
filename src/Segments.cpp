#include "Segments.h"

#include <QJsonObject>

#include <algorithm>

QVector<Segment> buildSegments(const QVector<KillEvent> &events, const SegmentParams &p, double duration)
{
	QVector<KillEvent> sorted;
	for (const KillEvent &e : events)
		if (e.counts())
			sorted.push_back(e);
	std::sort(sorted.begin(), sorted.end(), [](const KillEvent &a, const KillEvent &b) { return a.time < b.time; });

	QVector<Segment> out;
	auto emitChain = [&](double first, double last, int kills, int assists) {
		if (kills == 0 && !p.includeAssist)
			return;
		Segment s;
		s.start = std::max(0.0, first - p.before);
		s.end = last + p.after;
		if (duration > 0)
			s.end = std::min(duration, s.end);
		s.kills = kills;
		s.assists = assists;
		if (s.end - s.start < 0.5)
			return;
		if (!out.isEmpty() && s.start - out.back().end <= p.mergeGap) {
			Segment &prev = out.back();
			prev.end = std::max(prev.end, s.end);
			prev.kills += s.kills;
			prev.assists += s.assists;
		} else {
			out.push_back(s);
		}
	};
	double first = 0, last = -1e9;
	int kills = 0, assists = 0;
	bool open = false;
	for (const KillEvent &e : sorted) {
		if (open && e.time - last > p.chainGap) {
			emitChain(first, last, kills, assists);
			open = false;
		}
		if (!open) {
			first = e.time;
			kills = assists = 0;
			open = true;
		}
		last = e.time;
		if (e.kill)
			kills += std::max(1, e.kills);
		else if (e.assist)
			assists += std::max(1, e.assists);
	}
	if (open)
		emitChain(first, last, kills, assists);
	return out;
}

QJsonArray eventsToJson(const QVector<KillEvent> &events)
{
	QJsonArray a;
	for (const KillEvent &e : events)
		a.append(QJsonObject{{"t", e.time}, {"k", e.kill}, {"a", e.assist}, {"n", e.kills}, {"na", e.assists},
				     {"c", e.check}});
	return a;
}

QVector<KillEvent> eventsFromJson(const QJsonArray &a)
{
	QVector<KillEvent> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		KillEvent e;
		e.time = o["t"].toDouble();
		e.kill = o["k"].toBool();
		e.assist = o["a"].toBool();
		e.kills = o["n"].toInt(1);
		e.assists = o["na"].toInt(1);
		e.check = o["c"].toInt(KillEvent::Unchecked);
		out.push_back(e);
	}
	return out;
}

QJsonArray segmentsToJson(const QVector<Segment> &segs)
{
	QJsonArray a;
	for (const Segment &s : segs)
		a.append(QJsonObject{{"s", s.start}, {"e", s.end}, {"k", s.kills}, {"a", s.assists}, {"on", s.enabled},
				     {"m", s.manual}});
	return a;
}

QVector<Segment> segmentsFromJson(const QJsonArray &a)
{
	QVector<Segment> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		Segment s;
		s.start = o["s"].toDouble();
		s.end = o["e"].toDouble();
		s.kills = o["k"].toInt();
		s.assists = o["a"].toInt();
		s.enabled = o["on"].toBool(true);
		s.manual = o["m"].toBool();
		if (s.end > s.start)
			out.push_back(s);
	}
	return out;
}
