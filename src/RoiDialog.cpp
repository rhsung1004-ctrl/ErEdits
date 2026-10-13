#include "RoiDialog.h"

#include "HudKillWatcher.h"
#include "Util.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr double kAspect = double(HudKillWatcher::kW) / HudKillWatcher::kH; // 5:1
}

// ───────────────────────── FrameView ─────────────────────────

FrameView::FrameView(QWidget *parent) : QWidget(parent)
{
	setMinimumSize(640, 360);
	setFocusPolicy(Qt::StrongFocus);
	setCursor(Qt::CrossCursor);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FrameView::setImage(const QImage &img)
{
	m_img = img;
	update();
}

void FrameView::setRoi(const QRectF &roi)
{
	m_roi = roi;
	update();
}

QRectF FrameView::imageRect() const
{
	if (m_img.isNull())
		return QRectF(rect());
	const QSizeF s = QSizeF(m_img.size()).scaled(QSizeF(size()), Qt::KeepAspectRatio);
	return QRectF(QPointF((width() - s.width()) / 2, (height() - s.height()) / 2), s);
}

QPointF FrameView::toImage(const QPointF &p) const
{
	const QRectF ir = imageRect();
	if (m_img.isNull() || ir.width() <= 0)
		return {};
	return QPointF((p.x() - ir.x()) * m_img.width() / ir.width(), (p.y() - ir.y()) * m_img.height() / ir.height());
}

QRectF FrameView::roiPixels() const
{
	const double W = m_img.width(), H = m_img.height();
	return QRectF(m_roi.x() * W, m_roi.y() * H, m_roi.width() * W, m_roi.height() * H);
}

void FrameView::setRoiPixels(QRectF r)
{
	const double W = m_img.width(), H = m_img.height();
	if (W <= 0 || H <= 0)
		return;
	// 크기 제한 (숫자가 너무 작거나 크면 인식 불가)
	double w = std::clamp(r.width(), 40.0, W);
	double h = w / kAspect;
	if (h > H) {
		h = H;
		w = h * kAspect;
	}
	r.setSize(QSizeF(w, h));
	r.moveLeft(std::clamp(r.left(), 0.0, W - w));
	r.moveTop(std::clamp(r.top(), 0.0, H - h));
	m_roi = QRectF(r.x() / W, r.y() / H, r.width() / W, r.height() / H);
	update();
	emit roiChanged(m_roi);
}

void FrameView::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.fillRect(rect(), QColor(20, 20, 24));
	if (m_img.isNull()) {
		p.setPen(Qt::gray);
		p.drawText(rect(), Qt::AlignCenter, "장면 불러오는 중…");
		return;
	}
	const QRectF ir = imageRect();
	p.setRenderHint(QPainter::SmoothPixmapTransform);
	p.drawImage(ir, m_img);
	const QRectF r = roiPixels();
	const double sx = ir.width() / m_img.width(), sy = ir.height() / m_img.height();
	const QRectF wr(ir.x() + r.x() * sx, ir.y() + r.y() * sy, r.width() * sx, r.height() * sy);
	// 영역 밖을 살짝 어둡게
	QPainterPath outside;
	outside.addRect(ir);
	QPainterPath inside;
	inside.addRect(wr);
	p.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 110));
	p.setPen(QPen(QColor(255, 196, 0), 2));
	p.drawRect(wr);
	p.setPen(QColor(255, 196, 0));
	// 위에 자리가 없으면 상자 아래에 글씨
	const double ty = wr.top() - ir.top() >= 18 ? wr.top() - 6 : wr.bottom() + 16;
	p.drawText(QPointF(std::min(wr.left(), double(width() - 90)), ty), "킬 숫자 영역");
}

void FrameView::mousePressEvent(QMouseEvent *e)
{
	if (e->button() != Qt::LeftButton || m_img.isNull())
		return;
	const QPointF ip = toImage(e->position());
	m_startRoi = roiPixels();
	m_anchor = ip;
	m_drag = m_startRoi.contains(ip) ? Move : Draw;
}

