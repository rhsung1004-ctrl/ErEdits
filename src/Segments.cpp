#include "Segments.h"

#include <QJsonObject>

#include <algorithm>

QVector<Segment> buildSegments(const QVector<KillEvent> &events, const SegmentParams &p, double duration)
{
	QVector<KillEvent> sorted;
	for (const KillEvent &e : events)
		if (e.kill || (p.includeAssist && e.assist))
			sorted.push_back(e);
	std::sort(sorted.begin(), sorted.end(), [](const KillEvent &a, const KillEvent &b) { return a.time < b.time; });

	QVector<Segment> out;
	for (const KillEvent &e : sorted) {
		Segment s;
		s.start = std::max(0.0, e.time - p.before);
		s.end = e.time + p.after;
		if (duration > 0)
			s.end = std::min(duration, s.end);
		s.kills = e.kill ? 1 : 0;
		s.assists = (!e.kill && e.assist) ? 1 : 0;
		if (s.end - s.start < 0.5)
			continue;
		if (!out.isEmpty() && s.start - out.back().end <= p.mergeGap) {
			Segment &last = out.back();
			last.end = std::max(last.end, s.end);
			last.kills += s.kills;
			last.assists += s.assists;
		} else {
			out.push_back(s);
		}
	}
	return out;
}

QJsonArray eventsToJson(const QVector<KillEvent> &events)
{
	QJsonArray a;
	for (const KillEvent &e : events)
		a.append(QJsonObject{{"t", e.time}, {"k", e.kill}, {"a", e.assist}});
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
