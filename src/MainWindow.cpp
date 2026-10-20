#include "MainWindow.h"

#include "RoiDialog.h"
#include "SubtitlePanel.h"
#include "TimelineBar.h"
#include "VideoView.h"

#include <QApplication>
#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {
QDoubleSpinBox *spin(double min, double max, double step, double val, const QString &suffix, int decimals = 1)
{
	auto *s = new QDoubleSpinBox;
	s->setRange(min, max);
	s->setSingleStep(step);
	s->setDecimals(decimals);
	s->setValue(val);
	s->setSuffix(suffix);
	return s;
}

QPushButton *smallButton(const QString &text, const QString &tip = {})
{
	auto *b = new QPushButton(text);
	b->setToolTip(tip);
	b->setAutoDefault(false);
	return b;
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
	setWindowTitle("ERKillCut — 이터널리턴 킬 하이라이트 자동 컷 편집");
	setAcceptDrops(true);
	resize(1500, 900);

	m_scanner = new KillScanner(this);
	m_rankReader = new RankReader(this);
	m_exporter = new Exporter(this);
	buildUi();

	connect(m_scanner, &KillScanner::progress, this, [this](double t, double dur) {
		m_scannedUntil = t;
		m_timeline->setScanned(t);
		if (dur > 0)
			m_scanProgress->setValue(int(std::clamp(t / dur, 0.0, 1.0) * 1000));
		m_scanStatus->setText(QString("분석 중… %1 / %2 · 킬 %3번")
					      .arg(formatTime(t, false), formatTime(dur, false))
					      .arg(std::count_if(m_events.begin(), m_events.end(),
								 [](const KillEvent &e) { return e.kill; })));
	});
	connect(m_scanner, &KillScanner::found, this, [this](const KillEvent &e) {
		m_events.push_back(e);
		if (!m_edited)
			rebuildSegments(true);
	});
	connect(m_scanner, &KillScanner::finished, this, &MainWindow::onScanFinished);
	connect(m_scanner, &KillScanner::resultFound, this, [this](const ResultCandidate &c) { m_cands.push_back(c); });

	connect(m_rankReader, &RankReader::progress, this, [this](int done, int total) {
		m_rankStatus->setText(QString("결과 화면에서 등수 읽는 중… %1 / %2").arg(done).arg(total));
	});
	connect(m_rankReader, &RankReader::finished, this, [this](bool, const QVector<RankReading> &rs) {
		m_readings = rs;
		m_readsDone = true;
		setBusy(false, false);
		rebuildGames();
		saveCache();
	});

	connect(m_exporter, &Exporter::progress, this,
		[this](double f) { m_exportProgress->setValue(int(std::lround(f * 1000))); });
	connect(m_exporter, &Exporter::finished, this, [this](bool ok, const QString &msg) {
		setBusy(false, false);
		m_exportStatus->setText(ok ? "<b style='color:#3c3'>✓ " + msg + "</b>" : msg);
		if (ok) {
			const QString out = QSettings().value("lastExport").toString();
			if (QMessageBox::question(this, "내보내기 완료", msg + "\n\n저장한 폴더를 열까요?") ==
			    QMessageBox::Yes) {
#ifdef _WIN32
				QProcess::startDetached("explorer.exe", {"/select," + QDir::toNativeSeparators(out)});
#else
				QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(out).absolutePath()));
#endif
			}
		}
	});

	updateRoiLabel();
	setBusy(false, false);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi()
{
	QSettings st;

	// ── 왼쪽: 미리보기 ──
	m_player = new QMediaPlayer(this);
	m_audio = new QAudioOutput(this);
	m_player->setAudioOutput(m_audio);
	m_view = new VideoView;
	m_view->setMinimumSize(640, 360);
	m_player->setVideoSink(m_view->videoSink());

	auto *btnOpen = new QPushButton("📂 방송 다시보기 열기");
	btnOpen->setStyleSheet("font-weight:bold; padding:6px 14px;");
	m_fileLabel = new QLabel("영상 파일을 열거나 이 창으로 끌어다 놓으세요 (mp4, mkv, ts, flv …)");
	m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

	m_timeline = new TimelineBar;
	m_btnPlay = smallButton("▶", "재생 / 일시정지 (스페이스)");
	m_btnPlay->setFixedWidth(44);
	m_btnPlay->setShortcut(QKeySequence(Qt::Key_Space));
	m_posLabel = new QLabel("0:00 / 0:00");
	m_posLabel->setMinimumWidth(150);
	auto *vol = new QDoubleSpinBox;
	vol->setRange(0, 100);
	vol->setDecimals(0);
	vol->setSuffix("%");
	vol->setValue(st.value("volume", 70).toDouble());
	vol->setToolTip("미리보기 소리 크기");
	m_audio->setVolume(float(vol->value() / 100.0));
	connect(vol, &QDoubleSpinBox::valueChanged, this, [this](double v) {
		m_audio->setVolume(float(v / 100.0));
		QSettings().setValue("volume", v);
	});

	auto *top = new QHBoxLayout;
	top->addWidget(btnOpen);
	top->addWidget(m_fileLabel, 1);

	auto *ctrl = new QHBoxLayout;
	ctrl->addWidget(m_btnPlay);
	ctrl->addWidget(m_posLabel);
	ctrl->addStretch(1);
	ctrl->addWidget(new QLabel("🔊"));
	ctrl->addWidget(vol);

	auto *legend = new QLabel("<span style='color:#f5a028'>■</span> 클립 구간 &nbsp; "
				  "<span style='color:#e63232'>|</span> 킬 &nbsp; "
				  "<span style='color:#50bee6'>|</span> 어시스트 &nbsp; "
				  "<span style='color:#508ce6'>━</span> 분석한 범위");

	auto *left = new QWidget;
	auto *ll = new QVBoxLayout(left);
	ll->addLayout(top);
	ll->addWidget(m_view, 1);
	ll->addWidget(m_timeline);
	ll->addWidget(legend);
	ll->addLayout(ctrl);

	// ── 오른쪽: 1. 킬 찾기 ──
	auto *g1 = new QGroupBox("1. 킬 찾기");
	m_roiLabel = new QLabel;
	m_roiLabel->setWordWrap(true);
	auto *btnRoi = smallButton("영역 지정…", "방송 화면에서 TK·K·A 숫자 줄 위치를 지정");
	m_scanMode = new QComboBox;
	m_scanMode->addItem("정확하게 (초당 5장 검사)", int(KillScanner::Precise));
	m_scanMode->addItem("빠르게 (키프레임만, 킬 시각 ±2초)", int(KillScanner::Fast));
	m_scanMode->setCurrentIndex(st.value("scanMode", 0).toInt());
	m_btnScan = new QPushButton("🔍 킬 찾기 시작");
	m_btnScan->setStyleSheet("font-weight:bold; padding:5px;");
	m_scanProgress = new QProgressBar;
	m_scanProgress->setRange(0, 1000);
	m_scanProgress->setTextVisible(false);
	m_scanProgress->setMaximumHeight(10);
	m_scanStatus = new QLabel;
	m_scanStatus->setWordWrap(true);

	auto *roiRow = new QHBoxLayout;
	roiRow->addWidget(m_roiLabel, 1);
	roiRow->addWidget(btnRoi);
	auto *f1 = new QFormLayout;
	f1->addRow("킬 숫자 영역", roiRow);
	f1->addRow("분석 방식", m_scanMode);
	auto *l1 = new QVBoxLayout(g1);
	l1->addLayout(f1);
	l1->addWidget(m_btnScan);
	l1->addWidget(m_scanProgress);
	l1->addWidget(m_scanStatus);

	// ── 2. 클립 ──
	auto *g2 = new QWidget;
	m_before = spin(0, 60, 1, st.value("before", 10).toDouble(), " 초");
	m_after = spin(0, 60, 1, st.value("after", 5).toDouble(), " 초");
	m_gap = spin(0, 30, 1, st.value("gap", 3).toDouble(), " 초");
	m_gap->setToolTip("두 클립 사이가 이 시간보다 가까우면 한 클립으로 합쳐요 (연속 킬)");
	m_assist = new QCheckBox("어시스트만 한 장면도 포함");
	m_assist->setChecked(st.value("assist", false).toBool());
	m_btnRebuild = smallButton("다시 만들기", "지금 설정으로 클립을 처음부터 다시 만듦 (직접 고친 내용은 사라짐)");

	auto *pr = new QHBoxLayout;
	pr->addWidget(new QLabel("킬"));
	pr->addWidget(m_before);
	pr->addWidget(new QLabel("전부터"));
	pr->addWidget(m_after);
	pr->addWidget(new QLabel("후까지"));
	pr->addStretch(1);
	auto *pr2 = new QHBoxLayout;
	pr2->addWidget(new QLabel("합치기 간격"));
	pr2->addWidget(m_gap);
	pr2->addWidget(m_assist);
	pr2->addStretch(1);
	pr2->addWidget(m_btnRebuild);

	m_table = new QTableWidget(0, 5);
	m_table->setHorizontalHeaderLabels({"사용", "시작", "끝", "길이", "내용"});
	m_table->verticalHeader()->setDefaultSectionSize(24);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	m_table->setColumnWidth(0, 44);
	m_table->setColumnWidth(1, 90);
	m_table->setColumnWidth(2, 90);
	m_table->setColumnWidth(3, 60);
	m_table->setToolTip("더블클릭하면 그 클립을 미리보기로 재생해요");

	auto *bStartM = smallButton("시작 −1초");
	auto *bStartP = smallButton("시작 +1초");
	auto *bStartHere = smallButton("시작 = 현재 위치", "재생 위치를 이 클립의 시작으로");
	auto *bEndM = smallButton("끝 −1초");
	auto *bEndP = smallButton("끝 +1초");
	auto *bEndHere = smallButton("끝 = 현재 위치", "재생 위치를 이 클립의 끝으로");
	auto *bPlaySeg = smallButton("▶ 클립 재생");
	auto *bAdd = smallButton("＋ 여기에 클립 추가", "놓친 장면을 직접 추가 (재생 위치 앞뒤로 킬 전/후 설정만큼)");
	auto *bDel = smallButton("🗑 삭제");
	bDel->setShortcut(QKeySequence::Delete);

	auto *er1 = new QHBoxLayout;
	er1->addWidget(bStartM);
	er1->addWidget(bStartP);
	er1->addWidget(bStartHere);
	er1->addStretch(1);
	er1->addWidget(bPlaySeg);
	auto *er2 = new QHBoxLayout;
	er2->addWidget(bEndM);
	er2->addWidget(bEndP);
	er2->addWidget(bEndHere);
	er2->addStretch(1);
	er2->addWidget(bAdd);
	er2->addWidget(bDel);

	m_summary = new QLabel;

	auto *l2 = new QVBoxLayout(g2);
	l2->setContentsMargins(6, 6, 6, 6);
	l2->addLayout(pr);
	l2->addLayout(pr2);
	l2->addWidget(m_table, 1);
	l2->addLayout(er1);
	l2->addLayout(er2);
	l2->addWidget(m_summary);

	// ── 3. 내보내기 ──
	auto *g3 = new QGroupBox("3. 내보내기 (16:9)");
	m_xfade = spin(0, 3, 0.1, st.value("xfade", 0.5).toDouble(), " 초", 2);
	m_xfade->setToolTip("클립과 클립 사이를 겹치며 넘어가는 시간 (0 이면 그냥 이어 붙임)");
	m_fadeIn = spin(0, 5, 0.1, st.value("fadeIn", 1.0).toDouble(), " 초", 2);
	m_fadeIn->setToolTip("영상 맨 처음 검은 화면에서 밝아지는 시간 (소리도 함께)");
	m_fadeOut = spin(0, 5, 0.1, st.value("fadeOut", 1.0).toDouble(), " 초", 2);
	m_fadeOut->setToolTip("영상 맨 끝 검은 화면으로 어두워지는 시간 (소리도 함께)");
	m_res = new QComboBox;
	m_res->addItem("1920×1080 (1080p)", 1080);
	m_res->addItem("2560×1440 (1440p)", 1440);
	m_res->addItem("1280×720 (720p)", 720);
	m_res->addItem("원본 높이에 맞춤", 0);
	m_res->setCurrentIndex(std::max(0, m_res->findData(st.value("res", 1080).toInt())));
	m_fps = new QComboBox;
	m_fps->addItem("원본 프레임 (최대 60)", 0);
	m_fps->addItem("60 fps", 60);
	m_fps->addItem("30 fps", 30);
	m_fps->setCurrentIndex(std::max(0, m_fps->findData(st.value("fps", 0).toInt())));
	m_cpuOnly = new QCheckBox("CPU로만 인코딩 (그래픽카드 인코더에서 문제가 생길 때)");
	m_cpuOnly->setChecked(st.value("cpuOnly", false).toBool());
	m_btnExport = new QPushButton("💾 영상 내보내기…");
	m_btnExport->setStyleSheet("font-weight:bold; padding:5px;");
	m_exportProgress = new QProgressBar;
	m_exportProgress->setRange(0, 1000);
	m_exportProgress->setMaximumHeight(10);
	m_exportProgress->setTextVisible(false);
	m_exportStatus = new QLabel;
	m_exportStatus->setWordWrap(true);

	auto *fr = new QHBoxLayout;
	fr->addWidget(new QLabel("크로스페이드"));
	fr->addWidget(m_xfade);
	fr->addWidget(new QLabel("처음 페이드 인"));
	fr->addWidget(m_fadeIn);
	fr->addWidget(new QLabel("끝 페이드 아웃"));
	fr->addWidget(m_fadeOut);
	auto *rr = new QHBoxLayout;
	rr->addWidget(new QLabel("해상도"));
	rr->addWidget(m_res, 1);
	rr->addWidget(new QLabel("프레임"));
	rr->addWidget(m_fps, 1);
	auto *l3 = new QVBoxLayout(g3);
	l3->addLayout(fr);
	l3->addLayout(rr);
	l3->addWidget(m_cpuOnly);
	l3->addWidget(m_btnExport);
	l3->addWidget(m_exportProgress);
	l3->addWidget(m_exportStatus);

	auto *right = new QWidget;
	auto *rl = new QVBoxLayout(right);
	rl->addWidget(g1);
	m_subPanel = new SubtitlePanel;
	auto *tabs = new QTabWidget;
	tabs->addTab(g2, "2. 클립 (킬 장면만 남기기)");
	tabs->addTab(buildRankTab(), "판 등수");
	tabs->addTab(m_subPanel, "자막");
	rl->addWidget(tabs, 1);
	rl->addWidget(g3);
	right->setMinimumWidth(520);

	auto *split = new QSplitter;
	split->addWidget(left);
	split->addWidget(right);
	split->setStretchFactor(0, 3);
	split->setStretchFactor(1, 2);
	setCentralWidget(split);

	// ── 연결 ──
	connect(btnOpen, &QPushButton::clicked, this, &MainWindow::chooseVideo);
	connect(btnRoi, &QPushButton::clicked, this, &MainWindow::chooseRoi);
	connect(m_btnScan, &QPushButton::clicked, this, [this] {
		if (m_scanner->isRunning())
			m_scanner->cancel();
		else
			startScan();
	});
	connect(m_scanMode, &QComboBox::currentIndexChanged, this,
		[](int i) { QSettings().setValue("scanMode", i); });

	auto paramChanged = [this] {
		QSettings s;
		s.setValue("before", m_before->value());
		s.setValue("after", m_after->value());
		s.setValue("gap", m_gap->value());
		s.setValue("assist", m_assist->isChecked());
		rebuildSegments(false);
	};
	connect(m_before, &QDoubleSpinBox::valueChanged, this, paramChanged);
	connect(m_after, &QDoubleSpinBox::valueChanged, this, paramChanged);
	connect(m_gap, &QDoubleSpinBox::valueChanged, this, paramChanged);
	connect(m_assist, &QCheckBox::toggled, this, paramChanged);
	connect(m_btnRebuild, &QPushButton::clicked, this, [this] {
		if (m_edited && QMessageBox::question(this, "다시 만들기",
						      "직접 고친 클립(길이 조정·추가·삭제)이 사라지고 지금 설정으로 "
						      "다시 만들어요. 계속할까요?") != QMessageBox::Yes)
			return;
		rebuildSegments(true);
	});

	auto exportChanged = [this] {
		QSettings s;
		s.setValue("xfade", m_xfade->value());
		s.setValue("fadeIn", m_fadeIn->value());
		s.setValue("fadeOut", m_fadeOut->value());
		s.setValue("res", m_res->currentData());
		s.setValue("fps", m_fps->currentData());
		s.setValue("cpuOnly", m_cpuOnly->isChecked());
		refreshSummary();
	};
	connect(m_xfade, &QDoubleSpinBox::valueChanged, this, exportChanged);
	connect(m_fadeIn, &QDoubleSpinBox::valueChanged, this, exportChanged);
	connect(m_fadeOut, &QDoubleSpinBox::valueChanged, this, exportChanged);
	connect(m_res, &QComboBox::currentIndexChanged, this, exportChanged);
	connect(m_fps, &QComboBox::currentIndexChanged, this, exportChanged);
	connect(m_cpuOnly, &QCheckBox::toggled, this, exportChanged);
	connect(m_btnExport, &QPushButton::clicked, this, [this] {
		if (m_exporter->isRunning())
			m_exporter->cancel();
		else
			startExport();
	});

	connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
		m_timeline->setSelected(currentSegment());
	});
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { playSegment(row); });
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
		if (m_fillingTable || it->column() != 0 || it->row() >= m_segs.size())
			return;
		m_segs[it->row()].enabled = it->checkState() == Qt::Checked;
		m_edited = true;
		m_timeline->setData(m_segs, m_events, m_assist->isChecked());
		refreshSummary();
		saveCache();
	});

	connect(bStartM, &QPushButton::clicked, this, [this] { adjustSelected(true, -1, false); });
	connect(bStartP, &QPushButton::clicked, this, [this] { adjustSelected(true, +1, false); });
	connect(bStartHere, &QPushButton::clicked, this, [this] { adjustSelected(true, 0, true); });
	connect(bEndM, &QPushButton::clicked, this, [this] { adjustSelected(false, -1, false); });
	connect(bEndP, &QPushButton::clicked, this, [this] { adjustSelected(false, +1, false); });
	connect(bEndHere, &QPushButton::clicked, this, [this] { adjustSelected(false, 0, true); });
	connect(bPlaySeg, &QPushButton::clicked, this, [this] { playSegment(currentSegment()); });
	connect(bAdd, &QPushButton::clicked, this, &MainWindow::addSegmentAtPlayhead);
	connect(bDel, &QPushButton::clicked, this, &MainWindow::deleteSelected);

	connect(m_btnPlay, &QPushButton::clicked, this, [this] {
		if (m_player->playbackState() == QMediaPlayer::PlayingState) {
			m_player->pause();
		} else {
			m_playEnd = -1;
			m_player->play();
		}
	});
	connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
		m_btnPlay->setText(s == QMediaPlayer::PlayingState ? "⏸" : "▶");
	});
	connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
		const double t = ms / 1000.0;
		m_timeline->setPosition(t);
		m_view->setTime(t);
		m_subPanel->setPlayhead(t);
		m_posLabel->setText(formatTime(t) + " / " + formatTime(m_info.duration, false));
		if (m_playEnd >= 0 && t >= m_playEnd) {
			m_player->pause();
			m_playEnd = -1;
		}
	});
	connect(m_timeline, &TimelineBar::seekRequested, this, [this](double t) {
		m_playEnd = -1;
		m_player->setPosition(qint64(t * 1000));
	});
	connect(m_timeline, &TimelineBar::segmentClicked, this, &MainWindow::selectSegment);

	// ── 자막 ──
	m_subPanel->setOutsideCheck([this](const Subtitle &sb) {
		return mapSubtitle(sb, enabledSegments(), 0).isEmpty();
	});
	connect(m_subPanel, &SubtitlePanel::changed, this, &MainWindow::subtitlesChanged);
	connect(m_subPanel, &SubtitlePanel::selectionChanged, this, [this](int i) {
		m_view->setSubtitles(m_subPanel->subtitles(), i);
		if (i >= 0 && i < m_subPanel->subtitles().size()) {
			const Subtitle &sb = m_subPanel->subtitles()[i];
			const double here = m_player->position() / 1000.0;
			if (here < sb.start || here > sb.end) {
				m_playEnd = -1;
				m_player->setPosition(qint64(sb.start * 1000) + 50);
			}
		}
	});
	connect(m_subPanel, &SubtitlePanel::seekRequested, this, [this](double t) {
		m_playEnd = -1;
		m_player->setPosition(qint64(t * 1000) + 50);
	});
	connect(m_view, &VideoView::subtitleClicked, m_subPanel, &SubtitlePanel::select);
	connect(m_view, &VideoView::subtitleMoved, this,
		[this](int i, double y) { m_subPanel->setSelectedPosY(i, y); });
}

