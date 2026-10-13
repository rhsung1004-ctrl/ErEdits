#pragma once

#include "Segments.h"

#include <QWidget>

// 원본 영상 전체 길이 막대: 클립 구간(주황), 킬 표시(빨강), 재생 위치(흰 선), 분석한 범위
// 클릭/끌기로 재생 위치 이동
class TimelineBar : public QWidget {
	Q_OBJECT
public:
	explicit TimelineBar(QWidget *parent = nullptr);
	void setDuration(double d);
	void setPosition(double t);
	void setScanned(double t);
	void setData(const QVector<Segment> &segs, const QVector<KillEvent> &events, bool includeAssist);
	void setSelected(int index);
	QSize sizeHint() const override { return {600, 44}; }

signals:
	void seekRequested(double t);
	void segmentClicked(int index);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;

private:
	double toTime(double x) const;
	double toX(double t) const;

	double m_duration = 0;
	double m_pos = 0;
	double m_scanned = 0;
	int m_selected = -1;
	bool m_assist = false;
	QVector<Segment> m_segs;
	QVector<KillEvent> m_events;
};
