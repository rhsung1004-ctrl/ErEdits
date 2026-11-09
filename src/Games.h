#pragma once

#include "ResultScreen.h"
#include "Segments.h"

#include <QJsonArray>
#include <QString>
#include <QVector>

// 결과 화면 후보 (분석 중 파란 버튼이 보인 구간)
struct ResultCandidate {
	double t0 = 0, t1 = 0;
	double mid() const { return 0.5 * (t0 + t1); }
};

// 결과 화면 한 장에서 읽은 등수
struct RankReading {
	double time = 0;
	rank::Screen screen = rank::Screen::None;
	int rank = 0;
	bool weak = true;
};

// 한 판
struct GameResult {
	enum Status {
		Sure,     // 두 화면 이상에서 같은 등수
		Check,    // 한 화면에서만 읽었거나 서로 다름 / 숫자를 못 읽음
		NoResult, // 결과 화면이 없음 (영상이 판 중간에 끝남)
	};
	double start = 0;  // 이전 판 결과 화면 (첫 판은 0)
	double end = 0;    // 이 판 결과 화면이 처음 보인 시각
	int rank = 0;      // 0 = 모름
	Status status = Check;
	QString evidence;  // "결과 화면 5위 · 순위표 5위"
	int kills = 0;     // 이 판에서 찾은 킬 수
	// 직접 고친 값 (다시 분석·다시 읽어도 유지)
	int userRank = 0;  // 0 = 안 고침
	int userKeep = -1; // -1 자동, 0 뺌, 1 남김
	int shownRank() const { return userRank > 0 ? userRank : rank; }
};

struct GameFilter {
	bool enabled = false;
	int maxRank = 3;         // 이 등수 이내만 남김
	bool keepUnknown = true; // 등수를 모르는 판도 남김
};

// 후보·읽은 값 → 판 목록. 결과 화면 후보가 45초 안에 이어지고 사이에 킬이 없으면 한 판의 결과로 묶음
//  - 숫자를 하나도 못 읽은 묶음은 후보가 2개 이상일 때만 판으로 인정 (다른 파란 버튼 오인 방지)
//  - 마지막 결과 화면 뒤에 킬이 있으면 "결과 없음" 판을 덧붙임
QVector<GameResult> buildGames(const QVector<ResultCandidate> &cands, const QVector<RankReading> &readings,
			       const QVector<KillEvent> &events, double duration);

// 이전 판 목록에서 직접 고친 값을 새 목록으로 옮김 (끝난 시각이 20초 안이면 같은 판)
void carryOverUserEdits(const QVector<GameResult> &old, QVector<GameResult> *games);

// 이 판을 남기는지
bool keepGame(const GameResult &g, const GameFilter &f);

// t 가 속한 판 번호 (-1 = 없음)
int gameIndexAt(const QVector<GameResult> &games, double t);

// 남기는 판의 킬만
QVector<KillEvent> filterEvents(const QVector<KillEvent> &events, const QVector<GameResult> &games,
				const GameFilter &f);

QString statusText(GameResult::Status s);

QJsonArray candidatesToJson(const QVector<ResultCandidate> &c);
QVector<ResultCandidate> candidatesFromJson(const QJsonArray &a);
QJsonArray readingsToJson(const QVector<RankReading> &r);
QVector<RankReading> readingsFromJson(const QJsonArray &a);
QJsonArray gameEditsToJson(const QVector<GameResult> &g);
QVector<GameResult> gameEditsFromJson(const QJsonArray &a);
