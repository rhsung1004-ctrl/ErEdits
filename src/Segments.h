#pragma once

#include <QJsonArray>
#include <QVector>

struct KillEvent {
	double time = 0; // 원본 영상 기준 초
	bool kill = false;
	bool assist = false;
};

// 결과 영상에 들어갈 한 구간 (원본 영상 기준 시간)
struct Segment {
	double start = 0;
	double end = 0;
	int kills = 0;
	int assists = 0;
	bool enabled = true;
	bool manual = false; // 직접 추가한 구간
	double length() const { return end - start; }
};

struct SegmentParams {
	double before = 10.0;   // 킬 몇 초 전부터
	double after = 5.0;     // 킬 몇 초 후까지
	double mergeGap = 3.0;  // 구간 사이가 이보다 가까우면 하나로
	bool includeAssist = false;
};

// 킬 시점들 → 앞뒤 여유를 붙인 구간, 겹치거나 가까운 구간은 하나로 합침
QVector<Segment> buildSegments(const QVector<KillEvent> &events, const SegmentParams &p, double duration);

QJsonArray eventsToJson(const QVector<KillEvent> &events);
QVector<KillEvent> eventsFromJson(const QJsonArray &a);
QJsonArray segmentsToJson(const QVector<Segment> &segs);
QVector<Segment> segmentsFromJson(const QJsonArray &a);