QVector<Segment> MainWindow::enabledSegments() const
{
	QVector<Segment> on;
	for (const Segment &s : m_segs)
		if (s.enabled)
			on.push_back(s);
	return on;
}

void MainWindow::subtitlesChanged()
{
	m_view->setSubtitles(m_subPanel->subtitles(), m_subPanel->selected());
	m_timeline->setSubtitles(m_subPanel->subtitles());
	saveCache();
}

// ───────────────────────── 영상 열기 ─────────────────────────

void MainWindow::chooseVideo()
{
	QSettings s;
	const QString path = QFileDialog::getOpenFileName(
		this, "방송 다시보기 영상 열기", s.value("lastDir").toString(),
		"영상 (*.mp4 *.mkv *.ts *.flv *.mov *.webm *.m4v *.avi);;모든 파일 (*)");
	if (!path.isEmpty())
		openVideo(path);
}

void MainWindow::openVideo(const QString &path)
{
	if (m_scanner->isRunning() || m_exporter->isRunning() || m_rankReader->isRunning()) {
		QMessageBox::information(this, "작업 중", "분석이나 내보내기가 끝난 뒤에 다른 영상을 열 수 있어요.");
		return;
	}
	QApplication::setOverrideCursor(Qt::WaitCursor);
	const VideoInfo info = probeVideo(path);
	QApplication::restoreOverrideCursor();
	if (!info.ok) {
		QMessageBox::warning(this, "열 수 없음", info.error);
		return;
	}
	QSettings().setValue("lastDir", QFileInfo(path).absolutePath());
	m_path = path;
	m_info = info;
	m_rankReader->cancel();
	m_events.clear();
	m_segs.clear();
	m_cands.clear();
	m_readings.clear();
	m_games.clear();
	m_readsDone = false;
	m_edited = false;
	m_scannedUntil = 0;
	m_scanComplete = false;
	m_exportStatus->clear();
	m_rankStatus->clear();
	m_exportProgress->setValue(0);
	m_subPanel->setSubtitles({});
	m_subPanel->setDuration(info.duration);
	m_subPanel->setVideoLoaded(true);
	m_scanProgress->setValue(0);

	// 영역: 마지막에 쓴 프리셋 → 기본 위치
	m_roiPreset = QSettings().value("lastPreset").toString();
	if (!RoiDialog::loadPreset(m_roiPreset, &m_roi)) {
		m_roiPreset.clear();
		m_roi = defaultKillRoi(info.width, info.height);
	}

	m_fileLabel->setText(QString("<b>%1</b> · %2×%3 · %4fps · %5%6")
				     .arg(QFileInfo(path).fileName().toHtmlEscaped())
				     .arg(info.width)
				     .arg(info.height)
				     .arg(QString::number(info.fps, 'g', 4))
				     .arg(formatTime(info.duration, false))
				     .arg(info.hasAudio ? "" : " · 소리 없음"));
	m_timeline->setDuration(info.duration);
	m_timeline->setScanned(0);
	m_player->setSource(QUrl::fromLocalFile(path));
	m_player->pause();

	const bool cached = loadCache();
	rebuildGames(false); // 저장된 클립은 그대로 (판 목록만 다시 계산)
	if (cached) {
		m_scanStatus->setText(QString("이전 분석 결과를 불러왔어요 (킬 %1번%2). 다시 분석하려면 [킬 찾기 시작].")
					      .arg(std::count_if(m_events.begin(), m_events.end(),
								 [](const KillEvent &e) { return e.kill; }))
					      .arg(m_scanComplete ? "" : ", 중간까지만 분석됨"));
		m_timeline->setScanned(m_scannedUntil);
		if (m_info.duration > 0)
			m_scanProgress->setValue(int(std::clamp(m_scannedUntil / m_info.duration, 0.0, 1.0) * 1000));
	} else {
		m_scanStatus->setText("먼저 [영역 지정…]으로 킬 숫자 위치를 확인한 뒤 [킬 찾기 시작]을 누르세요.");
	}
	updateRoiLabel();
	refreshTable();
	subtitlesChanged();
	setBusy(false, false);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
	if (e->mimeData()->hasUrls())
		e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
	const auto urls = e->mimeData()->urls();
	if (!urls.isEmpty() && urls.first().isLocalFile())
		openVideo(urls.first().toLocalFile());
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	if (m_exporter->isRunning() &&
	    QMessageBox::question(this, "내보내는 중", "내보내기를 멈추고 닫을까요?") != QMessageBox::Yes) {
		e->ignore();
		return;
	}
	m_scanner->cancel();
	m_rankReader->cancel();
	m_exporter->cancel();
	saveCache();
	e->accept();
}

