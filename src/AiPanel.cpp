#include "AiPanel.h"

#include "AudioLevels.h"
#include "ChzzkChat.h"
#include "KillScanner.h"
#include "RoiDialog.h"
#include "Util.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QPushButton *button(const QString &text, const QString &tip = {})
{
	auto *b = new QPushButton(text);
	b->setToolTip(tip);
	return b;
}
constexpr int kAiBatch = 30; // 한 번에 AI 로 확인할 후보 수
} // namespace

AiPanel::AiPanel(QWidget *parent)
	: QWidget(parent), m_chatScan(new KillScanner(this)), m_chzzk(new ChzzkChat(this)), m_audio(new AudioLevels(this)),
	  m_ai(new LocalAi(this)), m_recalc(new QTimer(this))
{
	m_recalc->setSingleShot(true);
	m_recalc->setInterval(400);

	// ── 화면 채팅 (기본) ──
	m_screenOn = new QCheckBox("킬 찾기 때 방송 화면 채팅창도 같이 보기");
	m_screenOn->setChecked(QSettings().value("ai/screenChat", true).toBool());
	m_screenOn->setToolTip("채팅창이 위로 밀려 올라가는 속도로 채팅이 몰린 순간을 찾아요 (정확하게 방식에서만)");
	m_btnChatRect = button("채팅창 영역 지정…", "방송 화면에서 채팅 말풍선이 쌓이는 곳을 상자로 지정 (스트리머마다 한 번)");
	m_btnScreenScan = button("화면 채팅만 분석", "이미 킬 찾기를 끝낸 영상에 화면 채팅만 따로 재요 (킬 결과는 그대로)");
	m_screenStatus = new QLabel;
	m_screenStatus->setWordWrap(true);
	auto *screenBox = new QGroupBox("채팅 반응 — 방송 화면 채팅창");
	auto *sl = new QVBoxLayout(screenBox);
	auto *s1 = new QHBoxLayout;
	s1->addWidget(m_screenOn);
	s1->addStretch(1);
	auto *s2 = new QHBoxLayout;
	s2->addWidget(m_btnChatRect);
	s2->addWidget(m_btnScreenScan);
	s2->addStretch(1);
	sl->addLayout(s1);
	sl->addLayout(s2);
	sl->addWidget(m_screenStatus);

	// ── 치지직 채팅 (선택) ──
	m_url = new QLineEdit;
	m_url->setPlaceholderText("치지직 다시보기 주소 (https://chzzk.naver.com/video/1234567)");
	m_btnChat = button("채팅 가져오기", "다시보기 채팅 전체를 받아 이 영상과 함께 저장해요 (한 번만)");
	m_offset = new QDoubleSpinBox;
	m_offset->setRange(-36000, 36000);
	m_offset->setDecimals(1);
	m_offset->setSingleStep(1);
	m_offset->setSuffix(" 초");
	m_offset->setToolTip("녹화 파일과 다시보기의 시간 차이.\n"
			     "치지직 다시보기를 받은 파일이면 0. 방송인이 따로 녹화한 파일이면,\n"
			     "같은 장면이 녹화 파일에서 몇 초인지 − 다시보기에서 몇 초인지 를 넣어요");
	m_delay = new QDoubleSpinBox;
	m_delay->setRange(0, 30);
	m_delay->setDecimals(0);
	m_delay->setValue(5);
	m_delay->setSuffix(" 초");
	m_delay->setToolTip("시청자가 화면을 보고 채팅을 치기까지 걸리는 시간 (방송 지연 포함). 보통 3~8초");
	m_chatStatus = new QLabel;
	m_chatStatus->setWordWrap(true);

	auto *chatBox = new QGroupBox("(선택) 치지직 채팅 받기 — ㅋㅋ 내용까지 봐서 더 정확");
	chatBox->setCheckable(true); // 체크를 끄면 접힘 (평소엔 화면 채팅만으로 충분)
	auto *chatInner = new QWidget;
	auto *cl = new QVBoxLayout(chatInner);
	cl->setContentsMargins(0, 0, 0, 0);
	auto *chatOuter = new QVBoxLayout(chatBox);
	chatOuter->addWidget(chatInner);
	m_chzzkBox = chatBox;
	connect(chatBox, &QGroupBox::toggled, chatInner, &QWidget::setVisible);
	chatBox->setChecked(false);
	auto *r1 = new QHBoxLayout;
	r1->addWidget(m_url, 1);
	r1->addWidget(m_btnChat);
	auto *r2 = new QHBoxLayout;
	r2->addWidget(new QLabel("녹화 시간 맞추기"));
	r2->addWidget(m_offset);
	r2->addSpacing(12);
	r2->addWidget(new QLabel("채팅 반응 지연"));
	r2->addWidget(m_delay);
	r2->addStretch(1);
	cl->addLayout(r1);
	cl->addLayout(r2);
	cl->addWidget(m_chatStatus);

	m_btnFind = button("🔎 편집점 찾기", "채팅·소리 크기로 킬 클립에 점수를 매기고, 재밌는 장면 후보를 찾아요");
	m_btnFind->setStyleSheet("font-weight:bold; padding:5px;");
	m_status = new QLabel;
	m_status->setWordWrap(true);

	// ── 잘한 장면 ──
	m_target = new QSpinBox;
	m_target->setRange(1, 600);
	m_target->setValue(20);
	m_target->setSuffix(" 분");
	m_btnFill = button("점수 높은 클립으로 맞추기", "점수 높은 킬 클립부터 켜서 결과 길이를 이 안으로 (나머지는 끄기만, 되돌리기 가능)");
	auto *best = new QGroupBox("잘한 장면");
	auto *bl = new QVBoxLayout(best);
	auto *bh = new QHBoxLayout;
	bh->addWidget(new QLabel("롱폼 길이"));
	bh->addWidget(m_target);
	bh->addWidget(m_btnFill);
	bh->addStretch(1);
	auto *bhelp = new QLabel("<span style='color:#888'>킬 클립마다 ★점수가 붙어요 (킬 수·몰아서 킨 킬·채팅 반응·큰 리액션·1등 마지막 교전).</span>");
	bhelp->setWordWrap(true);
	bl->addLayout(bh);
	bl->addWidget(bhelp);

	// ── 재밌는 장면 ──
	m_table = new QTableWidget(0, 5);
	m_table->setHorizontalHeaderLabels({"넣기", "시각", "점수", "AI", "설명"});
	m_table->verticalHeader()->setVisible(false);
	m_table->verticalHeader()->setDefaultSectionSize(24);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
	m_table->setColumnWidth(0, 40);
	m_table->setColumnWidth(1, 70);
	m_table->setColumnWidth(2, 44);
	m_table->setColumnWidth(3, 56);
	m_table->setToolTip("더블클릭: 미리보기 · 설명에 마우스를 올리면 받아 적은 말");
	m_hideDrop = new QCheckBox("AI가 아니라고 한 것 숨기기");
	m_hideDrop->setChecked(true);
	m_btnAi = button("🤖 로컬 AI로 확인", QString("아직 확인 안 한 후보를 점수 순으로 %1개씩 음성 인식 + AI 로 확인해요").arg(kAiBatch));
	m_btnAiSettings = button("AI 설정…");
	m_every = new QSpinBox;
	m_every->setRange(1, 30);
	m_every->setValue(3);
	m_every->setPrefix("매 ");
	m_every->setSuffix(" 분");
	m_btnBreaks = button("환기용 자동 넣기", "결과 영상에서 이 간격마다 가장 좋은 재밌는 장면을 하나씩 넣어요");
	m_aiStatus = new QLabel;
	m_aiStatus->setWordWrap(true);

	auto *fun = new QGroupBox("재밌는 장면 (분위기 환기용)");
	auto *fl = new QVBoxLayout(fun);
	auto *f1 = new QHBoxLayout;
	f1->addWidget(m_btnAi);
	f1->addWidget(m_btnAiSettings);
	f1->addStretch(1);
	f1->addWidget(m_hideDrop);
	auto *f2 = new QHBoxLayout;
	f2->addWidget(m_every);
	f2->addWidget(m_btnBreaks);
	f2->addStretch(1);
	fl->addWidget(m_table, 1);
	fl->addLayout(f1);
	fl->addWidget(m_aiStatus);
	fl->addLayout(f2);

	auto *l = new QVBoxLayout(this);
	l->setContentsMargins(6, 6, 6, 6);
	l->addWidget(screenBox);
	l->addWidget(chatBox);
	auto *fr = new QHBoxLayout;
	fr->addWidget(m_btnFind);
	fr->addWidget(m_status, 1);
	l->addLayout(fr);
	l->addWidget(best);
	l->addWidget(fun, 1);

	// ── 연결 ──
	connect(m_btnChat, &QPushButton::clicked, this, [this] {
		if (m_chzzk->isRunning()) {
			m_chzzk->cancel();
			m_btnChat->setText("채팅 가져오기");
			m_chatStatus->setText("채팅 받기를 멈췄어요.");
			return;
		}
		fetchChat();
	});
	connect(m_chzzk, &ChzzkChat::info, this, [this](const QString &title, double dur) {
		m_chatStatus->setText(QString("「%1」 %2 — 채팅 받는 중…").arg(title, formatTime(dur, false)));
	});
	connect(m_chzzk, &ChzzkChat::progress, this, [this](double at, double dur, int n) {
		m_chatStatus->setText(QString("채팅 받는 중… %1개 · %2%3")
					      .arg(n)
					      .arg(formatTime(at, false))
					      .arg(dur > 0 ? QString(" / %1").arg(formatTime(dur, false)) : QString()));
	});
	connect(m_chzzk, &ChzzkChat::finished, this, [this](bool ok, const QString &msg, const QVector<ChatMessage> &c) {
		m_btnChat->setText("채팅 가져오기");
		if (ok) {
			m_chat = c;
			save();
			recompute();
		}
		m_chatStatus->setText(ok ? msg : "<span style='color:#e9a'>" + msg + "</span>");
		refreshStatus();
	});
	connect(m_offset, &QDoubleSpinBox::valueChanged, m_recalc, qOverload<>(&QTimer::start));
	connect(m_delay, &QDoubleSpinBox::valueChanged, m_recalc, qOverload<>(&QTimer::start));
	connect(m_recalc, &QTimer::timeout, this, [this] {
		if (hasSignals())
			recompute();
		save();
	});
	connect(m_btnFind, &QPushButton::clicked, this, &AiPanel::findPoints);
	connect(m_screenOn, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("ai/screenChat", on); });
	connect(m_btnChatRect, &QPushButton::clicked, this, &AiPanel::pickChatRect);
	connect(m_btnScreenScan, &QPushButton::clicked, this, [this] {
		if (m_chatScan->isRunning()) {
			m_chatScan->cancel();
			return;
		}
		scanScreenChat();
	});
	connect(m_chatScan, &KillScanner::progress, this, [this](double s, double d) {
		m_screenStatus->setText(QString("화면 채팅 분석 중… %1 / %2").arg(formatTime(s, false), formatTime(d, false)));
	});
	connect(m_chatScan, &KillScanner::finished, this, [this](bool ok, const QString &msg) {
		m_btnScreenScan->setText("화면 채팅만 분석");
		if (m_chatScan->chatMeasured() && !m_chatScan->chatLines().isEmpty())
			setScreenChat(m_chatScan->chatLines());
		else if (!ok)
			m_screenStatus->setText("<span style='color:#e9a'>" + msg + "</span>");
	});
	connect(m_audio, &AudioLevels::progress, this, [this](double s, double d) {
		m_status->setText(QString("소리 크기 분석 중… %1 / %2").arg(formatTime(s, false), formatTime(d, false)));
	});
	connect(m_audio, &AudioLevels::finished, this, [this](bool ok, const QVector<float> &db) {
		if (ok)
			m_loud = db;
		else
			m_status->setText("<span style='color:#e9a'>소리를 분석하지 못했어요 (채팅만 사용)</span>");
		save();
		if (m_wantCompute) {
			m_wantCompute = false;
			recompute();
		}
	});
	connect(m_btnFill, &QPushButton::clicked, this, [this] { emit fillRequested(m_target->value() * 60.0); });
	connect(m_btnBreaks, &QPushButton::clicked, this, [this] { emit breaksRequested(m_every->value() * 60.0); });
	connect(m_hideDrop, &QCheckBox::toggled, this, &AiPanel::refreshTable);
	connect(m_btnAiSettings, &QPushButton::clicked, this, &AiPanel::openSettings);
	connect(m_btnAi, &QPushButton::clicked, this, [this] {
		if (m_ai->isRunning()) {
			m_ai->cancel();
			m_btnAi->setText("🤖 로컬 AI로 확인");
			m_aiStatus->setText("AI 확인을 멈췄어요. 이미 끝난 후보는 그대로 남아요.");
			return;
		}
		runAi();
	});
	connect(m_ai, &LocalAi::status, m_aiStatus, &QLabel::setText);
	connect(m_ai, &LocalAi::candidateDone, this, [this](int i, const FunCandidate &c) {
		if (i < 0 || i >= m_cands.size())
			return;
		const bool added = m_cands[i].added;
		m_cands[i] = c;
		m_cands[i].added = added;
		refreshTable();
		save();
	});
	connect(m_ai, &LocalAi::finished, this, [this](bool ok, const QString &msg) {
		m_btnAi->setText("🤖 로컬 AI로 확인");
		m_aiStatus->setText(ok ? msg : "<span style='color:#e9a'>" + msg + "</span>");
		save();
	});
	connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
		if (m_filling || it->column() != 0)
			return;
		const int i = it->data(Qt::UserRole).toInt();
		emit funClipsRequested({i}, it->checkState() == Qt::Checked);
	});
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
		const QTableWidgetItem *it = m_table->item(row, 0);
		if (!it)
			return;
		const FunCandidate &c = m_cands[it->data(Qt::UserRole).toInt()];
		emit playRequested(c.clipStart(), c.clipEnd());
	});
	refreshStatus();
}

