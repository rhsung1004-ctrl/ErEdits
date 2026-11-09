#pragma once

#include "Segments.h"

#include <QWidget>

class Thumbs;

// 결과 영상 기준 편집 타임라인 (ERShorts 편집 화면과 같은 조작)
//  - 사용하는 클립만 이어 붙여 결과 영상 순서대로 보여 줌 (장면 썸네일 + 킬 표시)
//  - 클릭: 그 위치로 이동 / 클립 선택,  선택한 클립의 양 끝 끌기: 길이 조절 (바깥으로 끌면 원본에서 늘어남)
//  - Ctrl+휠: 확대/축소 (마우스 위치 기준), 휠: 확대했을 때 좌우 이동
//  - 자르기(S)·삭제(Delete)·되돌리기(Ctrl+Z)는 메인 창 단축키
class EditTimeline : public QWidget {
	Q_OBJECT
public:
	explicit EditTimeline(QWidget *parent = nullptr);

	// segs: 메인 창의 클립 목록 (끄기 된 클립은 안 보임), events: 킬 표시용
	void setData(QVector<Segment> *segs, const QVector<KillEvent> *events, double duration);
	void setThumbs(Thumbs *t) { m_thumbs = t; }
	void refresh(); // 클립 목록이 바뀐 뒤
	void setSourcePosition(double t);
	void setSelected(int segIndex);
	void setZoom(double zoom, double anchor = -1);
	void zoomToFit() { setZoom(1.0); }

	// 결과 시간 ↔ 원본 시간
	double resultLength() const;
	double resultToSource(double r) const;
	double sourceToResult(double t) const; // 클립 밖이면 가장 가까운 클립 경계
	int segmentAtSource(double t) const;   // t 가 들어 있는 사용 클립 (m_segs 번호, 없으면 -1)

	QSize sizeHint() const override { return {800, 112}; }
	QSize minimumSizeHint() const override { return {300, 112}; }

signals:
	void seekRequested(double sourceTime);
	void segmentSelected(int segIndex);
	void editStarted();    // 끌기로 클립을 바꾸기 직전 (되돌리기 저장)
	void segmentsEdited(); // 끌기가 끝남
	void zoomChanged(double zoom);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

private:
	enum class Drag { None, Playhead, TrimIn, TrimOut };

	QVector<int> order() const;     // 사용 클립의 m_segs 번호 (결과 순서)
	double offsetOf(int segIndex) const; // 결과 영상에서 클립이 시작하는 시각
	QRect rulerRect() const;
	QRect videoRect() const;
	double xToTime(double x) const;
	double timeToX(double t) const;
	double fitDuration() const;
	double visibleDuration() const { return fitDuration() / m_zoom; }
	void clampView();
	bool nearEdge(double x, bool *isIn) const;
	// 원본에서 끌 수 있는 범위 (앞뒤 클립과 겹치지 않게)
	double minStart(int segIndex) const;
	double maxEnd(int segIndex) const;

	QVector<Segment> *m_segs = nullptr;
	const QVector<KillEvent> *m_events = nullptr;
	double m_duration = 0;
	Thumbs *m_thumbs = nullptr;
	double m_srcPos = 0;
	int m_sel = -1;

	Drag m_drag = Drag::None;
	double m_pressT = 0;
	double m_origStart = 0, m_origEnd = 0;
	double m_trimShift = 0; // 앞부분을 자르는 중: 클립 뒤쪽 끝을 제자리에 두려고 미는 양
	double m_fixedFit = 0;  // 끄는 동안 배율 고정
	double m_zoom = 1.0;
	double m_viewStart = 0;
	static constexpr int kMargin = 12;
};