// ───────────────────────── 킬 찾기 ─────────────────────────

void MainWindow::updateRoiLabel()
{
	if (m_path.isEmpty()) {
		m_roiLabel->setText("-");
		return;
	}
	m_roiLabel->setText(m_roiPreset.isEmpty() ? QString("기본 위치 (오른쪽 위)")
						  : QString("프리셋 <b>%1</b>").arg(m_roiPreset.toHtmlEscaped()));
}

void MainWindow::chooseRoi()
{
	if (m_path.isEmpty()) {
		chooseVideo();
		if (m_path.isEmpty())
			return;
	}
	const double at = m_player->position() / 1000.0 > 1 ? m_player->position() / 1000.0 : m_info.duration * 0.3;
	RoiDialog dlg(m_path, m_info.duration, m_info.width, m_info.height, m_roi, m_roiPreset, at, this);
	if (dlg.exec() != QDialog::Accepted)
		return;
	const QRectF r = dlg.roi();
	const bool changed = r != m_roi;
	m_roi = r;
	m_roiPreset = dlg.presetName();
	// 프리셋을 고른 뒤 상자를 움직였으면 프리셋 이름은 떼어 냄
	QRectF pr;
	if (!m_roiPreset.isEmpty() && RoiDialog::loadPreset(m_roiPreset, &pr) && pr != m_roi)
		m_roiPreset.clear();
	QSettings().setValue("lastPreset", m_roiPreset);
	updateRoiLabel();
	if (changed && !m_events.isEmpty())
		m_scanStatus->setText("영역이 바뀌었어요. 새 영역으로 다시 분석하려면 [킬 찾기 시작]을 누르세요.");
}