AiPanel::~AiPanel() = default;

bool AiPanel::isBusy() const
{
	return m_chzzk->isRunning() || m_audio->isRunning() || m_ai->isRunning() || m_chatScan->isRunning();
}

QRectF AiPanel::screenChatRect() const
{
	if (!m_screenOn->isChecked())
		return {};
	const QRectF r = QSettings().value("ai/chatRect", defaultChatRect()).toRectF();
	return r.isValid() ? r : defaultChatRect();
}

void AiPanel::setScreenChat(const QVector<float> &lines)
{
	m_screen = lines;
	save();
	recompute();
}

void AiPanel::scanScreenChat()
{
	if (m_path.isEmpty())
		return;
	QRectF r = screenChatRect();
	if (r.isEmpty())
		r = QSettings().value("ai/chatRect", defaultChatRect()).toRectF();
	m_chatScan->setResultDetection({});
	m_chatScan->setChatDetection(r);
	m_chatScan->start(m_path, QRectF(), m_duration, KillScanner::Precise); // 킬 영역 없이 = 채팅만
	m_btnScreenScan->setText("⏹ 멈추기");
	m_screenStatus->setText("화면 채팅 분석을 시작하는 중…");
}

void AiPanel::pickChatRect()
{
	if (m_path.isEmpty())
		return;
	const double t = m_now ? m_now() : m_duration / 2;
	// 지금 장면 한 장 (채팅이 떠 있는 장면에서 누르세요)
	QProcess ff;
	ff.start(ffmpegPath(), QStringList{"-hide_banner", "-nostdin", "-loglevel", "error", "-ss", num(t)} + inputArgs(m_path) +
				       QStringList{"-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-"});
	ff.waitForFinished(20000);
	const QImage img = QImage::fromData(ff.readAllStandardOutput(), "PNG");
	if (img.isNull()) {
		m_screenStatus->setText("<span style='color:#e9a'>장면을 가져오지 못했어요</span>");
		return;
	}
	QDialog dlg(this);
	dlg.setWindowTitle("채팅창 영역 지정");
	auto *view = new FrameView;
	view->setAspect(0);
	view->setLabel("채팅창");
	view->setImage(img);
	view->setRoi(QSettings().value("ai/chatRect", defaultChatRect()).toRectF());
	auto *help = new QLabel("채팅 말풍선이 쌓이는 곳을 끌어서 상자로 지정하세요. 말풍선 전체 폭이 들어가고, "
				"위아래로 말풍선 4~6개가 들어가면 좋아요. (재생 위치를 채팅이 떠 있는 장면에 두고 열면 편해요)");
	help->setWordWrap(true);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
	connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	connect(bb->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, view,
		[view] { view->setRoi(defaultChatRect()); });
	auto *l = new QVBoxLayout(&dlg);
	l->addWidget(help);
	l->addWidget(view, 1);
	l->addWidget(bb);
	dlg.resize(1100, 720);
	if (dlg.exec() != QDialog::Accepted)
		return;
	QSettings().setValue("ai/chatRect", view->roi());
	m_screenStatus->setText("채팅창 영역을 저장했어요. 다음 킬 찾기부터 쓰여요 (이미 분석한 영상은 [화면 채팅만 분석]).");
}

