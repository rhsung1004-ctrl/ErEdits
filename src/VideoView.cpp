#include "VideoView.h"

#include "ComboColors.h"

#include <QMouseEvent>
#include <QPainter>
#include <QVideoSink>

#include <algorithm>

VideoView::VideoView(QWidget *parent) : QWidget(parent), m_sink(new QVideoSink(this))
{
	setMinimumSize(480, 270);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	connect(ComboColors::notifier(), &ComboColors::Notifier::changed, this, qOverload<>(&QWidget::update));
	connect(m_sink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
		if (!f.isValid())
			return;
		m_image = f.toImage();
		update();
	});
}

void VideoView::setSubtitles(const QVector<Subtitle> &subs, int selected)
{
	m_subs = subs;
	m_selected = selected;
	update();
}

void VideoView::setTime(double t)
{
	m_time = t;
	update();
}

QRectF VideoView::canvasRect() const
{
	const QSizeF s = QSizeF(16, 9).scaled(QSizeF(size()), Qt::KeepAspectRatio);
	return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

void VideoView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(16, 16, 18));
	const QRectF c = canvasRect();
	p.fillRect(c, Qt::black);
	if (!m_image.isNull()) {
		// 내보내기와 같게: 비율을 지켜 16:9 화면 가운데에
		const QSizeF s = QSizeF(m_image.size()).scaled(c.size(), Qt::KeepAspectRatio);
		const QRectF r(c.center().x() - s.width() / 2, c.center().y() - s.height() / 2, s.width(), s.height());
		p.setRenderHint(QPainter::SmoothPixmapTransform);
		p.drawImage(r, m_image);
	} else {
		p.setPen(QColor(120, 120, 120));
		p.drawText(c, Qt::AlignCenter, "방송 다시보기를 열어 주세요");
	}

	m_rects.fill(QRectF(), m_subs.size());
	p.setClipRect(c);
	for (int i = 0; i < m_subs.size(); ++i) {
		double alpha = subtitleAlphaAt(m_subs[i], m_time);
		// 고른 자막은 지금 시간에 안 보여도 흐리게 보여 줌 (위치 맞추기용)
		if (i == m_selected && alpha <= 0)
			alpha = 0.35;
		if (alpha <= 0)
			continue;
		m_rects[i] = paintSubtitle(p, c, m_subs[i], alpha);
	}
	if (m_selected >= 0 && m_selected < m_rects.size() && !m_rects[m_selected].isNull()) {
		QPen pen(QColor(255, 196, 0), 1, Qt::DashLine);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		p.drawRect(m_rects[m_selected].adjusted(-6, -4, 6, 4));
	}
}

void VideoView::mousePressEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton)
		return;
	// 고른 자막을 먼저, 그다음 보이는 자막
	QVector<int> order;
	if (m_selected >= 0)
		order << m_selected;
	for (int i = 0; i < m_rects.size(); ++i)
		if (i != m_selected)
			order << i;
	for (int i : order) {
		if (i < m_rects.size() && m_rects[i].adjusted(-8, -6, 8, 6).contains(e->position())) {
			if (i != m_selected)
				emit subtitleClicked(i);
			m_drag = i;
			const QRectF c = canvasRect();
			m_grabDy = (c.top() + m_subs[i].posY * c.height()) - e->position().y();
			setCursor(Qt::SizeVerCursor);
			return;
		}
	}
}

void VideoView::mouseMoveEvent(QMouseEvent *e)
{
	if (m_drag < 0 || m_drag >= m_subs.size())
		return;
	const QRectF c = canvasRect();
	const double y = std::clamp((e->position().y() + m_grabDy - c.top()) / c.height(), 0.05, 1.0);
	m_subs[m_drag].posY = y;
	update();
	emit subtitleMoved(m_drag, y);
}

void VideoView::mouseReleaseEvent(QMouseEvent *)
{
	if (m_drag >= 0)
		emit subtitleMoveFinished(m_drag);
	m_drag = -1;
	unsetCursor();
}