void MainWindow::startScan()
{
	if (m_path.isEmpty()) {
		chooseVideo();
		if (m_path.isEmpty())
			return;
	}
	if (!m_events.isEmpty() || m_edited) {
		if (QMessageBox::question(this, "다시 분석",
					  "지금 있는 킬 목록과 클립을 지우고 처음부터 다시 분석할까요?") !=
		    QMessageBox::Yes)
			return;
	}
	m_rankReader->cancel();
	m_events.clear();
	m_segs.clear();
	m_cands.clear();
	m_readings.clear();
	m_readsDone = false;
	rebuildGames();
	m_edited = false;
	m_scannedUntil = 0;
	m_scanComplete = false;
	refreshTable();
	m_scanStatus->setText("분석을 시작하는 중…");
	m_rankStatus->setText("킬을 찾으면서 결과 화면도 함께 찾는 중…");
	setBusy(true, false);
	m_scanner->setResultDetection(gameRect());
	m_scanner->start(m_path, m_roi, m_info.duration, KillScanner::Mode(m_scanMode->currentData().toInt()));
}

void MainWindow::onScanFinished(bool ok, const QString &msg)
{
	setBusy(false, false);
	const int kills = int(std::count_if(m_events.begin(), m_events.end(), [](const KillEvent &e) { return e.kill; }));
	const int assists =
		int(std::count_if(m_events.begin(), m_events.end(), [](const KillEvent &e) { return !e.kill && e.assist; }));
	m_scanComplete = ok && msg.isEmpty();
	if (ok) {
		m_scanProgress->setValue(1000);
		QString text = QString("<b>분석 끝</b> · 킬 장면 %1번, 어시스트만 %2번 → 클립 %3개")
				       .arg(kills)
				       .arg(assists)
				       .arg(m_segs.size());
		if (!msg.isEmpty())
			text += "<br>" + msg;
		if (m_events.isEmpty())
			text += "<br><span style='color:#e55'>킬을 하나도 찾지 못했어요. [영역 지정…]에서 게임 장면에 "
				"숫자 3개가 초록 상자로 잡히는지 확인해 주세요.</span>";
		m_scanStatus->setText(text);
	} else {
		m_scanStatus->setText(msg + QString(" (그때까지 찾은 킬 %1번은 남겨 뒀어요)").arg(kills));
	}
	if (!m_edited)
		rebuildSegments(true);
	saveCache();
	// 결과 화면 후보에서 등수 읽기 (멈춘 경우에도 그때까지 찾은 만큼)
	if (!m_cands.isEmpty())
		startRankRead();
	else
		rebuildGames();
}