void AiPanel::setVideo(const QString &path, double duration, bool hasAudio, const QString &cacheFile)
{
	m_chzzk->cancel();
	m_audio->cancel();
	m_ai->cancel();
	m_chatScan->cancel();
	m_btnScreenScan->setText("화면 채팅만 분석");
	m_screen.clear();
	m_btnChat->setText("채팅 가져오기");
	m_btnAi->setText("🤖 로컬 AI로 확인");
	m_path = path;
	m_duration = duration;
	m_hasAudio = hasAudio;
	m_cache = cacheFile;
	m_chat.clear();
	m_loud.clear();
	m_cands.clear();
	m_sig = Signals{};
	m_chatStatus->clear();
	m_aiStatus->clear();
	m_status->clear();
	load();
	if (!m_chat.isEmpty() || !m_loud.isEmpty() || !m_screen.isEmpty())
		recompute();
	refreshTable();
	refreshStatus();
}

void AiPanel::fetchChat()
{
	const qint64 no = ChzzkChat::videoNumber(m_url->text());
	if (no <= 0) {
		m_chatStatus->setText("<span style='color:#e9a'>치지직 다시보기 주소나 번호를 넣어 주세요.</span>");
		return;
	}
	m_btnChat->setText("⏹ 멈추기");
	m_chatStatus->setText("다시보기 정보를 받는 중…");
	m_chzzk->start(no);
	save();
}

