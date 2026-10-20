#include "EditTimeline.h"

#include "Thumbs.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
const QColor kClip[2] = {QColor("#D9822B"), QColor("#B8681E")};
}

EditTimeline::EditTimeline(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setFixedHeight(112);
}

void EditTimeline::setData(QVector<Segment> *segs, const QVector<KillEvent> *events, double duration)
{
	m_segs = segs;
	m_events = events;
	m_duration = duration;
	refresh();
}

void EditTimeline::refresh()
{
	if (m_sel >= 0 && (!m_segs || m_sel >= m_segs->size() || !(*m_segs)[m_sel].enabled))
		m_sel = -1;
	clampView();
	update();
}

void EditTimeline::setSelected(int segIndex)
{
	m_sel = segIndex;
	update();
}

void EditTimeline::setSourcePosition(double t)
{
	m_srcPos = t;
	// 확대했을 때 재생 위치가 화면 밖으로 나가면 따라감
	if (m_zoom > 1.0 && m_drag == Drag::None) {
		const double r = sourceToResult(t), vis = visibleDuration();
		if (r < m_viewStart || r > m_viewStart + vis * 0.92) {
			m_viewStart = r - vis * 0.1;
			clampView();
		}
	}
	update();
}

QVector<int> EditTimeline::order() const
{
	QVector<int> o;
	if (!m_segs)
		return o;
	for (int i = 0; i < m_segs->size(); ++i)
		if ((*m_segs)[i].enabled)
			o.push_back(i);
	return o;
}

double EditTimeline::offsetOf(int segIndex) const
{
	double off = 0;
	for (int i : order()) {
		if (i == segIndex)
			return off;
		off += (*m_segs)[i].length();
	}
	return off;
}

double EditTimeline::resultLength() const
{
	double off = 0;
	for (int i : order())
		off += (*m_segs)[i].length();
	return off;
}

double EditTimeline::resultToSource(double r) const
{
	double off = 0;
	const QVector<int> o = order();
	for (int i : o) {
		const Segment &s = (*m_segs)[i];
		if (r < off + s.length())
			return s.start + std::max(0.0, r - off);
		off += s.length();
	}
	return o.isEmpty() ? 0.0 : (*m_segs)[o.back()].end;
}

double EditTimeline::sourceToResult(double t) const
{
	double off = 0;
	for (int i : order()) {
		const Segment &s = (*m_segs)[i];
		if (t < s.start)
			return off;
		if (t <= s.end)
			return off + (t - s.start);
		off += s.length();
	}
	return off;
}

int EditTimeline::segmentAtSource(double t) const
{
	for (int i : order()) {
		const Segment &s = (*m_segs)[i];
		if (t >= s.start && t < s.end)
			return i;
	}
	return -1;
}

double EditTimeline::minStart(int idx) const
{
	double lo = 0;
	for (int i = 0; i < m_segs->size(); ++i)
		if (i != idx && (*m_segs)[i].end <= (*m_segs)[idx].start + 1e-6)
			lo = std::max(lo, (*m_segs)[i].end);
	return lo;
}

double EditTimeline::maxEnd(int idx) const
{
	double hi = m_duration > 0 ? m_duration : 1e9;
	for (int i = 0; i < m_segs->size(); ++i)
		if (i != idx && (*m_segs)[i].start >= (*m_segs)[idx].end - 1e-6)
			hi = std::min(hi, (*m_segs)[i].start);
	return hi;
}

// ── 화면 좌표 ──

QRect EditTimeline::rulerRect() const { return {kMargin, 4, width() - 2 * kMargin, 18}; }
QRect EditTimeline::videoRect() const { return {kMargin, 26, width() - 2 * kMargin, 78}; }

double EditTimeline::fitDuration() const
{
	if (m_fixedFit > 0)
		return m_fixedFit;
	return std::max(5.0, resultLength() * 1.04);
}

void EditTimeline::clampView()
{
	m_viewStart = std::clamp(m_viewStart, 0.0, std::max(0.0, fitDuration() - visibleDuration()));
}

double EditTimeline::xToTime(double x) const
{
	const QRect r = videoRect();
	return std::max(0.0, m_viewStart + (x - r.left()) / r.width() * visibleDuration());
}

double EditTimeline::timeToX(double t) const
{
	const QRect r = videoRect();
	return r.left() + (t - m_viewStart) / visibleDuration() * r.width();
}

void EditTimeline::setZoom(double zoom, double anchor)
{
	zoom = std::clamp(zoom, 1.0, 200.0);
	if (anchor < 0)
		anchor = sourceToResult(m_srcPos);
	const double frac = (anchor - m_viewStart) / visibleDuration();
	m_zoom = zoom;
	m_viewStart = anchor - std::clamp(frac, 0.0, 1.0) * visibleDuration();
	clampView();
	emit zoomChanged(m_zoom);
	update();
}

