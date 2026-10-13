#pragma once

#include "Subtitles.h"

#include <QVideoFrame>
#include <QWidget>

class QVideoSink;

// 미리보기 화면: 영상을 16:9 결과 화면처럼(검은 여백 포함) 그리고, 그 위에 자막을 결과와 같은 모양으로 그림
// 고른 자막은 마우스로 위아래로 끌어 위치를 바꿀 수 있음
class VideoView : public QWidget {
	Q_OBJECT
public:
	explicit VideoView(QWidget *parent = nullptr);
	QVideoSink *videoSink() const { return m_sink; }
	void setSubtitles(const QVector<Subtitle> &subs, int selected);
	void setTime(double t);

signals:
	void subtitleClicked(int index);
	void subtitleMoved(int index, double posY); // 끄는 중
	void subtitleMoveFinished(int index);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	QRectF canvasRect() const;

	QVideoSink *m_sink;
	QImage m_image;
	QVector<Subtitle> m_subs;
	QVector<QRectF> m_rects; // 마지막으로 그린 자막 자리
	int m_selected = -1;
	double m_time = 0;
	int m_drag = -1;
	double m_grabDy = 0;
};