void AiPanel::findPoints()
{
	if (m_path.isEmpty())
		return;
	if (m_hasAudio && m_loud.isEmpty() && !m_audio->isRunning()) {
		m_wantCompute = true;
		m_status->setText("소리 크기 분석 중…");
		m_audio->start(m_path, m_duration);
		return;
	}
	if (m_audio->isRunning()) {
		m_wantCompute = true;
		return;
	}
	recompute();
}

void AiPanel::recompute()
{
	SignalParams p;
	p.chatOffset = m_offset->value();
	p.chatDelay = m_delay->value();
	m_sig = computeSignals(m_chat, m_screen, m_loud, m_duration, p);
	QVector<Segment> segs = m_segments ? m_segments() : QVector<Segment>{};
	const QVector<FunCandidate> old = m_cands;
	m_cands = findFunCandidates(m_sig, m_chat, p, segs, m_duration);
	carryOverFun(old, &m_cands);
	syncAdded(segs);
	refreshTable();
	refreshStatus();
	save();
	emit signalsChanged();
}

void AiPanel::syncAdded(const QVector<Segment> &segs)
{
	bool changed = false;
	for (FunCandidate &c : m_cands) {
		bool added = false;
		for (const Segment &s : segs)
			if (s.kind == Segment::Fun && s.enabled && s.start <= c.peak + 2 && s.end >= c.peak - 2)
				added = true;
		changed = changed || added != c.added;
		c.added = added;
	}
	if (changed) {
		refreshTable();
		save();
	}
}

