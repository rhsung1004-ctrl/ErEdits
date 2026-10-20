#pragma once

#include "Exporter.h"
#include "Games.h"
#include "KillScanner.h"
#include "RankReader.h"
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
class QSpinBox;
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

	// 판 등수
	QWidget *buildRankTab();
	QRectF gameRect() const;
	GameFilter gameFilter() const;
	void startRankRead();
	void rebuildGames(bool applyToClips = true);
	void refreshGameTable();
	void gameFilterChanged();
	void saveGameLog();

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

	// 판 등수 결과
	QVector<ResultCandidate> m_cands;  // 결과 화면 후보 (분석 중 찾음)
	QVector<RankReading> m_readings;   // 후보에서 읽은 등수
	QVector<GameResult> m_games;       // 판 목록 (직접 고친 값 포함)
	bool m_readsDone = false;          // 후보를 모두 읽었는지

	KillScanner *m_scanner;
	RankReader *m_rankReader;
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

	QCheckBox *m_rankFilter;
	QSpinBox *m_maxRank;
	QCheckBox *m_keepUnknown;
	QComboBox *m_gameArea;
	QTableWidget *m_gameTable;
	QLabel *m_rankStatus;
	QPushButton *m_btnReadRanks;
	QPushButton *m_btnResetGames;
	QPushButton *m_btnSaveLog;

	bool m_fillingTable = false;
	bool m_fillingGames = false;
};
