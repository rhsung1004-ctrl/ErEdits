#pragma once

#include "Exporter.h"
#include "Games.h"
#include "KillScanner.h"
#include "KillVerifier.h"
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
class EditTimeline;
class AiPanel;
class JoinedPlayer;
class Thumbs;
class TimelineBar;
class VodList;
class QTimer;
struct ProjectData;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow() override;
	void openVideo(const QString &path);
	void openFiles(QStringList files); // 하나면 그대로, 여러 개면 순서를 정해 묶어서 (.erkc 면 프로젝트)
	// 프로젝트 (.erkc): 방송 목록 + 방송마다 분석·편집 결과
	void openProject(const QString &file, bool interactive = true);
	bool saveProject(bool saveAs);
	void newProject();
	// 시작할 때: 마지막 프로젝트, 없으면 마지막 작업 기록을 다시 엶
	void restoreLastSession();

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
	void startVerify();
	// 여러 방송: 분석이 (등수 읽기까지) 끝났을 때 → 모두 킬 찾기 중이면 다음 방송
	void analysisDone();
	void batchNext();
	void stopBatch(const QString &why);
	void addVods();
	void switchVod(const QString &path);
	void exportCombined();
	ExportSettings exportSettings() const;
	bool anyBusy() const;
	struct ProjectData collectProject() const;
	void writeAutoSave();
	void updateProjectLabel(const QString &note = QString());
	// 이 영상의 저장된 작업이 없을 때: 같은 이름 영상으로 하던 작업을 찾아 불러올지 물음 (영상을 옮기거나 다시 받은 경우)
	bool tryRecoverCache(const QString &path, double duration);
	static QString sessionFile();
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

	// 컷 편집
	void pushUndo();
	void undo();
	void redo();
	void recountSegments(); // 클립마다 안에 든 킬·어시 수 다시 세기
	void splitAtPlayhead();
	void requestThumbs();
	void setFunClips(const QVector<int> &indices, bool add);

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
	KillVerifier *m_verifier;
	VodList *m_vodList = nullptr;
	QString m_projectPath;     // 비어 있으면 아직 저장 안 한 프로젝트
	QLabel *m_projectLabel = nullptr;
	QTimer *m_autoSave = nullptr;
	bool m_restoring = false;  // 프로젝트를 여는 중 (자동 저장·이전 작업 찾기 안 함)
	bool m_batch = false;      // 모두 킬 찾기 중
	bool m_noAsk = false;      // 다시 분석 확인을 묻지 않음 (모두 킬 찾기)
	QStringList m_batchTried;
	Thumbs *m_thumbs;
	AiPanel *m_aiPanel = nullptr;
	QVector<QVector<Segment>> m_undo, m_redo;
	Exporter *m_exporter;

	// 화면
	JoinedPlayer *m_player;
	QAudioOutput *m_audio;
	VideoView *m_view;
	SubtitlePanel *m_subPanel;
	TimelineBar *m_timeline;
	EditTimeline *m_editTl;
	QCheckBox *m_resultOnly;
	QPushButton *m_btnUndo;
	QPushButton *m_btnRedo;
	QPushButton *m_btnPlay;
	QLabel *m_posLabel;
	QLabel *m_fileLabel;

	QLabel *m_roiLabel;
	QComboBox *m_scanMode;
	QPushButton *m_btnScan;
	QPushButton *m_btnVerify;
	QPushButton *m_btnRecord;
	QCheckBox *m_dropUnchanged;
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
	QCheckBox *m_loudnorm;
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
