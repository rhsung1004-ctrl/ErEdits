#include "TimelineBar.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

TimelineBar::TimelineBar(QWidget *parent) : QWidget(parent)
{
	setMinimumHeight(40);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
}

void TimelineBar::setDuration(double d)
{
	m_duration = d;
	update();
}

void TimelineBar::setPosition(double t)
{
	m_pos = t;
	update();
}

void TimelineBar::setScanned(double t)
{
	m_scanned = t;
	update();
}

void TimelineBar::setData(const QVector<Segment> &segs, const QVector<KillEvent> &events, bool includeAssist)
{
	m_segs = segs;
	m_events = events;
	m_assist = includeAssist;
	update();
}

void TimelineBar::setSubtitles(const QVector<Subtitle> &subs)
{
	m_subs = subs;
	update();
}

void TimelineBar::setSelected(int index)
{
	m_selected = index;
	update();
}

double TimelineBar::toX(double t) const
{
	if (m_duration <= 0)
		return 4;
	return 4 + (width() - 8) * std::clamp(t / m_duration, 0.0, 1.0);
}

double TimelineBar::toTime(double x) const
{
	if (width() <= 8)
		return 0;
	return std::clamp((x - 4) / (width() - 8), 0.0, 1.0) * m_duration;
}

void TimelineBar::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF bar(4, 10, width() - 8, height() - 20);
	p.fillRect(bar, QColor(45, 45, 52));
	if (m_duration <= 0)
		return;
	// 분석한 범위
	if (m_scanned > 0)
		p.fillRect(QRectF(bar.left(), bar.bottom() - 3, toX(m_scanned) - bar.left(), 3), QColor(80, 140, 230));
	// 클립 구간
	for (int i = 0; i < m_segs.size(); ++i) {
		const Segment &s = m_segs[i];
		const double x0 = toX(s.start), x1 = std::max(toX(s.end), x0 + 2);
		QColor c = s.enabled ? QColor(245, 160, 40) : QColor(110, 110, 110);
		if (i == m_selected)
			c = QColor(255, 220, 90);
		p.fillRect(QRectF(x0, bar.top() + 3, x1 - x0, bar.height() - 7), c);
	}
	// 자막 (막대 아래쪽 보라색)
	for (const Subtitle &sb : m_subs) {
		const double x0 = toX(sb.start), x1 = std::max(toX(sb.end), x0 + 2);
		p.fillRect(QRectF(x0, bar.bottom() + 1, x1 - x0, 4), QColor(170, 110, 240));
	}
	// 킬 표시
	for (const KillEvent &e : m_events) {
		if (!e.counts() || (!e.kill && !(m_assist && e.assist)))
			continue;
		const double x = toX(e.time);
		p.setPen(QPen(e.kill ? QColor(230, 50, 50) : QColor(80, 190, 230), 2));
		p.drawLine(QPointF(x, 2), QPointF(x, 10));
	}
	// 재생 위치
	const double px = toX(m_pos);
	p.setPen(QPen(Qt::white, 2));
	p.drawLine(QPointF(px, 0), QPointF(px, height()));
}

void TimelineBar::mousePressEvent(QMouseEvent *e)
{
	const double t = toTime(e->position().x());
	for (int i = 0; i < m_segs.size(); ++i)
		if (t >= m_segs[i].start && t <= m_segs[i].end) {
			emit segmentClicked(i);
			break;
		}
	emit seekRequested(t);
}

void TimelineBar::mouseMoveEvent(QMouseEvent *e)
{
	if (e->buttons() & Qt::LeftButton)
		emit seekRequested(toTime(e->position().x()));
}
