#pragma once

#include <cstdint>
#include <vector>

// 방송 화면에 띄운 채팅창(오른쪽 말풍선 목록)에서 새로 올라온 채팅 줄 수를 셈 — 화면만 보고 채팅 속도를 잼
//
//  1) 장면마다 줄별 "글자 획" 개수 → 글줄이 있는 곳이 봉우리로 나타남
//  2) 앞 장면과 비교해 채팅창이 위로 몇 줄 밀렸는지 맞춰 봄 (새 채팅이 아래에서 밀어 올림)
//  3) 맞춰 본 뒤에도 앞 장면에 없던 글줄 = 새 채팅 줄
//  - 화면 가장자리에 걸린 글줄은 다 보일 때 한 번만 셈
//  - 배경(게임 화면)이 비쳐 생기는 잡음은 이후 "평소보다 얼마나 튀는지"로 걸러짐
class ChatMeter {
public:
	static constexpr int kW = 192; // 채팅 영역을 이 크기 밝기 영상으로 줄여서 넣음
	static constexpr int kH = 256;
	static constexpr int kBins = 12; // 가로 칸 수 (글이 어디까지 있는지)

	void reset();
	// gray: kW x kH. 이번 장면에서 새로 나타난 글줄 수
	int feed(const uint8_t *gray);

	// 검사용
	int lastShift() const { return m_lastShift; }

private:
	struct Band {
		int y0, y1; // [y0, y1)
	};
	static void profile(const uint8_t *g, std::vector<float> *p, std::vector<float> *grid);
	static std::vector<Band> bands(const std::vector<float> &p);

	std::vector<float> m_prev;
	std::vector<float> m_prevGrid;
	std::vector<Band> m_prevBands;
	bool m_hasPrev = false;
	int m_lastShift = 0;
};
