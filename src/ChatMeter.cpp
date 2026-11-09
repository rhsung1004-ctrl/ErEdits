#include "ChatMeter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
constexpr float kTextRow = 4.0f; // 한 줄에 글자 획이 이만큼 있으면 글줄
constexpr int kMinBand = 9;      // 글줄 높이 하한 (줄 수, 256 기준) — 말풍선 사이로 비치는 배경 글씨 조각 제외
constexpr int kEdge = 2;         // 위아래 가장자리에 이만큼 붙은 글줄은 아직 다 안 보임
constexpr int kBins = ChatMeter::kBins;
} // namespace

void ChatMeter::reset()
{
	m_prev.clear();
	m_prevGrid.clear();
	m_prevBands.clear();
	m_hasPrev = false;
	m_lastShift = 0;
}

// 줄별 글자 획 수 (어두운 말풍선 위 밝은 글자: 옆 칸과 밝기 차이가 크고 밝은 쪽이 밝음)
//  p: 줄 합계, grid: 줄 × 가로 칸(kBins) — 글이 가로로 어디까지 있는지까지 봐야
//  높이가 같은 말풍선이 정확히 한 칸 밀린 것과 "그대로"를 구별할 수 있음
void ChatMeter::profile(const uint8_t *g, std::vector<float> *p, std::vector<float> *grid)
{
	std::vector<float> raw(size_t(kH) * kBins, 0.f);
	for (int y = 0; y < kH; ++y) {
		const uint8_t *r = g + y * kW;
		for (int x = 0; x + 1 < kW; ++x) {
			const int a = r[x], b = r[x + 1];
			if (std::abs(a - b) > 40 && std::max(a, b) > 120 && std::min(a, b) < 110)
				raw[size_t(y) * kBins + size_t(x * kBins / kW)] += 1.f;
		}
	}
	grid->assign(size_t(kH) * kBins, 0.f);
	p->assign(kH, 0.f);
	for (int y = 0; y < kH; ++y) { // 1-2-1 로 살짝 뭉갬 (반 줄 어긋나도 맞도록)
		const int ya = std::max(0, y - 1), yb = std::min(kH - 1, y + 1);
		for (int c = 0; c < kBins; ++c) {
			const float v = (2 * raw[size_t(y) * kBins + c] + raw[size_t(ya) * kBins + c] + raw[size_t(yb) * kBins + c]) / 4.f;
			(*grid)[size_t(y) * kBins + c] = v;
			(*p)[size_t(y)] += v;
		}
	}
}

std::vector<ChatMeter::Band> ChatMeter::bands(const std::vector<float> &p)
{
	std::vector<Band> out;
	int y = 0;
	while (y < kH) {
		if (p[size_t(y)] < kTextRow) {
			++y;
			continue;
		}
		int e = y;
		int gap = 0;
		for (int k = y; k < kH; ++k) { // 2줄 이하 끊김은 이어 봄
			if (p[size_t(k)] >= kTextRow) {
				e = k + 1;
				gap = 0;
			} else if (++gap > 2) {
				break;
			}
		}
		if (e - y >= kMinBand)
			out.push_back({y, e});
		y = e + 1;
	}
	return out;
}

int ChatMeter::feed(const uint8_t *gray)
{
	std::vector<float> cur, grid;
	profile(gray, &cur, &grid);
	const std::vector<Band> cb = bands(cur);
	int fresh = 0;
	if (m_hasPrev) {
		double total = 0;
		for (int y = 0; y < kH; ++y)
			total += m_prev[size_t(y)] + cur[size_t(y)];
		// 위로 밀린 정도 찾기: 앞 장면 y+s 줄이 지금 y 줄과 같아야 함
		int best = 0;
		double bestSim = -1;
		if (total > 0) {
			for (int s = -3; s <= int(kH * 0.6); ++s) {
				double mn = 0;
				for (int y = std::max(0, -s); y < std::min(kH, kH - s); ++y) {
					const float *pv = &m_prevGrid[size_t(y + s) * kBins];
					const float *cv = &grid[size_t(y) * kBins];
					for (int c = 0; c < kBins; ++c)
						mn += std::min(pv[c], cv[c]);
				}
				// 겹치는 부분이 작아지면 우연히 맞기 쉬우니 전체 대비로
				const double sim = 2 * mn / total;
				// 확실히 더 잘 맞으면 바꾸고, 비슷하면 덜 밀린 쪽
				if (sim > bestSim + 0.01 || (sim >= bestSim - 0.01 && std::abs(s) < std::abs(best))) {
					bestSim = std::max(bestSim, sim);
					best = s;
				}
			}
		}
		m_lastShift = best;
		// 새 글줄: 다 보이는 글줄 중 (밀린 만큼 옮긴) 앞 장면의 다 보였던 글줄과 겹치지 않는 것
		for (const Band &b : cb) {
			if (b.y0 < kEdge || b.y1 > kH - kEdge)
				continue;
			bool old = false;
			for (const Band &pb : m_prevBands) {
				if (pb.y1 > kH - kEdge) // 앞에서는 아래가 덜 보였던 글줄 → 이번에 처음 다 보임
					continue;
				const int a0 = pb.y0 - best, a1 = pb.y1 - best;
				const int ov = std::min(a1, b.y1) - std::max(a0, b.y0);
				if (ov > 0.3 * (b.y1 - b.y0)) {
					old = true;
					break;
				}
			}
			if (!old)
				++fresh;
		}
		fresh = std::min(fresh, 8); // 화면이 통째로 바뀐 경우(장면 전환) 너무 크게 세지 않게
	}
	m_prev = std::move(cur);
	m_prevGrid = std::move(grid);
	m_prevBands = cb;
	m_hasPrev = true;
	return fresh;
}
