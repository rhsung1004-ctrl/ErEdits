#pragma once

#include "Exporter.h"
#include "KillScanner.h"
#include "Segments.h"
#include "Util.h"

#include <QMainWindow>
#include <QRectF>

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QMediaPlayer;
class QProgressBar;
class QPushButton;
class QTableWidget;
class SubtitlePanel;
class VideoView;
class TimelineBar;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;
	void openVideo(const QString &path);

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dropEvent(QDropEvent *e) override;
	void closeEvent(QCloseEvent *e) override;

private:
	void buildUi();
	void chooseVideo();
	void chooseRoi();
	void startScan();
	void onScanFinished(bool ok, const QString &msg);
	void rebuildSegments(bool force);
	void refreshTable();
	void refreshSummary();
	void selectSegment(int index);
	int currentSegment() const;
	void adjustSelected(bool startSide, double delta, bool toPlayhead);
	void addSegmentAtPlayhead();
	void deleteSelected();
	void playSegment(int index);
	void startExport();
	void updateRoiLabel();
	void setBusy(bool scanning, bool exporting);
	void subtitlesChanged();
	QVector<Segment> enabledSegments() const;
	SegmentParams params() const;

	// 분석 결과를 영상별로 저장 (몇 시간짜리를 다시 분석하지 않게)
	QString cachePath() const;
	void saveCache();
	bool loadCache();

	// 영상
	QString m_path;
	VideoInfo m_info;
	QRectF m_roi;
	QString m_roiPreset;

	// 결과
	QVector<KillEvent> m_events;
	QVector<Segment> m_segs;
	bool m_edited = false;     // 구간을 직접 고쳤는지
	double m_scannedUntil = 0; // 분석이 끝난 위치
	bool m_scanComplete = false;
	double m_playEnd = -1;     // 구간 미리보기 끝

	KillScanner *m_scanner;
	Exporter *m_exporter;

	// 화면
	QMediaPlayer *m_player;
	QAudioOutput *m_audio;
	VideoView *m_view;
	SubtitlePanel *m_subPanel;
	TimelineBar *m_timeline;
	QPushButton *m_btnPlay;
	QLabel *m_posLabel;
	QLabel *m_fileLabel;

	QLabel *m_roiLabel;
	QComboBox *m_scanMode;
	QPushButton *m_btnScan;
	QProgressBar *m_scanProgress;
	QLabel *m_scanStatus;

	QDoubleSpinBox *m_before, *m_after, *m_gap;
	QCheckBox *m_assist;
	QPushButton *m_btnRebuild;
	QTableWidget *m_table;
	QLabel *m_summary;

	QDoubleSpinBox *m_xfade, *m_fadeIn, *m_fadeOut;
	QComboBox *m_res, *m_fps;
	QCheckBox *m_cpuOnly;
	QPushButton *m_btnExport;
	QProgressBar *m_exportProgress;
	QLabel *m_exportStatus;

	bool m_fillingTable = false;
};