void AiPanel::refreshStatus()
{
	QStringList parts;
	if (!m_chat.isEmpty())
		parts << QString("받은 채팅 %1개").arg(m_chat.size());
	else if (!m_screen.isEmpty())
		parts << "화면 채팅";
	else
		parts << "채팅 없음";
	if (!m_chatScan->isRunning()) {
		double lines = 0;
		for (float v : m_screen)
			lines += v;
		m_screenStatus->setText(m_screen.isEmpty()
						? QString("화면 채팅: 아직 안 잼 — 킬 찾기(정확하게)를 하면 같이 재요%1")
							  .arg(m_path.isEmpty() ? QString() : ", 이미 분석했으면 [화면 채팅만 분석]")
						: QString("화면 채팅: 잼 (새 채팅 약 %1줄)%2")
							  .arg(int(lines))
							  .arg(m_chat.isEmpty() ? QString() : " · 받은 치지직 채팅이 있어서 그걸 먼저 써요"));
	}
	if (!m_hasAudio)
		parts << "소리 없음";
	else
		parts << (m_loud.isEmpty() ? "소리 분석 전" : "소리 분석됨");
	if (hasSignals()) {
		int keep = 0, drop = 0;
		for (const FunCandidate &c : m_cands) {
			keep += c.ai == FunCandidate::Keep;
			drop += c.ai == FunCandidate::Drop;
		}
		parts << QString("재밌는 장면 후보 %1개").arg(m_cands.size());
		if (keep + drop > 0)
			parts << QString("AI: 재밌음 %1 · 아님 %2").arg(keep).arg(drop);
	}
	m_status->setText(parts.join(" · "));
	m_btnFind->setEnabled(!m_path.isEmpty());
	m_btnScreenScan->setEnabled(!m_path.isEmpty());
	m_btnChatRect->setEnabled(!m_path.isEmpty());
	m_btnBreaks->setEnabled(!m_cands.isEmpty());
	m_btnAi->setEnabled(!m_cands.isEmpty() || m_ai->isRunning());
	m_btnFill->setEnabled(!m_path.isEmpty());
}