void FrameView::mouseMoveEvent(QMouseEvent *e)
{
	if (m_drag == None)
		return;
	const QPointF ip = toImage(e->position());
	if (m_drag == Move) {
		setRoiPixels(m_startRoi.translated(ip - m_anchor));
		return;
	}
	// 가로 길이로 크기를 정하고 세로는 5:1 로 맞춤
	const double w = std::abs(ip.x() - m_anchor.x());
	if (w < 8)
		return;
	const double h = w / kAspect;
	const double x = ip.x() < m_anchor.x() ? m_anchor.x() - w : m_anchor.x();
	const double y = ip.y() < m_anchor.y() ? m_anchor.y() - h : m_anchor.y();
	setRoiPixels(QRectF(x, y, w, h));
}

void FrameView::mouseReleaseEvent(QMouseEvent *)
{
	m_drag = None;
}

void FrameView::wheelEvent(QWheelEvent *e)
{
	if (m_img.isNull())
		return;
	// 휠: 가운데를 기준으로 크기 미세 조정
	const QRectF r = roiPixels();
	const double f = e->angleDelta().y() > 0 ? 1.03 : 1 / 1.03;
	const QPointF c = r.center();
	const double w = r.width() * f, h = w / kAspect;
	setRoiPixels(QRectF(c.x() - w / 2, c.y() - h / 2, w, h));
}

void FrameView::keyPressEvent(QKeyEvent *e)
{
	QPointF d;
	switch (e->key()) {
	case Qt::Key_Left: d = {-1, 0}; break;
	case Qt::Key_Right: d = {1, 0}; break;
	case Qt::Key_Up: d = {0, -1}; break;
	case Qt::Key_Down: d = {0, 1}; break;
	default: QWidget::keyPressEvent(e); return;
	}
	if (e->modifiers() & Qt::ShiftModifier)
		d *= 5;
	setRoiPixels(roiPixels().translated(d));
}

// ───────────────────────── RoiDialog ─────────────────────────

