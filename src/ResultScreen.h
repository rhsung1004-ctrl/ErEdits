#pragma once

#include <cstdint>
#include <vector>

// 게임이 끝난 뒤 나오는 결과 화면을 찾고 등수를 읽음 (Qt 없이 쓰는 순수 코드)
//
// 결과 화면 3종 (모두 화면 아래 가운데에 파란 버튼이 있음)
//  - 결과 화면:  왼쪽에 큰 파란 숫자 "5 /7"            + [나가기]
//  - 점수 창:    가운데 "매치 순위: 1위"                + [계속하기]  (랭크 대전)
//  - 순위표:     왼쪽 위 "5위", 위쪽에 파란 [순위표] 탭 + [나가기]
//
// 1단계 (분석 중, 초당 5장): 게임 화면을 320x180 으로 줄인 장면에서 파란 버튼만 찾음 → 가벼움
// 2단계 (분석 뒤):          버튼이 보였던 시각의 원본 장면을 1920x1080 으로 꺼내 숫자를 읽음
namespace rank {

// 1단계 입력 크기 (게임 화면 전체를 줄인 RGB)
constexpr int kSmallW = 320;
constexpr int kSmallH = 180;
// 2단계 입력 크기 (게임 화면 전체를 이 크기로 맞춘 RGB)
constexpr int kFullW = 1920;
constexpr int kFullH = 1080;

struct Box {
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	int w() const { return x1 - x0; }
	int h() const { return y1 - y0; }
};

// 320x180 RGB 장면에서 결과 화면의 파란 버튼을 찾음
bool findResultButton(const uint8_t *rgb, Box *button);

// 버튼이 이어서 보인 구간 하나 = 결과 화면 후보
struct Candidate {
	double t0 = 0; // 처음 보인 시각 (초)
	double t1 = 0; // 마지막으로 보인 시각
	Box button;
	double mid() const { return 0.5 * (t0 + t1); }
};

// 장면을 차례로 넣으면 버튼이 보인 구간을 후보로 돌려줌
//  - 버튼 위치·크기가 바뀌면(점수 창 → 순위표) 다른 화면이므로 후보를 나눔
//  - 0.6초 이하로 잠깐 안 보인 건 같은 구간으로 봄
class ResultWatcher {
public:
	void reset();
	void feed(const uint8_t *rgb, double t, std::vector<Candidate> *out);
	void finish(std::vector<Candidate> *out); // 영상 끝: 진행 중인 구간 마무리

private:
	bool m_open = false;
	Candidate m_cur;
};

enum class Screen { None = 0, Result = 1, RankPoint = 2, Scoreboard = 3 };
const char *screenName(Screen s); // "결과 화면" 등

struct Reading {
	Screen screen = Screen::None;
	int rank = 0;          // 0 = 못 읽음
	double distance = 99;  // 숫자 모양 차이 (작을수록 확실)
	double margin = 0;     // 2등 후보와의 차이 (클수록 확실)
	bool weak = true;      // 덜 확실한 읽기
	bool ok() const { return rank > 0; }
};

// 1920x1080 RGB 장면에서 세 가지 결과 화면을 모두 시도해 등수를 읽음
Reading readRank(const uint8_t *rgb);

// 낱개 읽기 (검사용)
Reading readResultScreen(const uint8_t *rgb);
Reading readRankPoint(const uint8_t *rgb);
Reading readScoreboard(const uint8_t *rgb);

// 킬 숫자 줄(TK·K·A)을 크게 잘라 낸 RGB(w x h)에서 세 숫자를 읽음 (킬 확인용)
//  counts: TK, K, A. weak: 덜 확실한 숫자가 섞임. 못 읽으면 false
constexpr int kHudScale = 6; // 킬 숫자 영역(160x32 기준)을 이 배수로 키워서 읽음
bool readHudCounts(const uint8_t *rgb, int w, int h, int counts[3], bool *weak);

// 숫자 하나 알아보기: mask 는 w x h (1 = 글자). 0~9, 못 알아보면 -1
int classifyDigit(const std::vector<uint8_t> &mask, int w, int h, double *distance, double *margin);

} // namespace rank