// ───────────────────────── 클립 ─────────────────────────

SegmentParams MainWindow::params() const
{
	SegmentParams p;
	p.before = m_before->value();
	p.after = m_after->value();
	p.mergeGap = m_gap->value();
	p.includeAssist = m_assist->isChecked();
	return p;
}

void MainWindow::rebuildSegments(bool force)
{
	if (m_edited && !force) {
		m_summary->setText("<span style='color:#e9a'>설정을 바꿨어요. 직접 고친 클립이 있어서 자동으로 바꾸지 "
				   "않았어요 → [다시 만들기]</span>");
		return;
	}
	m_segs = buildSegments(filterEvents(m_events, m_games, gameFilter()), params(), m_info.duration);
	m_edited = false;
	refreshTable();
	saveCache();
}

void MainWindow::refreshTable()
{
	const int keep = currentSegment();
	m_fillingTable = true;
	m_table->setRowCount(int(m_segs.size()));
	for (int i = 0; i < m_segs.size(); ++i) {
		const Segment &s = m_segs[i];
		auto *on = new QTableWidgetItem;
		on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		on->setCheckState(s.enabled ? Qt::Checked : Qt::Unchecked);
		m_table->setItem(i, 0, on);
		m_table->setItem(i, 1, new QTableWidgetItem(formatTime(s.start)));
		m_table->setItem(i, 2, new QTableWidgetItem(formatTime(s.end)));
		m_table->setItem(i, 3, new QTableWidgetItem(QString::number(s.length(), 'f', 1) + "s"));
		QStringList what;
		if (s.kills > 0)
			what << QString("킬 %1").arg(s.kills);
		if (s.assists > 0)
			what << QString("어시 %1").arg(s.assists);
		if (s.manual)
			what << "직접 추가";
		m_table->setItem(i, 4, new QTableWidgetItem(what.join(" · ")));
	}
	m_fillingTable = false;
	if (keep >= 0 && keep < m_segs.size())
		m_table->selectRow(keep);
	m_timeline->setData(m_segs, m_events, m_assist->isChecked());
	m_subPanel->refreshMarks();
	refreshSummary();
}

void MainWindow::refreshSummary()
{
	QVector<Segment> on;
	for (const Segment &s : m_segs)
		if (s.enabled)
			on.push_back(s);
	if (m_segs.isEmpty()) {
		m_summary->setText(m_path.isEmpty() ? QString() : "클립 없음");
		return;
	}
	const double d = Exporter::effectiveCrossfade(on, m_xfade->value());
	QString text = QString("사용 클립 <b>%1개</b> / %2개 · 결과 길이 <b>%3</b>")
			       .arg(on.size())
			       .arg(m_segs.size())
			       .arg(formatTime(Exporter::outputLength(on, d), false));
	if (on.size() > 1 && m_xfade->value() > 0 && d + 0.01 < m_xfade->value())
		text += QString(" · 짧은 클립 때문에 크로스페이드 %1초로 줄어듦").arg(d, 0, 'f', 2);
	if (!m_games.isEmpty()) {
		const GameFilter f = gameFilter();
		const int kept = int(std::count_if(m_games.begin(), m_games.end(), [&](const GameResult &g) { return keepGame(g, f); }));
		if (kept < m_games.size())
			text += QString("<br>판 등수: %1판 중 <b>%2판</b>의 킬만 사용").arg(m_games.size()).arg(kept);
	}
	m_summary->setText(text);
}

int MainWindow::currentSegment() const
{
	const auto rows = m_table->selectionModel()->selectedRows();
	return rows.isEmpty() ? -1 : rows.first().row();
}

void MainWindow::selectSegment(int index)
{
	if (index >= 0 && index < m_table->rowCount()) {
		m_table->selectRow(index);
		m_table->scrollToItem(m_table->item(index, 0));
	}
}

void MainWindow::adjustSelected(bool startSide, double delta, bool toPlayhead)
{
	const int i = currentSegment();
	if (i < 0 || i >= m_segs.size())
		return;
	Segment &s = m_segs[i];
	const double here = m_player->position() / 1000.0;
	if (startSide) {
		const double v = toPlayhead ? here : s.start + delta;
		s.start = std::clamp(v, 0.0, s.end - 0.5);
	} else {
		const double v = toPlayhead ? here : s.end + delta;
		s.end = std::clamp(v, s.start + 0.5, m_info.duration);
	}
	m_edited = true;
	refreshTable();
	saveCache();
	// 바꾼 쪽 근처를 바로 보여 줌
	m_playEnd = -1;
	m_player->setPosition(qint64((startSide ? s.start : std::max(s.start, s.end - 2.0)) * 1000));
}