void AiPanel::refreshTable()
{
	m_filling = true;
	QVector<int> rows;
	for (int i = 0; i < m_cands.size(); ++i)
		if (!(m_hideDrop->isChecked() && m_cands[i].ai == FunCandidate::Drop && !m_cands[i].added))
			rows.push_back(i);
	m_table->setRowCount(int(rows.size()));
	for (int r = 0; r < rows.size(); ++r) {
		const FunCandidate &c = m_cands[rows[r]];
		auto *on = new QTableWidgetItem;
		on->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
		on->setCheckState(c.added ? Qt::Checked : Qt::Unchecked);
		on->setData(Qt::UserRole, rows[r]);
		m_table->setItem(r, 0, on);
		m_table->setItem(r, 1, new QTableWidgetItem(formatTime(c.peak, false)));
		auto *sc = new QTableWidgetItem;
		sc->setData(Qt::DisplayRole, c.rankScore());
		m_table->setItem(r, 2, sc);
		QString ai;
		switch (c.ai) {
		case FunCandidate::Keep:
			ai = QString("✔ %1/10").arg(c.aiScore);
			break;
		case FunCandidate::Drop:
			ai = "✘ 아님";
			break;
		case FunCandidate::Failed:
			ai = "실패";
			break;
		default:
			ai = "-";
		}
		m_table->setItem(r, 3, new QTableWidgetItem(ai));
		auto *d = new QTableWidgetItem(c.title.isEmpty() ? c.reason : c.title + "  ·  " + c.reason);
		d->setToolTip(c.transcript.isEmpty() ? c.chatSample.mid(0, 8).join("\n") : c.transcript);
		m_table->setItem(r, 4, d);
		if (c.ai == FunCandidate::Drop)
			for (int col = 1; col < 5; ++col)
				m_table->item(r, col)->setForeground(QColor(140, 140, 140));
		else if (c.ai == FunCandidate::Keep)
			m_table->item(r, 3)->setForeground(QColor(120, 210, 120));
	}
	m_filling = false;
	refreshStatus();
}

