#include "ResultScreen.h"

#include <algorithm>
#include <cmath>

namespace rank {
namespace {

struct DigitTemplate {
	int digit;
	int holes;
	float aspect;
	uint8_t grid[160];
};
#include "DigitTemplates.inc"

constexpr int kGW = 10, kGH = 16;

// ── 색 판별 ──
// 결과 화면 버튼·탭·큰 숫자에 쓰이는 파란색 (약 RGB 35,120,160 / 숫자는 42,137,181)
inline bool buttonBlue(const uint8_t *p)
{
	const int r = p[0], g = p[1], b = p[2];
	return b >= 125 && b <= 205 && g >= 90 && g <= 150 && r < 80 && b - r > 80 && g - r > 50;
}
inline bool tabBlue(const uint8_t *p) // [순위표] 탭은 조금 어두움 (37,98,127)
{
	const int r = p[0], g = p[1], b = p[2];
	return b >= 95 && r < 110 && b - r > 55 && g - r > 35;
}
inline bool digitBlue(const uint8_t *p)
{
	const int r = p[0], g = p[1], b = p[2];
	return b > 140 && b - r > 90 && g - r > 50;
}

// ── 덩어리 찾기 ──
struct Comp {
	Box b;
	int count = 0;
	std::vector<uint8_t> mask; // b.w() x b.h()
};

// mask(w x h) 에서 8방향으로 이어진 덩어리들 (minPix 미만은 버림)
std::vector<Comp> components(const std::vector<uint8_t> &mask, int w, int h, int minPix)
{
	std::vector<int> label(size_t(w * h), 0);
	std::vector<Comp> out;
	std::vector<int> stack, pixels;
	int next = 0;
	for (int i = 0; i < w * h; ++i) {
		if (!mask[size_t(i)] || label[size_t(i)])
			continue;
		++next;
		stack.assign(1, i);
		pixels.clear();
		label[size_t(i)] = next;
		Box b{w, h, -1, -1};
		while (!stack.empty()) {
			const int p = stack.back();
			stack.pop_back();
			pixels.push_back(p);
			const int x = p % w, y = p / w;
			b.x0 = std::min(b.x0, x);
			b.y0 = std::min(b.y0, y);
			b.x1 = std::max(b.x1, x + 1);
			b.y1 = std::max(b.y1, y + 1);
			for (int dy = -1; dy <= 1; ++dy)
				for (int dx = -1; dx <= 1; ++dx) {
					const int nx = x + dx, ny = y + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h)
						continue;
					const int q = ny * w + nx;
					if (mask[size_t(q)] && !label[size_t(q)]) {
						label[size_t(q)] = next;
						stack.push_back(q);
					}
				}
		}
		if (int(pixels.size()) < minPix)
			continue;
		Comp c;
		c.b = b;
		c.count = int(pixels.size());
		c.mask.assign(size_t(b.w() * b.h()), 0);
		for (int p : pixels)
			c.mask[size_t((p / w - b.y0) * b.w() + (p % w - b.x0))] = 1;
		out.push_back(std::move(c));
	}
	std::sort(out.begin(), out.end(), [](const Comp &a, const Comp &b) { return a.b.x0 < b.b.x0; });
	return out;
}

// 1920x1080 장면의 비율 영역
struct Region {
	int x0, y0, w, h;
};
Region region(double fx0, double fy0, double fx1, double fy1)
{
	const int x0 = int(fx0 * kFullW), y0 = int(fy0 * kFullH);
	return {x0, y0, int(fx1 * kFullW) - x0, int(fy1 * kFullH) - y0};
}

// 흰 글자 (밝고 색이 거의 없음). 기준 밝기는 영역에서 가장 밝은 쪽에 맞춤
std::vector<uint8_t> whiteMask(const uint8_t *rgb, const Region &r)
{
	std::vector<uint8_t> m(size_t(r.w * r.h), 0);
	int hist[256] = {};
	for (int y = 0; y < r.h; ++y)
		for (int x = 0; x < r.w; ++x) {
			const uint8_t *p = rgb + (size_t(r.y0 + y) * kFullW + size_t(r.x0 + x)) * 3;
			++hist[(p[0] + p[1] + p[2]) / 3];
		}
	const int total = r.w * r.h;
	int acc = 0, p995 = 255;
	for (int v = 255; v >= 0; --v) {
		acc += hist[v];
		if (acc >= total * 0.005) {
			p995 = v;
			break;
		}
	}
	const int thr = std::max(140, int(0.65 * p995));
	for (int y = 0; y < r.h; ++y)
		for (int x = 0; x < r.w; ++x) {
			const uint8_t *p = rgb + (size_t(r.y0 + y) * kFullW + size_t(r.x0 + x)) * 3;
			const int lum = (p[0] + p[1] + p[2]) / 3;
			const int sat = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
			m[size_t(y * r.w + x)] = lum > thr && sat < 60;
		}
	return m;
}

template <class F> std::vector<uint8_t> colorMask(const uint8_t *rgb, const Region &r, F test)
{
	std::vector<uint8_t> m(size_t(r.w * r.h), 0);
	for (int y = 0; y < r.h; ++y)
		for (int x = 0; x < r.w; ++x)
			m[size_t(y * r.w + x)] = test(rgb + (size_t(r.y0 + y) * kFullW + size_t(r.x0 + x)) * 3);
	return m;
}

// 왼쪽부터 이어진 숫자 덩어리들 ("5위" → 5, "12위" → 1,2)
//  - 가장 큰 글자 높이의 60% 이상인 첫 덩어리부터 시작
//  - 높이·윗선이 비슷한 덩어리까지만 (위 의 ㅇ·ㅜ 는 낮고, ㅣ 는 숫자보다 높아서 멈춤)
std::vector<const Comp *> digitRun(const std::vector<Comp> &all)
{
	std::vector<const Comp *> out;
	int H = 0;
	for (const Comp &c : all)
		H = std::max(H, c.b.h());
	std::vector<const Comp *> cs;
	for (const Comp &c : all)
		if (c.b.h() >= 0.25 * H)
			cs.push_back(&c);
	size_t i = 0;
	while (i < cs.size() && cs[i]->b.h() < 0.6 * H)
		++i;
	if (i == cs.size())
		return out;
	const Comp *f = cs[i];
	const double h0 = f->b.h();
	out.push_back(f);
	for (size_t j = i + 1; j < cs.size(); ++j) {
		const double h = cs[j]->b.h();
		if (h >= 0.85 * h0 && h <= 1.15 * h0 && std::abs(cs[j]->b.y0 - f->b.y0) < 0.2 * h0)
			out.push_back(cs[j]);
		else
			break;
	}
	return out;
}

Reading readDigits(Screen screen, const std::vector<const Comp *> &run)
{
	Reading r;
	r.screen = screen;
	if (run.empty() || run.size() > 2)
		return r;
	int value = 0;
	double dist = 0, margin = 1e9;
	for (const Comp *c : run) {
		double d = 0, m = 0;
		const int digit = classifyDigit(c->mask, c->b.w(), c->b.h(), &d, &m);
		if (digit < 0)
			return r;
		value = value * 10 + digit;
		dist = std::max(dist, d);
		margin = std::min(margin, m);
	}
	if (value < 1 || value > 24 || dist > 3.0)
		return r;
	r.rank = value;
	r.distance = dist;
	r.margin = margin;
	r.weak = dist >= 1.5 || margin < 0.4;
	return r;
}

} // namespace