void MainWindow::addSegmentAtPlayhead()
{
	if (m_path.isEmpty())
		return;
	const double t = m_player->position() / 1000.0;
	Segment s;
	s.start = std::max(0.0, t - m_before->value());
	s.end = std::min(m_info.duration, t + m_after->value());
	if (s.end - s.start < 0.5)
		return;
	s.manual = true;
	auto it = std::lower_bound(m_segs.begin(), m_segs.end(), s,
				   [](const Segment &a, const Segment &b) { return a.start < b.start; });
	const int idx = int(it - m_segs.begin());
	m_segs.insert(it, s);
	m_edited = true;
	refreshTable();
	selectSegment(idx);
	saveCache();
}

void MainWindow::deleteSelected()
{
	const int i = currentSegment();
	if (i < 0 || i >= m_segs.size())
		return;
	m_segs.removeAt(i);
	m_edited = true;
	refreshTable();
	selectSegment(std::min(i, int(m_segs.size()) - 1));
	saveCache();
}

void MainWindow::playSegment(int index)
{
	if (index < 0 || index >= m_segs.size())
		return;
	selectSegment(index);
	m_player->setPosition(qint64(m_segs[index].start * 1000));
	m_playEnd = m_segs[index].end;
	m_player->play();
}

// ───────────────────────── 내보내기 ─────────────────────────

void MainWindow::startExport()
{
	int count = 0;
	for (const Segment &s : m_segs)
		count += s.enabled;
	if (m_path.isEmpty() || count == 0) {
		QMessageBox::information(this, "내보내기", "내보낼 클립이 없어요. 먼저 킬을 찾아 주세요.");
		return;
	}
	if (m_scanner->isRunning() || m_rankReader->isRunning()) {
		QMessageBox::information(this, "내보내기", "분석(등수 읽기 포함)이 끝난 뒤에 내보낼 수 있어요.");
		return;
	}
	QSettings st;
	const QFileInfo fi(m_path);
	const QString suggestion = QDir(st.value("exportDir", fi.absolutePath()).toString())
					   .filePath(fi.completeBaseName() + "_킬하이라이트.mp4");
	const QString out = QFileDialog::getSaveFileName(this, "결과 영상 저장", suggestion, "MP4 영상 (*.mp4)");
	if (out.isEmpty())
		return;
	if (QFileInfo(out).absoluteFilePath() == fi.absoluteFilePath()) {
		QMessageBox::warning(this, "내보내기", "원본 파일과 다른 이름으로 저장해 주세요.");
		return;
	}
	st.setValue("exportDir", QFileInfo(out).absolutePath());
	st.setValue("lastExport", out);

	ExportSettings s;
	s.height = m_res->currentData().toInt();
	s.fps = m_fps->currentData().toDouble();
	s.crossfade = m_xfade->value();
	s.fadeIn = m_fadeIn->value();
	s.fadeOut = m_fadeOut->value();
	s.cpuOnly = m_cpuOnly->isChecked();
	s.outPath = out;
	m_player->pause();
	m_exportProgress->setValue(0);
	m_exportStatus->setText(QString("내보내는 중… (클립 %1개)").arg(count));
	setBusy(false, true);
	m_exporter->start(m_path, m_info, m_segs, m_subPanel->subtitles(), s);
}

void MainWindow::setBusy(bool scanning, bool exporting)
{
	const bool hasVideo = !m_path.isEmpty();
	m_btnScan->setText(scanning ? "⏹ 분석 멈추기" : "🔍 킬 찾기 시작");
	m_btnScan->setEnabled(hasVideo && !exporting);
	m_scanMode->setEnabled(!scanning);
	m_btnExport->setText(exporting ? "⏹ 내보내기 멈추기" : "💾 영상 내보내기…");
	m_btnExport->setEnabled(hasVideo && !scanning);
	m_btnRebuild->setEnabled(!scanning && !exporting);
	const bool reading = m_rankReader->isRunning();
	m_btnReadRanks->setEnabled(hasVideo && !scanning && !exporting && !reading && !m_cands.isEmpty());
	m_gameArea->setEnabled(!scanning);
}

// ───────────────────────── 분석 결과 저장 ─────────────────────────

QString MainWindow::cachePath() const
{
	if (m_path.isEmpty())
		return {};
	const QFileInfo fi(m_path);
	const QByteArray key = (fi.absoluteFilePath() + "|" + QString::number(fi.size()) + "|" +
				QString::number(fi.lastModified().toSecsSinceEpoch()))
				       .toUtf8();
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/scans";
	QDir().mkpath(dir);
	return dir + "/" + QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(20) + ".json";
}