void AiPanel::runAi()
{
	QVector<int> todo;
	for (int i = 0; i < m_cands.size(); ++i)
		if (m_cands[i].ai == FunCandidate::NotChecked || m_cands[i].ai == FunCandidate::Failed)
			todo.push_back(i);
	std::stable_sort(todo.begin(), todo.end(), [&](int a, int b) { return m_cands[a].score > m_cands[b].score; });
	if (todo.size() > kAiBatch)
		todo.resize(kAiBatch);
	if (todo.isEmpty()) {
		m_aiStatus->setText("모든 후보를 이미 확인했어요.");
		return;
	}
	m_btnAi->setText("⏹ AI 확인 멈추기");
	m_ai->start(m_path, AiConfig::load(), m_cands, todo);
}

void AiPanel::openSettings()
{
	AiConfig cfg = AiConfig::load();
	QDialog dlg(this);
	dlg.setWindowTitle("로컬 AI 설정");
	auto *wExe = new QLineEdit(cfg.whisperExe);
	auto *wModel = new QLineEdit(cfg.whisperModel);
	auto *oUrl = new QLineEdit(cfg.ollamaUrl);
	auto *oModel = new QLineEdit(cfg.ollamaModel);
	auto browse = [&dlg](QLineEdit *e, const QString &filter) {
		auto *b = new QPushButton("찾기…");
		QObject::connect(b, &QPushButton::clicked, &dlg, [&dlg, e, filter] {
			const QString f = QFileDialog::getOpenFileName(&dlg, "파일 선택", e->text(), filter);
			if (!f.isEmpty())
				e->setText(f);
		});
		auto *w = new QWidget;
		auto *h = new QHBoxLayout(w);
		h->setContentsMargins(0, 0, 0, 0);
		h->addWidget(e, 1);
		h->addWidget(b);
		return w;
	};
	auto *result = new QLabel;
	result->setWordWrap(true);
	auto *btnCheck = new QPushButton("연결 확인");
	auto *help = new QLabel(
		"<b>음성 인식 (선택)</b> — whisper.cpp 릴리스에서 CUDA용 zip(whisper-cublas-…-bin-x64.zip)을 받아 풀고 "
		"<code>whisper-cli.exe</code> 를 고르세요. 모델은 <code>ggml-large-v3-turbo-q5_0.bin</code> (약 550MB) 추천.<br>"
		"<b>판단 AI</b> — Ollama 를 설치한 뒤 명령 창에서 <code>ollama pull qwen3:8b</code>. "
		"그래픽 메모리 8GB 에 맞는 크기예요. 다른 모델 이름을 넣어도 돼요.<br>"
		"<span style='color:#888'>음성 인식을 설정하지 않으면 채팅만 보고 판단해요 (덜 정확).</span>");
	help->setWordWrap(true);
	help->setTextFormat(Qt::RichText);
	auto *form = new QFormLayout;
	form->addRow("whisper-cli.exe", browse(wExe, "whisper-cli (whisper-cli*)"));
	form->addRow("음성 인식 모델", browse(wModel, "whisper 모델 (*.bin)"));
	form->addRow("Ollama 주소", oUrl);
	form->addRow("Ollama 모델", oModel);
	auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
	auto *l = new QVBoxLayout(&dlg);
	l->addWidget(help);
	l->addLayout(form);
	auto *ch = new QHBoxLayout;
	ch->addWidget(btnCheck);
	ch->addStretch(1);
	l->addLayout(ch);
	l->addWidget(result);
	l->addWidget(bb);
	dlg.resize(620, 0);
	auto current = [&] {
		AiConfig c;
		c.whisperExe = wExe->text().trimmed();
		c.whisperModel = wModel->text().trimmed();
		c.ollamaUrl = oUrl->text().trimmed();
		c.ollamaModel = oModel->text().trimmed();
		return c;
	};
	QObject::connect(btnCheck, &QPushButton::clicked, &dlg, [&] {
		result->setText("확인 중…");
		m_ai->check(current());
	});
	const auto conn = connect(m_ai, &LocalAi::checked, &dlg, [result](bool ok, const QString &msg) {
		result->setText(QString("<pre style='color:%1'>%2</pre>").arg(ok ? "#7c7" : "#e9a", msg.toHtmlEscaped()));
	});
	QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	if (dlg.exec() == QDialog::Accepted)
		current().save();
	disconnect(conn);
}