RoiDialog::RoiDialog(const QString &videoPath, double duration, int videoW, int videoH, const QRectF &roi,
		     const QString &presetName, double startTime, QWidget *parent)
	: QDialog(parent), m_path(videoPath), m_duration(duration), m_vw(videoW), m_vh(videoH)
{
	setWindowTitle("킬 숫자 영역 지정");
	resize(1100, 820);

	auto *help = new QLabel(
		"게임 화면 오른쪽 위 <b>TK · K · A 숫자 줄</b>을 노란 상자에 딱 맞게 맞춰 주세요. "
		"(빈 곳을 끌면 새로 그리기 · 상자 안을 끌면 이동 · 휠로 크기 · 방향키로 1칸 이동)<br>"
		"아래 슬라이더로 <b>게임 중인 장면</b>을 찾은 뒤 맞추세요. 아래 확대 화면에 숫자 3개가 초록 상자로 잡히면 성공이에요.");
	help->setWordWrap(true);

	m_view = new FrameView;
	m_view->setRoi(roi);

	m_slider = new QSlider(Qt::Horizontal);
	m_slider->setRange(0, std::max(1, int(duration)));
	m_slider->setValue(int(std::clamp(startTime, 0.0, duration)));
	m_time = new QLabel(formatTime(m_slider->value(), false));
	m_time->setMinimumWidth(80);

	m_probeImg = new QLabel;
	m_probeImg->setFixedSize(HudKillWatcher::kW * 4, HudKillWatcher::kH * 4);
	m_probeImg->setStyleSheet("background:#111; border:1px solid #444;");
	m_probeText = new QLabel;
	m_probeText->setWordWrap(true);

	m_preset = new QComboBox;
	m_preset->setEditable(false);
	m_preset->addItem("(저장 안 함)");
	m_preset->addItems(presetNames());
	if (!presetName.isEmpty())
		m_preset->setCurrentText(presetName);
	auto *btnLoad = new QPushButton("불러오기");
	auto *btnSave = new QPushButton("현재 영역 저장…");
	auto *btnDel = new QPushButton("삭제");
	auto *btnDefault = new QPushButton("기본 위치 (화면 전체가 게임일 때)");

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText("이 영역 사용");
	buttons->button(QDialogButtonBox::Cancel)->setText("취소");

	auto *timeRow = new QHBoxLayout;
	timeRow->addWidget(new QLabel("장면 위치"));
	timeRow->addWidget(m_slider, 1);
	timeRow->addWidget(m_time);

	auto *probeRow = new QHBoxLayout;
	probeRow->addWidget(m_probeImg);
	probeRow->addWidget(m_probeText, 1);

	auto *presetRow = new QHBoxLayout;
	presetRow->addWidget(new QLabel("스트리머 프리셋"));
	presetRow->addWidget(m_preset, 1);
	presetRow->addWidget(btnLoad);
	presetRow->addWidget(btnSave);
	presetRow->addWidget(btnDel);
	presetRow->addSpacing(20);
	presetRow->addWidget(btnDefault);

	auto *lay = new QVBoxLayout(this);
	lay->addWidget(help);
	lay->addWidget(m_view, 1);
	lay->addLayout(timeRow);
	lay->addLayout(probeRow);
	lay->addLayout(presetRow);
	lay->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_view, &FrameView::roiChanged, this, &RoiDialog::updateProbe);
	connect(btnDefault, &QPushButton::clicked, this, [this] {
		m_view->setRoi(defaultKillRoi(m_vw, m_vh));
		updateProbe();
	});
	connect(btnLoad, &QPushButton::clicked, this, [this] {
		QRectF r;
		if (loadPreset(m_preset->currentText(), &r)) {
			m_view->setRoi(r);
			updateProbe();
		}
	});
	connect(m_preset, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		QRectF r;
		if (loadPreset(name, &r)) {
			m_view->setRoi(r);
			updateProbe();
		}
	});
	connect(btnSave, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString cur = m_preset->currentIndex() > 0 ? m_preset->currentText() : QString();
		const QString name =
			QInputDialog::getText(this, "프리셋 저장", "스트리머 이름 (다음부터 이 이름으로 불러와요):",
					      QLineEdit::Normal, cur, &ok)
				.trimmed();
		if (!ok || name.isEmpty())
			return;
		QString key = name;
		key.replace('/', '_').replace('\\', '_');
		savePreset(key, m_view->roi());
		if (m_preset->findText(key) < 0)
			m_preset->addItem(key);
		m_preset->setCurrentText(key);
	});
	connect(btnDel, &QPushButton::clicked, this, [this] {
		if (m_preset->currentIndex() <= 0)
			return;
		const QString name = m_preset->currentText();
		if (QMessageBox::question(this, "프리셋 삭제", QString("'%1' 프리셋을 지울까요?").arg(name)) !=
		    QMessageBox::Yes)
			return;
		deletePreset(name);
		m_preset->removeItem(m_preset->currentIndex());
		m_preset->setCurrentIndex(0);
	});

	// 슬라이더를 움직이면 잠깐 뒤에 그 장면을 가져옴
	m_grabDelay.setSingleShot(true);
	m_grabDelay.setInterval(250);
	connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
		m_time->setText(formatTime(v, false));
		m_grabDelay.start();
	});
	connect(&m_grabDelay, &QTimer::timeout, this, [this] { requestFrame(m_slider->value()); });
	connect(&m_grab, &QProcess::readyReadStandardOutput, this, [this] { m_grabBuf += m_grab.readAllStandardOutput(); });
	connect(&m_grab, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
		m_grabBuf += m_grab.readAllStandardOutput();
		QImage img;
		if (img.loadFromData(m_grabBuf, "PNG")) {
			m_view->setImage(img);
			updateProbe();
		}
		m_grabBuf.clear();
		if (m_pendingT >= 0) {
			const double t = m_pendingT;
			m_pendingT = -1;
			requestFrame(t);
		}
	});

	requestFrame(m_slider->value());
	updateProbe();
}

void RoiDialog::requestFrame(double t)
{
	if (m_grab.state() != QProcess::NotRunning) {
		m_pendingT = t;
		return;
	}
	m_grabBuf.clear();
	setQuietProcess(m_grab, false);
	m_grab.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-ss", QString::number(t, 'f', 2),
				    "-i", m_path, "-frames:v", "1", "-an", "-f", "image2pipe", "-c:v", "png", "-"});
}