const char *screenName(Screen s)
{
	switch (s) {
	case Screen::Result:
		return "결과 화면";
	case Screen::RankPoint:
		return "점수 창";
	case Screen::Scoreboard:
		return "순위표";
	default:
		return "-";
	}
}

// ───────────────────────── 1단계: 버튼 찾기 ─────────────────────────

bool findResultButton(const uint8_t *rgb, Box *button)
{
	// 아래 가운데 영역만 (320x180 기준)
	const int X0 = int(0.28 * kSmallW), X1 = int(0.72 * kSmallW);
	const int Y0 = int(0.78 * kSmallH), Y1 = kSmallH;
	const int w = X1 - X0, h = Y1 - Y0;
	std::vector<uint8_t> m(size_t(w * h), 0);
	int any = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const bool b = buttonBlue(rgb + (size_t(Y0 + y) * kSmallW + size_t(X0 + x)) * 3);
			m[size_t(y * w + x)] = b;
			any += b;
		}
	if (any < 150) // 버튼 넓이의 절반도 안 됨
		return false;
	const auto cs = components(m, w, h, 150);
	const Comp *best = nullptr;
	for (const Comp &c : cs) {
		const double bw = c.b.w(), bh = c.b.h();
		const double fill = c.count / (bw * bh);
		const double cx = X0 + c.b.x0 + bw / 2;
		if (bw < 0.09 * kSmallW || bw > 0.25 * kSmallW || bh < 0.03 * kSmallH || bh > 0.10 * kSmallH)
			continue;
		if (fill < 0.75 || bw / bh < 2.5 || bw / bh > 7.5)
			continue;
		if (cx < 0.42 * kSmallW || cx > 0.58 * kSmallW)
			continue;
		if (!best || c.count > best->count)
			best = &c;
	}
	if (!best)
		return false;
	if (button)
		*button = Box{X0 + best->b.x0, Y0 + best->b.y0, X0 + best->b.x1, Y0 + best->b.y1};
	return true;
}