void MainWindow::saveCache()
{
	if (m_path.isEmpty() ||
	    (m_events.isEmpty() && m_segs.isEmpty() && m_scannedUntil <= 0 && m_subPanel->subtitles().isEmpty()))
		return;
	QJsonObject o;
	o["version"] = 1;
	o["video"] = m_path;
	o["roi"] = QJsonArray{m_roi.x(), m_roi.y(), m_roi.width(), m_roi.height()};
	o["preset"] = m_roiPreset;
	o["events"] = eventsToJson(m_events);
	o["segments"] = segmentsToJson(m_segs);
	o["subtitles"] = subtitlesToJson(m_subPanel->subtitles());
	o["edited"] = m_edited;
	o["scannedUntil"] = m_scannedUntil;
	o["complete"] = m_scanComplete;
	o["resultScreens"] = candidatesToJson(m_cands);
	o["rankReadings"] = readingsToJson(m_readings);
	o["ranksRead"] = m_readsDone;
	o["gameEdits"] = gameEditsToJson(m_games);
	QFile f(cachePath());
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool MainWindow::loadCache()
{
	QFile f(cachePath());
	if (!f.open(QIODevice::ReadOnly))
		return false;
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	if (o["version"].toInt() != 1)
		return false;
	const QJsonArray r = o["roi"].toArray();
	if (r.size() == 4) {
		m_roi = QRectF(r[0].toDouble(), r[1].toDouble(), r[2].toDouble(), r[3].toDouble());
		m_roiPreset = o["preset"].toString();
	}
	m_events = eventsFromJson(o["events"].toArray());
	m_segs = segmentsFromJson(o["segments"].toArray());
	m_subPanel->setSubtitles(subtitlesFromJson(o["subtitles"].toArray()));
	m_edited = o["edited"].toBool();
	m_scannedUntil = o["scannedUntil"].toDouble();
	m_scanComplete = o["complete"].toBool();
	m_cands = candidatesFromJson(o["resultScreens"].toArray());
	m_readings = readingsFromJson(o["rankReadings"].toArray());
	m_readsDone = o["ranksRead"].toBool();
	m_games = gameEditsFromJson(o["gameEdits"].toArray()); // 직접 고친 값만 (rebuildGames 가 옮겨 담음)
	return !m_events.isEmpty() || !m_segs.isEmpty() || m_scannedUntil > 0 || !m_subPanel->subtitles().isEmpty();
}

// ───────────────────────── 판 등수 ─────────────────────────

QWidget *MainWindow::buildRankTab()
{
	QSettings st;
	auto *w = new QWidget;
	m_rankFilter = new QCheckBox("등수 필터 사용:");
	m_rankFilter->setChecked(st.value("rankFilter", false).toBool());
	m_maxRank = new QSpinBox;
	m_maxRank->setRange(1, 24);
	m_maxRank->setValue(st.value("maxRank", 3).toInt());
	m_maxRank->setSuffix(" 등");
	m_keepUnknown = new QCheckBox("등수를 모르는 판도 남기기");
	m_keepUnknown->setChecked(st.value("keepUnknown", true).toBool());
	m_keepUnknown->setToolTip("결과 화면을 못 찾았거나 숫자를 못 읽은 판, 영상이 판 중간에 끝난 경우");
	m_gameArea = new QComboBox;
	m_gameArea->addItem("영상 전체 (보통)", 0);
	m_gameArea->addItem("킬 숫자 영역으로 계산 (게임 화면이 작게 들어간 방송)", 1);
	m_gameArea->setCurrentIndex(std::max(0, m_gameArea->findData(st.value("gameArea", 0).toInt())));
	m_gameArea->setToolTip("결과 화면을 찾을 게임 화면 위치. 바꾸면 [킬 찾기 시작]으로 다시 분석해야 해요");

	auto *row1 = new QHBoxLayout;
	row1->addWidget(m_rankFilter);
	row1->addWidget(m_maxRank);
	row1->addWidget(new QLabel("이상 판의 킬만 남기기"));
	row1->addStretch(1);
	auto *row2 = new QHBoxLayout;
	row2->addWidget(m_keepUnknown);
	row2->addStretch(1);
	auto *row3 = new QHBoxLayout;
	row3->addWidget(new QLabel("게임 화면"));
	row3->addWidget(m_gameArea, 1);

	m_gameTable = new QTableWidget(0, 7);
	m_gameTable->setHorizontalHeaderLabels({"남김", "판", "결과 화면", "등수", "킬", "상태", "근거"});
	m_gameTable->verticalHeader()->setVisible(false);
	m_gameTable->verticalHeader()->setDefaultSectionSize(24);
	m_gameTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_gameTable->setSelectionMode(QAbstractItemView::SingleSelection);
	m_gameTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_gameTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
	m_gameTable->setColumnWidth(0, 44);
	m_gameTable->setColumnWidth(1, 36);
	m_gameTable->setColumnWidth(2, 80);
	m_gameTable->setColumnWidth(3, 50);
	m_gameTable->setColumnWidth(4, 36);
	m_gameTable->setColumnWidth(5, 70);
	m_gameTable->setToolTip("더블클릭: 그 판의 결과 화면으로 이동 · 등수 칸 더블클릭: 등수 직접 고치기");

	m_rankStatus = new QLabel;
	m_rankStatus->setWordWrap(true);
	m_btnReadRanks = smallButton("등수 다시 읽기", "분석 때 찾은 결과 화면에서 등수만 다시 읽어요 (영상 전체를 다시 훑지 않음)");
	m_btnResetGames = smallButton("직접 고친 것 되돌리기", "판마다 직접 바꾼 등수·남김 표시를 자동 판정으로 되돌려요");
	m_btnSaveLog = smallButton("판별 기록 저장 (CSV)…");
	auto *row4 = new QHBoxLayout;
	row4->addWidget(m_btnReadRanks);
	row4->addWidget(m_btnResetGames);
	row4->addStretch(1);
	row4->addWidget(m_btnSaveLog);

	auto *help = new QLabel("<span style='color:#888'>게임이 끝난 뒤 나오는 결과 화면 · 점수 창 · 순위표에서 등수를 읽어요. "
				"방송인이 1초 만에 넘겨도 판마다 화면이 2~3개라 대부분 잡혀요. 두 화면 이상이 같으면 <b>확실</b>, "
				"하나만 읽었거나 서로 다르면 <b>확인 필요</b>.</span>");
	help->setWordWrap(true);

	auto *l = new QVBoxLayout(w);
	l->setContentsMargins(6, 6, 6, 6);
	l->addLayout(row1);
	l->addLayout(row2);
	l->addLayout(row3);
	l->addWidget(m_gameTable, 1);
	l->addWidget(m_rankStatus);
	l->addLayout(row4);
	l->addWidget(help);

	connect(m_rankFilter, &QCheckBox::toggled, this, &MainWindow::gameFilterChanged);
	connect(m_maxRank, &QSpinBox::valueChanged, this, &MainWindow::gameFilterChanged);
	connect(m_keepUnknown, &QCheckBox::toggled, this, &MainWindow::gameFilterChanged);
	connect(m_gameArea, &QComboBox::currentIndexChanged, this, [this] {
		QSettings().setValue("gameArea", m_gameArea->currentData());
		if (!m_cands.isEmpty())
			m_rankStatus->setText("게임 화면 위치를 바꿨어요. 새 위치로 찾으려면 [킬 찾기 시작]으로 다시 분석하세요.");
	});
	connect(m_btnReadRanks, &QPushButton::clicked, this, &MainWindow::startRankRead);
	connect(m_btnResetGames, &QPushButton::clicked, this, [this] {
		for (GameResult &g : m_games) {
			g.userRank = 0;
			g.userKeep = -1;
		}
		refreshGameTable();
		rebuildSegments(false);
		saveCache();
	});
	connect(m_btnSaveLog, &QPushButton::clicked, this, &MainWindow::saveGameLog);
	connect(m_gameTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
		if (m_fillingGames || it->column() != 0 || it->row() >= m_games.size())
			return;
		m_games[it->row()].userKeep = it->checkState() == Qt::Checked ? 1 : 0;
		refreshGameTable();
		rebuildSegments(false);
		saveCache();
	});
	connect(m_gameTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
		if (row < 0 || row >= m_games.size())
			return;
		GameResult &g = m_games[row];
		if (col == 3 && g.status != GameResult::NoResult) {
			bool ok = false;
			const int v = QInputDialog::getInt(this, "등수 고치기",
							   QString("%1판 등수 (0 = 자동 판정으로 되돌리기)").arg(row + 1),
							   g.shownRank(), 0, 24, 1, &ok);
			if (!ok)
				return;
			g.userRank = v;
			refreshGameTable();
			rebuildSegments(false);
			saveCache();
			return;
		}
		// 결과 화면(없으면 판 시작)으로 이동
		m_playEnd = -1;
		const double t = g.status == GameResult::NoResult ? g.start : g.end + 0.3;
		m_player->setPosition(qint64(t * 1000));
	});
	return w;
}

QRectF MainWindow::gameRect() const
{
	if (m_gameArea->currentData().toInt() == 1)
		return gameRectFromKillRoi(m_roi, m_info.width, m_info.height);
	return QRectF(0, 0, 1, 1);
}