void EditTimeline::wheelEvent(QWheelEvent *e)
{
	const double steps = e->angleDelta().y() / 120.0;
	if (e->modifiers() & Qt::ControlModifier) {
		setZoom(m_zoom * std::pow(1.25, steps), xToTime(e->position().x()));
	} else if (m_zoom > 1.0) {
		const double dx = (e->angleDelta().x() != 0 ? e->angleDelta().x() : e->angleDelta().y()) / 120.0;
		m_viewStart -= dx * visibleDuration() * 0.15;
		clampView();
		update();
	} else {
		e->ignore();
		return;
	}
	e->accept();
}

bool EditTimeline::nearEdge(double x, bool *isIn) const
{
	if (!m_segs || m_sel < 0 || m_sel >= m_segs->size() || !(*m_segs)[m_sel].enabled)
		return false;
	const double s = offsetOf(m_sel), e = s + (*m_segs)[m_sel].length();
	if (std::abs(x - timeToX(e)) <= 7) {
		*isIn = false;
		return true;
	}
	if (std::abs(x - timeToX(s)) <= 7) {
		*isIn = true;
		return true;
	}
	return false;
}

// ── 그리기 ──

void EditTimeline::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(rect(), QColor("#18181C"));
	const QVector<int> o = order();
	if (!m_segs || o.isEmpty()) {
		p.setPen(QColor("#888"));
		p.drawText(rect(), Qt::AlignCenter, "편집 타임라인 — 킬을 찾으면 결과 영상 순서대로 클립이 나타나요");
		return;
	}
	QFont small = font();
	small.setPixelSize(10);
	QFont label = font();
	label.setPixelSize(11);
	label.setBold(true);
	p.setClipRect(QRect(kMargin - 8, 0, width() - 2 * kMargin + 16, height()));
	const double viewEnd = m_viewStart + visibleDuration();

	// 눈금 (결과 영상 시간)
	const QRect ruler = rulerRect();
	const double pps = ruler.width() / visibleDuration();
	double step = 600.0;
	for (double c : {0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0})
		if (c * pps >= 60) {
			step = c;
			break;
		}
	p.setFont(small);
	for (double t = std::floor(m_viewStart / step) * step; t <= viewEnd; t += step) {
		if (t < -1e-9)
			continue;
		const double x = timeToX(t);
		p.setPen(QColor("#555"));
		p.drawLine(QPointF(x, ruler.bottom() - 4), QPointF(x, ruler.bottom()));
		p.setPen(QColor("#9A9AA5"));
		const int m = int(t + 1e-6) / 60;
		const double sec = t - m * 60;
		p.drawText(QPointF(x + 3, ruler.bottom() - 5),
			   step < 1.0 ? QString("%1:%2").arg(m).arg(sec, 4, 'f', 1, QChar('0'))
				      : QString("%1:%2").arg(m).arg(int(sec + 1e-6), 2, 10, QChar('0')));
	}

	const QRect vr = videoRect();
	p.fillRect(vr, QColor("#202026"));
	const bool trimmingIn = m_drag == Drag::TrimIn && m_sel >= 0;
	if (trimmingIn && m_trimShift > 1e-4) { // 잘려 나가는 부분
		const double os = offsetOf(m_sel);
		QRectF cut(timeToX(os), vr.top() + 1, timeToX(os + m_trimShift) - timeToX(os), vr.height() - 2);
		p.setPen(Qt::NoPen);
		p.setBrush(QBrush(QColor(229, 72, 77, 150), Qt::BDiagPattern));
		p.drawRoundedRect(cut, 4, 4);
	}

	double off = 0;
	for (int n = 0; n < o.size(); ++n) {
		const int i = o[n];
		const Segment &s = (*m_segs)[i];
		const bool shifted = trimmingIn && off >= offsetOf(m_sel) - 1e-9;
		const double st = off + (shifted ? m_trimShift : 0.0);
		off += s.length();
		QRectF r(timeToX(st), vr.top(), timeToX(st + s.length()) - timeToX(st), vr.height());
		r.adjust(1, 1, -1, -1);
		if (r.width() < 1 || r.right() < 0 || r.left() > width())
			continue;
		const QColor c = kClip[n % 2];
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawRoundedRect(r, 4, 4);

		// 장면 썸네일
		if (m_thumbs) {
			const QRectF inner = r.adjusted(2, 16, -2, -2);
			const double tileW = inner.height() * 16.0 / 9.0;
			p.save();
			QPainterPath clip;
			clip.addRoundedRect(inner, 3, 3);
			p.setClipPath(clip, Qt::IntersectClip);
			const double x0 = std::max(inner.left(), -tileW);
			const double firstX = inner.left() + std::floor((x0 - inner.left()) / tileW) * tileW;
			for (double x = firstX; x < std::min(inner.right(), double(width())); x += tileW) {
				const double local = std::clamp(xToTime(x + tileW / 2) - st, 0.0, s.length());
				const QImage img = m_thumbs->frameAt(s.start + local);
				if (!img.isNull())
					p.drawImage(QRectF(x, inner.top(), tileW, inner.height()), img);
			}
			p.restore();
		}

		// 킬 표시 (빨강 킬, 파랑 어시)
		if (m_events)
			for (const KillEvent &e : *m_events) {
				if (!e.counts() || e.time < s.start || e.time > s.end)
					continue;
				const double x = timeToX(st + e.time - s.start);
				p.setPen(QPen(e.kill ? QColor(255, 70, 70) : QColor(90, 200, 240), 2));
				p.drawLine(QPointF(x, r.top() + 14), QPointF(x, r.bottom()));
			}

		// 이름표
		QStringList what;
		what << QString("#%1").arg(n + 1);
		if (s.kills > 0)
			what << QString("킬 %1").arg(s.kills);
		if (s.assists > 0)
			what << QString("어시 %1").arg(s.assists);
		p.setFont(label);
		p.setPen(Qt::white);
		p.drawText(r.adjusted(5, 1, -3, 0), Qt::AlignTop | Qt::AlignLeft,
			   p.fontMetrics().elidedText(what.join(" · "), Qt::ElideRight, int(r.width()) - 8));

		if (i == m_sel) {
			p.setPen(QPen(Qt::white, 2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
			p.setPen(Qt::NoPen);
			p.setBrush(Qt::white);
			p.drawRect(QRectF(r.left(), r.top() + 16, 4, r.height() - 28));
			p.drawRect(QRectF(r.right() - 4, r.top() + 16, 4, r.height() - 28));
		}
	}

	// 재생 위치 (클립 밖이면 흐리게)
	const bool inside = segmentAtSource(m_srcPos) >= 0;
	double rp = sourceToResult(m_srcPos);
	if (trimmingIn && rp >= offsetOf(m_sel) - 1e-6)
		rp += m_trimShift;
	const double px = timeToX(rp);
	const QColor pc = inside ? QColor("#FF4D4F") : QColor(255, 77, 79, 90);
	p.setPen(QPen(pc, 2));
	p.drawLine(QPointF(px, ruler.top()), QPointF(px, vr.bottom()));
	QPainterPath tri;
	tri.moveTo(px - 6, ruler.top());
	tri.lineTo(px + 6, ruler.top());
	tri.lineTo(px, ruler.top() + 8);
	tri.closeSubpath();
	p.fillPath(tri, pc);
}

// ── 마우스 ──

void EditTimeline::mousePressEvent(QMouseEvent *e)
{
	if (!m_segs || e->button() != Qt::LeftButton || order().isEmpty())
		return;
	const QPointF pos = e->position();
	const double t = xToTime(pos.x());
	m_fixedFit = fitDuration();
	if (videoRect().contains(pos.toPoint())) {
		bool isIn = false;
		if (nearEdge(pos.x(), &isIn)) {
			emit editStarted();
			m_drag = isIn ? Drag::TrimIn : Drag::TrimOut;
			m_pressT = t;
			m_origStart = (*m_segs)[m_sel].start;
			m_origEnd = (*m_segs)[m_sel].end;
			m_trimShift = 0;
			return;
		}
		const double src = resultToSource(t);
		const int i = segmentAtSource(src);
		if (i >= 0 && i != m_sel) {
			m_sel = i;
			emit segmentSelected(i);
		}
	}
	m_drag = Drag::Playhead;
	emit seekRequested(resultToSource(t));
	update();
}

void EditTimeline::mouseMoveEvent(QMouseEvent *e)
{
	if (!m_segs)
		return;
	const QPointF pos = e->position();
	if (m_drag == Drag::None) {
		bool isIn = false;
		setCursor(videoRect().contains(pos.toPoint()) && nearEdge(pos.x(), &isIn) ? Qt::SizeHorCursor
											     : Qt::ArrowCursor);
		return;
	}
	const double t = xToTime(pos.x());
	switch (m_drag) {
	case Drag::Playhead:
		emit seekRequested(resultToSource(std::min(t, resultLength() - 0.01)));
		break;
	case Drag::TrimIn: {
		Segment &s = (*m_segs)[m_sel];
		s.start = std::clamp(m_origStart + (t - m_pressT), minStart(m_sel), s.end - 0.5);
		m_trimShift = s.start - m_origStart; // 뒤쪽 끝은 제자리에 보이게
		emit seekRequested(s.start);
		update();
		break;
	}
	case Drag::TrimOut: {
		Segment &s = (*m_segs)[m_sel];
		s.end = std::clamp(m_origEnd + (t - m_pressT), s.start + 0.5, maxEnd(m_sel));
		emit seekRequested(std::max(s.start, s.end - 0.05));
		update();
		break;
	}
	default:
		break;
	}
}

void EditTimeline::mouseReleaseEvent(QMouseEvent *)
{
	const bool trimmed = m_drag == Drag::TrimIn || m_drag == Drag::TrimOut;
	m_drag = Drag::None;
	m_trimShift = 0;
	m_fixedFit = 0;
	clampView();
	setCursor(Qt::ArrowCursor);
	if (trimmed)
		emit segmentsEdited();
	update();
}