void ResultWatcher::reset()
{
	m_open = false;
	m_cur = Candidate{};
}

void ResultWatcher::feed(const uint8_t *rgb, double t, std::vector<Candidate> *out)
{
	Box b;
	const bool found = findResultButton(rgb, &b);
	if (m_open) {
		const Box &c = m_cur.button;
		const bool same = found && std::abs(b.w() - c.w()) <= std::max(3, int(0.08 * c.w())) &&
				  std::abs(b.y0 - c.y0) <= 2 && std::abs(b.x0 - c.x0) <= 3;
		if (same && t - m_cur.t1 <= 0.65) {
			m_cur.t1 = t;
			return;
		}
		if (found || t - m_cur.t1 > 0.6) {
			out->push_back(m_cur);
			m_open = false;
		}
	}
	if (found && !m_open) {
		m_open = true;
		m_cur.t0 = m_cur.t1 = t;
		m_cur.button = b;
	}
}

void ResultWatcher::finish(std::vector<Candidate> *out)
{
	if (m_open)
		out->push_back(m_cur);
	m_open = false;
}

// ───────────────────────── 2단계: 등수 읽기 ─────────────────────────

Reading readResultScreen(const uint8_t *rgb)
{
	// 왼쪽 큰 파란 숫자 "5" (뒤의 "/7" 은 회색이라 빠짐)
	const Region r = region(0.10, 0.30, 0.26, 0.47);
	const auto cs = components(colorMask(rgb, r, digitBlue), r.w, r.h, 30);
	std::vector<Comp> big;
	for (const Comp &c : cs)
		if (c.b.h() >= 0.04 * kFullH && c.b.h() <= 0.14 * kFullH && c.b.w() >= 6)
			big.push_back(c);
	return readDigits(Screen::Result, digitRun(big));
}

Reading readRankPoint(const uint8_t *rgb)
{
	// "매치 순위: 1위" — 쌍점(:) 뒤의 숫자
	const Region r = region(0.355, 0.285, 0.47, 0.335);
	const auto cs = components(whiteMask(rgb, r), r.w, r.h, 4);
	Reading none;
	none.screen = Screen::RankPoint;
	if (cs.size() < 4)
		return none;
	int H = 0;
	for (const Comp &c : cs)
		H = std::max(H, c.b.h());
	if (H < 12 || H > 45)
		return none;
	auto small = [&](const Comp &c) { return c.b.h() <= 0.3 * H && c.b.w() <= 0.3 * H; };
	int colon = -1;
	for (const Comp &a : cs)
		for (const Comp &b : cs)
			if (&a != &b && small(a) && small(b) && std::abs(a.b.x0 - b.b.x0) <= 3 && b.b.y0 > a.b.y1)
				colon = std::max({colon, a.b.x1, b.b.x1});
	if (colon < 0)
		return none;
	std::vector<Comp> after;
	for (const Comp &c : cs)
		if (c.b.x0 > colon && !small(c))
			after.push_back(c);
	return readDigits(Screen::RankPoint, digitRun(after));
}

Reading readScoreboard(const uint8_t *rgb)
{
	Reading none;
	none.screen = Screen::Scoreboard;
	// 위쪽 파란 [순위표] 탭이 있어야 함
	const Region tab = region(0.30, 0.035, 0.46, 0.075);
	int cols = 0;
	for (int x = 0; x < tab.w; ++x) {
		int n = 0;
		for (int y = 0; y < tab.h; ++y)
			n += tabBlue(rgb + (size_t(tab.y0 + y) * kFullW + size_t(tab.x0 + x)) * 3);
		cols += n >= 0.6 * tab.h;
	}
	if (cols < 0.07 * kFullW)
		return none;
	// 왼쪽 위 "5위"
	const Region r = region(0.03, 0.12, 0.17, 0.22);
	auto cs = components(whiteMask(rgb, r), r.w, r.h, 8);
	std::vector<Comp> text;
	for (Comp &c : cs)
		if (c.b.h() >= 8)
			text.push_back(std::move(c));
	return readDigits(Screen::Scoreboard, digitRun(text));
}

