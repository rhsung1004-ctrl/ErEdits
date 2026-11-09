#pragma once

#include "Games.h"
#include "Segments.h"

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

// AI 편집점: 채팅·소리·게임 신호로
//  1) 킬 클립마다 "잘한 장면" 점수 (0~100)
//  2) 킬이 없는 곳의 "재밌는 장면" 후보 (분위기 환기용)
// 를 계산. (화면·네트워크 없는 순수 계산)

struct ChatMessage {
	double time = 0; // 다시보기 기준 초
	QString text;
};

bool isLaughChat(const QString &text); // ㅋㅋ, ㅎㅎㅎ, 웃겨 …
bool isHypeChat(const QString &text);  // ㄷㄷ, 미쳤다, 나이스, 캬 …

struct SignalParams {
	double chatOffset = 0; // 녹화 파일 시간 = 다시보기 시간 + chatOffset
	double chatDelay = 5;  // 시청자가 반응을 치기까지 걸리는 시간 (채팅 시각에서 이만큼 앞당겨 봄)
};

// 1초 단위 신호. 각 값은 그 영상에서 "평소보다 얼마나 튀는지" (0 = 평소, 3 이상 = 눈에 띄게)
struct Signals {
	int seconds = 0;
	bool hasChat = false;     // 채팅 신호 있음 (받은 채팅 또는 화면 채팅)
	bool chatHasText = false; // 채팅 내용까지 있음 (치지직에서 받은 채팅) — 없으면 화면에서 잰 속도만
	bool hasAudio = false;
	QVector<float> chat;  // 채팅 수 (5초 합)
	QVector<float> laugh; // 웃음 채팅 수 (5초 합)
	QVector<float> hype;  // 감탄 채팅 수 (5초 합)
	QVector<float> chatZ, laughZ, hypeZ, loudZ;
	float at(const QVector<float> &v, double t) const;
	float maxIn(const QVector<float> &v, double a, double b) const;
};

// chat: 치지직에서 받은 채팅 (있으면 이걸 씀)
// screenLines: 방송 화면 채팅창에서 잰 초마다 새 채팅 줄 수 (받은 채팅이 없을 때)
// loudDb: 1초마다 소리 크기(dB, 없으면 빈 배열)
Signals computeSignals(const QVector<ChatMessage> &chat, const QVector<float> &screenLines,
		       const QVector<float> &loudDb, double duration, const SignalParams &p);

// 킬 클립 점수 매기기 (kind == Kill 인 클립만, score·note 를 채움)
void scoreSegments(QVector<Segment> *segs, const QVector<KillEvent> &events, const QVector<GameResult> &games,
		   const Signals &sig);

// 재밌는 장면 후보
struct FunCandidate {
	double peak = 0;       // 반응이 가장 컸던 시각
	double start = 0, end = 0;
	int score = 0;         // 0~100 (채팅·소리)
	int laughs = 0;        // 구간 웃음 채팅 수
	QString reason;
	QStringList chatSample; // 그 순간 채팅 몇 줄 (AI 판단용)
	// 로컬 AI 확인 결과
	enum Ai { NotChecked = 0, Keep = 1, Drop = 2, Failed = 3 };
	int ai = NotChecked;
	int aiScore = 0;       // 1~10
	QString title;         // 한 줄 설명
	QString transcript;    // 음성 인식 결과 (시간 포함 줄들)
	double aiStart = -1, aiEnd = -1; // AI 가 고른 시작·끝 (-1 = 없음)
	bool added = false;    // 클립으로 넣음
	double clipStart() const { return aiStart >= 0 ? aiStart : start; }
	double clipEnd() const { return aiEnd > aiStart && aiStart >= 0 ? aiEnd : end; }
	int rankScore() const { return ai == Keep ? 40 + aiScore * 6 : ai == Drop ? score / 4 : score; }
};

// killSegs: 킬 클립 (그 안의 반응은 이미 잘한 장면이라 뺌)
QVector<FunCandidate> findFunCandidates(const Signals &sig, const QVector<ChatMessage> &chat, const SignalParams &p,
					const QVector<Segment> &killSegs, double duration);

// 후보 목록을 새로 만들 때 이전 AI 결과·넣음 표시를 옮김 (반응 시각이 10초 안이면 같은 후보)
void carryOverFun(const QVector<FunCandidate> &old, QVector<FunCandidate> *cands);

QJsonArray funToJson(const QVector<FunCandidate> &c);
QVector<FunCandidate> funFromJson(const QJsonArray &a);
QJsonArray chatToJson(const QVector<ChatMessage> &c);
QVector<ChatMessage> chatFromJson(const QJsonArray &a);

// 점수 높은 킬 클립부터 켜서 결과 길이를 target 초 안으로 (재밌는 장면은 그대로 두고 길이에만 셈)
void fillToLength(QVector<Segment> *segs, double target);

// 결과 영상 every 초마다 재밌는 장면 하나씩 고름 (이미 넣은 칸은 건너뜀). 고른 후보 번호
QVector<int> pickFunForBreaks(const QVector<Segment> &segs, const QVector<FunCandidate> &cands, double every);