void AiPanel::save()
{
	if (m_cache.isEmpty())
		return;
	QJsonObject o;
	o["url"] = m_url->text();
	o["offset"] = m_offset->value();
	o["delay"] = m_delay->value();
	o["chat"] = chatToJson(m_chat);
	QJsonArray loud;
	for (float v : m_loud)
		loud.append(std::round(v * 10) / 10);
	o["loud"] = loud;
	QJsonArray screen;
	for (float v : m_screen)
		screen.append(v);
	o["screen"] = screen;
	o["fun"] = funToJson(m_cands);
	o["target"] = m_target->value();
	o["every"] = m_every->value();
	QSaveFile f(m_cache);
	if (f.open(QIODevice::WriteOnly)) {
		f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
		f.commit();
	}
}

void AiPanel::load()
{
	const QSignalBlocker b1(m_offset), b2(m_delay);
	m_url->clear();
	m_chzzkBox->setChecked(false);
	m_offset->setValue(0);
	m_delay->setValue(5);
	QFile f(m_cache);
	if (m_cache.isEmpty() || !f.open(QIODevice::ReadOnly))
		return;
	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
	m_url->setText(o["url"].toString());
	m_chzzkBox->setChecked(!m_url->text().isEmpty());
	m_offset->setValue(o["offset"].toDouble());
	m_delay->setValue(o["delay"].toDouble(5));
	m_chat = chatFromJson(o["chat"].toArray());
	for (const auto &v : o["loud"].toArray())
		m_loud.push_back(float(v.toDouble()));
	for (const auto &v : o["screen"].toArray())
		m_screen.push_back(float(v.toDouble()));
	m_cands = funFromJson(o["fun"].toArray()); // AI 결과만 (recompute 가 옮겨 담음)
	m_target->setValue(o["target"].toInt(20));
	m_every->setValue(o["every"].toInt(3));
	if (!m_chat.isEmpty())
		m_chatStatus->setText(QString("저장해 둔 채팅 %1개").arg(m_chat.size()));
}