Reading readRank(const uint8_t *rgb)
{
	Reading best;
	for (Reading r : {readScoreboard(rgb), readRankPoint(rgb), readResultScreen(rgb)}) {
		if (!r.ok())
			continue;
		if (!best.ok() || (best.weak && !r.weak) || (best.weak == r.weak && r.distance < best.distance))
			best = r;
	}
	return best;
}

// ───────────────────────── 숫자 알아보기 ─────────────────────────

int classifyDigit(const std::vector<uint8_t> &mask, int w, int h, double *distance, double *margin)
{
	if (w <= 0 || h <= 2)
		return -1;
	// 구멍 수: 테두리와 이어지지 않은 빈 칸 덩어리
	const int pw = w + 2, ph = h + 2;
	std::vector<uint8_t> bg(size_t(pw * ph), 0);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x)
			bg[size_t((y + 1) * pw + x + 1)] = 1 + mask[size_t(y * w + x)]; // 1 빈칸, 2 글자
	std::vector<int> lab(size_t(pw * ph), 0), st;
	int holes = 0, next = 0;
	const int minHole = std::max(2, int(0.01 * w * h));
	for (int i = 0; i < pw * ph; ++i) {
		if (bg[size_t(i)] == 2 || lab[size_t(i)])
			continue;
		++next;
		st.assign(1, i);
		lab[size_t(i)] = next;
		int n = 0;
		bool border = false;
		while (!st.empty()) {
			const int p = st.back();
			st.pop_back();
			++n;
			const int x = p % pw, y = p / pw;
			if (x == 0 || y == 0 || x == pw - 1 || y == ph - 1)
				border = true;
			const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
			for (const auto &d : nb) {
				const int nx = x + d[0], ny = y + d[1];
				if (nx < 0 || ny < 0 || nx >= pw || ny >= ph)
					continue;
				const int q = ny * pw + nx;
				if (bg[size_t(q)] != 2 && !lab[size_t(q)]) {
					lab[size_t(q)] = next;
					st.push_back(q);
				}
			}
		}
		if (!border && n >= minHole)
			++holes;
	}
	// 10x16 칸별 채움 정도 (칸 경계에 걸친 픽셀은 걸친 만큼만)
	double grid[kGW * kGH];
	for (int gy = 0; gy < kGH; ++gy) {
		const double y0 = gy * double(h) / kGH, y1 = (gy + 1) * double(h) / kGH;
		for (int gx = 0; gx < kGW; ++gx) {
			const double x0 = gx * double(w) / kGW, x1 = (gx + 1) * double(w) / kGW;
			double tot = 0, area = 0;
			for (int y = int(y0); y < int(std::ceil(y1)); ++y) {
				const double fy = std::min(y1, y + 1.0) - std::max(y0, double(y));
				if (fy <= 0)
					continue;
				for (int x = int(x0); x < int(std::ceil(x1)); ++x) {
					const double fx = std::min(x1, x + 1.0) - std::max(x0, double(x));
					if (fx <= 0)
						continue;
					tot += mask[size_t(y * w + x)] * fx * fy;
					area += fx * fy;
				}
			}
			grid[gy * kGW + gx] = area > 0 ? tot / area : 0;
		}
	}
	const double aspect = double(w) / h;
	double best[10];
	std::fill(best, best + 10, 1e9);
	for (const DigitTemplate &t : kDigitTemplates) {
		double d = 0;
		for (int i = 0; i < kGW * kGH; ++i) {
			const double v = grid[i] - t.grid[i] / 255.0;
			d += v * v;
		}
		d = d / (kGW * kGH) * 10.0;
		const double la = std::log(aspect / t.aspect);
		d += 6.0 * la * la;
		if (holes != t.holes)
			d += 4.0;
		best[t.digit] = std::min(best[t.digit], d);
	}
	int d1 = 0;
	for (int i = 1; i < 10; ++i)
		if (best[i] < best[d1])
			d1 = i;
	double second = 1e9;
	for (int i = 0; i < 10; ++i)
		if (i != d1)
			second = std::min(second, best[i]);
	if (distance)
		*distance = best[d1];
	if (margin)
		*margin = second - best[d1];
	return d1;
}

} // namespace rank