void RoiDialog::updateProbe()
{
	const QImage &img = m_view->image();
	if (img.isNull()) {
		m_probeText->setText("장면을 불러오는 중…");
		return;
	}
	const QRectF r = m_view->roi();
	const QRect px(int(std::lround(r.x() * img.width())), int(std::lround(r.y() * img.height())),
		       std::max(1, int(std::lround(r.width() * img.width()))),
		       std::max(1, int(std::lround(r.height() * img.height()))));
	// 분석 때와 같은 크기(160x32)의 밝기 영상으로 줄여서 감지기에 넣어 봄
	const QImage small = img.copy(px)
				     .scaled(HudKillWatcher::kW, HudKillWatcher::kH, Qt::IgnoreAspectRatio,
					     Qt::SmoothTransformation)
				     .convertToFormat(QImage::Format_Grayscale8);
	std::vector<uint8_t> gray(size_t(HudKillWatcher::kW * HudKillWatcher::kH));
	for (int y = 0; y < HudKillWatcher::kH; ++y)
		memcpy(&gray[size_t(y * HudKillWatcher::kW)], small.constScanLine(y), HudKillWatcher::kW);
	std::vector<std::array<int, 4>> boxes;
	const bool ok = HudKillWatcher::probe(gray.data(), &boxes);

	// 확대 화면: 감지기가 "숫자(밝은 부분)"로 보는 곳을 노랗게
	constexpr int Z = 4;
	QImage view(HudKillWatcher::kW * Z, HudKillWatcher::kH * Z, QImage::Format_RGB32);
	for (int y = 0; y < view.height(); ++y) {
		auto *line = reinterpret_cast<QRgb *>(view.scanLine(y));
		for (int x = 0; x < view.width(); ++x) {
			const int g = gray[size_t((y / Z) * HudKillWatcher::kW + x / Z)];
			line[x] = g > 190 ? qRgb(255, 210, 60) : qRgb(g / 2, g / 2, g / 2);
		}
	}
	QPainter p(&view);
	p.setPen(QPen(ok ? QColor(60, 220, 90) : QColor(230, 80, 80), 2));
	for (const auto &b : boxes)
		p.drawRect(QRect(b[0] * Z - 2, b[1] * Z - 2, (b[2] - b[0]) * Z + 3, (b[3] - b[1]) * Z + 3));
	p.end();
	m_probeImg->setPixmap(QPixmap::fromImage(view));
	m_probeText->setText(
		ok ? "<b style='color:#3c3'>✓ 숫자 3개(TK·K·A)를 찾았어요.</b> 이 위치로 분석하면 돼요."
		   : "<b style='color:#e55'>✗ 숫자 3개를 못 찾았어요.</b> 게임 중인 장면인지 확인하고, 상자를 숫자 줄에 "
		     "맞춰 보세요. 숫자가 화면 높이에 비해 너무 작게 나오는 방송이면 상자를 숫자 줄에 바짝 붙여 그려 주세요.");
}

QRectF RoiDialog::roi() const
{
	return m_view->roi();
}

QString RoiDialog::presetName() const
{
	return m_preset->currentIndex() > 0 ? m_preset->currentText() : QString();
}

QStringList RoiDialog::presetNames()
{
	QSettings s;
	s.beginGroup("roiPresets");
	QStringList names = s.childKeys();
	s.endGroup();
	names.sort(Qt::CaseInsensitive);
	return names;
}

bool RoiDialog::loadPreset(const QString &name, QRectF *roi)
{
	if (name.isEmpty())
		return false;
	QSettings s;
	const QVariant v = s.value("roiPresets/" + name);
	if (!v.isValid())
		return false;
	const QRectF r = v.toRectF();
	if (r.width() <= 0 || r.height() <= 0)
		return false;
	*roi = r;
	return true;
}

void RoiDialog::savePreset(const QString &name, const QRectF &roi)
{
	QString key = name;
	key.replace('/', '_').replace('\\', '_');
	QSettings s;
	s.setValue("roiPresets/" + key, roi);
}

void RoiDialog::deletePreset(const QString &name)
{
	QSettings s;
	s.remove("roiPresets/" + name);
}
