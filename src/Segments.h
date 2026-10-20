#pragma once

#include <QJsonArray>
#include <QVector>

struct KillEvent {
	enum Check {
		Unchecked = 0,  // 아직 숫자로 확인 안 함
		Confirmed = 1,  // 전·후 숫자를 읽어 K 또는 A 가 실제로 올라간 것을 확인
		Rejected = 2,   // 숫자 모양은 바뀌었지만 K·A 값은 그대로 → 잘못 잡힌 킬 (클립에서 뺌)
		Unreadable = 3, // 숫자를 읽지 못함 (모양 변화만 믿음)
	};
	double time = 0; // 원본 영상 기준 초
	bool kill = false;
	bool assist = false;
	int kills = 1;   // 이때 올라간 K 수 (확인했을 때만 정확, 동시에 두 킬이면 2)
	int assists = 1; // 이때 올라간 A 수
	int check = Unchecked;
	bool counts() const { return check != Rejected && (kill || assist); }
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
	double chainGap = 20.0; // 킬·어시 뒤 이 시간 안에 다음 킬·어시가 나오면 한 클립으로 이어 붙임
	double mergeGap = 1.0;  // 여유를 붙인 구간끼리 이보다 가까우면 하나로
	bool includeAssist = false; // 어시스트만 있는 장면도 클립으로
};

// 킬 시점들 → 클립
//  1) 킬·어시를 시간순으로 보며 앞 것과 chainGap 안이면 한 묶음 (어시도 묶음을 이어 줌)
//  2) 묶음 = [첫 킬·어시 - before, 마지막 킬·어시 + after]. 킬이 없는 묶음은 includeAssist 일 때만
//  3) 숫자 확인에서 잘못 잡힌 것으로 나온 킬(Rejected)은 뺌
QVector<Segment> buildSegments(const QVector<KillEvent> &events, const SegmentParams &p, double duration);

QJsonArray eventsToJson(const QVector<KillEvent> &events);
QVector<KillEvent> eventsFromJson(const QJsonArray &a);
QJsonArray segmentsToJson(const QVector<Segment> &segs);
QVector<Segment> segmentsFromJson(const QJsonArray &a);