GameFilter MainWindow::gameFilter() const
{
	GameFilter f;
	f.enabled = m_rankFilter->isChecked();
	f.maxRank = m_maxRank->value();
	f.keepUnknown = m_keepUnknown->isChecked();
	return f;
}

void MainWindow::gameFilterChanged()
{
	QSettings s;
	s.setValue("rankFilter", m_rankFilter->isChecked());
	s.setValue("maxRank", m_maxRank->value());
	s.setValue("keepUnknown", m_keepUnknown->isChecked());
	refreshGameTable();
	rebuildSegments(false);
}

void MainWindow::startRankRead()
{
	if (m_path.isEmpty() || m_cands.isEmpty() || m_scanner->isRunning())
		return;
	m_readsDone = false;
	m_rankStatus->setText(QString("결과 화면 %1곳에서 등수 읽는 중…").arg(m_cands.size()));
	m_rankReader->start(m_path, gameRect(), m_cands);
	setBusy(false, false);
}

void MainWindow::rebuildGames(bool applyToClips)
{
	const QVector<GameResult> old = m_games;
	m_games = buildGames(m_cands, m_readings, m_events, m_info.duration);
	carryOverUserEdits(old, &m_games);
	refreshGameTable();
	if (applyToClips && !m_games.isEmpty())
		rebuildSegments(false);
	if (m_rankReader->isRunning())
		return;
	if (m_path.isEmpty()) {
		m_rankStatus->clear();
	} else if (m_cands.isEmpty() && m_scannedUntil <= 0) {
		m_rankStatus->setText("[킬 찾기 시작]을 누르면 결과 화면도 함께 찾아요.");
	} else if (m_cands.isEmpty()) {
		m_rankStatus->setText("<span style='color:#e9a'>결과 화면을 하나도 찾지 못했어요. 게임 화면이 영상 일부에만 "
				      "들어간 방송이면 [게임 화면]을 '킬 숫자 영역으로 계산'으로 바꾸고 다시 분석해 보세요.</span>");
	} else {
		const int sure = int(std::count_if(m_games.begin(), m_games.end(),
						   [](const GameResult &g) { return g.status == GameResult::Sure; }));
		const int check = int(std::count_if(m_games.begin(), m_games.end(),
						    [](const GameResult &g) { return g.status == GameResult::Check; }));
		QString t = QString("판 <b>%1개</b> · 확실 %2 · 확인 필요 %3").arg(m_games.size()).arg(sure).arg(check);
		if (!m_readsDone)
			t += " · <span style='color:#e9a'>등수를 아직 다 읽지 않았어요 → [등수 다시 읽기]</span>";
		m_rankStatus->setText(t);
	}
}

void MainWindow::refreshGameTable()
{
	const GameFilter f = gameFilter();
	m_fillingGames = true;
	m_gameTable->setRowCount(int(m_games.size()));
	for (int i = 0; i < m_games.size(); ++i) {
		const GameResult &g = m_games[i];
		const bool keep = keepGame(g, f);
		auto *on = new QTableWidgetItem;
		on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		on->setCheckState(keep ? Qt::Checked : Qt::Unchecked);
		if (g.userKeep >= 0)
			on->setToolTip("직접 정함");
		m_gameTable->setItem(i, 0, on);
		m_gameTable->setItem(i, 1, new QTableWidgetItem(QString::number(i + 1)));
		m_gameTable->setItem(i, 2, new QTableWidgetItem(g.status == GameResult::NoResult ? QString("-")
												  : formatTime(g.end, false)));
		const int r = g.shownRank();
		auto *ri = new QTableWidgetItem(r > 0 ? QString("%1위%2").arg(r).arg(g.userRank > 0 ? " ✎" : "") : QString("?"));
		if (g.userRank > 0)
			ri->setToolTip(QString("직접 고침 (자동 판정: %1)").arg(g.rank > 0 ? QString("%1위").arg(g.rank) : "모름"));
		m_gameTable->setItem(i, 3, ri);
		m_gameTable->setItem(i, 4, new QTableWidgetItem(QString::number(g.kills)));
		auto *st = new QTableWidgetItem(g.userRank > 0 ? QString("직접 고침") : statusText(g.status));
		if (g.userRank == 0 && g.status == GameResult::Check)
			st->setForeground(QColor(230, 150, 60));
		m_gameTable->setItem(i, 5, st);
		auto *ev = new QTableWidgetItem(g.evidence);
		ev->setToolTip(g.evidence);
		m_gameTable->setItem(i, 6, ev);
		if (!keep)
			for (int c = 1; c < 7; ++c)
				m_gameTable->item(i, c)->setForeground(QColor(140, 140, 140));
	}
	m_fillingGames = false;
	m_btnSaveLog->setEnabled(!m_games.isEmpty());
	m_btnResetGames->setEnabled(std::any_of(m_games.begin(), m_games.end(),
						[](const GameResult &g) { return g.userRank > 0 || g.userKeep >= 0; }));
	m_btnReadRanks->setEnabled(!m_cands.isEmpty() && !m_scanner->isRunning() && !m_rankReader->isRunning() &&
				   !m_exporter->isRunning());
}

void MainWindow::saveGameLog()
{
	if (m_games.isEmpty())
		return;
	const QFileInfo fi(m_path);
	const QString out = QFileDialog::getSaveFileName(this, "판별 기록 저장", fi.absoluteDir().filePath(fi.completeBaseName() + "_판별기록.csv"),
							 "CSV (*.csv)");
	if (out.isEmpty())
		return;
	QFile f(out);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		QMessageBox::warning(this, "저장 실패", "파일을 쓸 수 없어요: " + out);
		return;
	}
	const GameFilter flt = gameFilter();
	QTextStream ts(&f);
	ts.setEncoding(QStringConverter::Utf8);
	ts.setGenerateByteOrderMark(true); // 엑셀에서 한글이 깨지지 않게
	auto q = [](QString v) { return "\"" + v.replace("\"", "\"\"") + "\""; };
	ts << "판,시작,결과 화면,등수,자동 판정 등수,상태,킬,남김,근거\n";
	for (int i = 0; i < m_games.size(); ++i) {
		const GameResult &g = m_games[i];
		ts << i + 1 << "," << formatTime(g.start, false) << ","
		   << (g.status == GameResult::NoResult ? QString("-") : formatTime(g.end, false)) << ","
		   << (g.shownRank() > 0 ? QString::number(g.shownRank()) : QString("?")) << ","
		   << (g.rank > 0 ? QString::number(g.rank) : QString("?")) << ","
		   << (g.userRank > 0 ? QString("직접 고침") : statusText(g.status)) << "," << g.kills << ","
		   << (keepGame(g, flt) ? "남김" : "뺌") << "," << q(g.evidence) << "\n";
	}
	m_rankStatus->setText("판별 기록을 저장했어요: " + QDir::toNativeSeparators(out));
}
