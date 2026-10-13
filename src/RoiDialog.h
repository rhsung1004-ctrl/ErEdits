#pragma once

#include <QDialog>
#include <QImage>
#include <QProcess>
#include <QRectF>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QSlider;

// 영상 한 장면 위에서 킬 숫자 영역(TK·K·A 줄)을 끌어서 지정하는 화면
class FrameView : public QWidget {
	Q_OBJECT
public:
	explicit FrameView(QWidget *parent = nullptr);
	void setImage(const QImage &img);
	const QImage &image() const { return m_img; }
	void setRoi(const QRectF &roi); // 0~1 비율
	QRectF roi() const { return m_roi; }

signals:
	void roiChanged(const QRectF &roi);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	QRectF imageRect() const;              // 위젯 안에서 영상이 그려진 자리
	QPointF toImage(const QPointF &p) const; // 위젯 좌표 → 영상 픽셀
	QRectF roiPixels() const;
	void setRoiPixels(QRectF r);

	QImage m_img;
	QRectF m_roi;
	enum { None, Draw, Move } m_drag = None;
	QPointF m_anchor;   // 영상 픽셀
	QRectF m_startRoi;  // 영상 픽셀
};

class RoiDialog : public QDialog {
	Q_OBJECT
public:
	RoiDialog(const QString &videoPath, double duration, int videoW, int videoH, const QRectF &roi,
		  const QString &presetName, double startTime, QWidget *parent = nullptr);
	QRectF roi() const;
	QString presetName() const;

	// 저장된 영역 프리셋 (스트리머별)
	static QStringList presetNames();
	static bool loadPreset(const QString &name, QRectF *roi);
	static void savePreset(const QString &name, const QRectF &roi);
	static void deletePreset(const QString &name);

private:
	void requestFrame(double t);
	void updateProbe();

	QString m_path;
	double m_duration;
	int m_vw, m_vh;
	FrameView *m_view;
	QSlider *m_slider;
	QLabel *m_time;
	QLabel *m_probeImg;
	QLabel *m_probeText;
	QComboBox *m_preset;
	QProcess m_grab;
	QByteArray m_grabBuf;
	QTimer m_grabDelay;
	double m_pendingT = -1;
};
