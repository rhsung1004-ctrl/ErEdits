#include "SubtitlePanel.h"

#include "FontManager.h"
#include "Util.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
QPushButton *btn(const QString &text, const QString &tip = {})
{
	auto *b = new QPushButton(text);
	b->setToolTip(tip);
	b->setAutoDefault(false);
	return b;
}
} // namespace

SubtitlePanel::SubtitlePanel(QWidget *parent) : QWidget(parent)
{
	m_table = new QTableWidget(0, 3);
	m_table->setHorizontalHeaderLabels({"시작 (원본)", "길이", "내용"});
	m_table->verticalHeader()->setDefaultSectionSize(24);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::SingleSelection);
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	m_table->setColumnWidth(0, 96);
	m_table->setColumnWidth(1, 56);
	m_table->setMinimumHeight(110);

	m_btnAdd = btn("＋ 현재 위치에 자막", "재생 위치부터 자막을 넣어요 (마지막에 쓴 모양 그대로)");
	m_btnStart = btn("시작 = 현재 위치");
	m_btnEnd = btn("끝 = 현재 위치");
	m_btnDel = btn("🗑 삭제");

	m_text = new QPlainTextEdit;
	m_text->setPlaceholderText("자막 내용 (Enter 로 줄바꿈)");
	m_text->setFixedHeight(64);

	m_len = new QDoubleSpinBox;
	m_len->setRange(0.3, 120);
	m_len->setDecimals(1);
	m_len->setSingleStep(0.5);
	m_len->setSuffix(" 초");

	m_font = new QComboBox;
	m_font->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	m_font->setMinimumContentsLength(14);
	m_btnFont = btn("글꼴 추가…", "TTF / OTF 글꼴 파일을 골라 추가해요 (여러 개 가능, 한 번 추가하면 계속 사용)");
	auto *btnFontDir = btn("📁", "추가한 글꼴이 저장된 폴더 열기");
	btnFontDir->setFixedWidth(32);

	m_size = new QSpinBox;
	m_size->setRange(16, 240);
	m_size->setSuffix(" px");
	m_size->setToolTip("1080p 기준 글자 크기");
	m_outlineW = new QSpinBox;
	m_outlineW->setRange(0, 20);
	m_outlineW->setToolTip("테두리 두께 (0 = 없음)");
	m_color = btn("글자색");
	m_outline = btn("테두리색");
	m_shadow = new QCheckBox("그림자");
	m_box = new QCheckBox("반투명 박스");
	m_fade = new QCheckBox("부드럽게 나타나기");
	m_posY = new QSlider(Qt::Horizontal);
	m_posY->setRange(5, 100);
	m_posY->setToolTip("자막 아래쪽 끝의 위치 (미리보기 화면에서 자막을 직접 끌어도 돼요)");
	m_btnAll = btn("이 모양을 모든 자막에 적용", "글꼴·크기·색·테두리·위치를 모든 자막에 똑같이");

	auto *fontRow = new QHBoxLayout;
	fontRow->addWidget(m_font, 1);
	fontRow->addWidget(m_btnFont);
	fontRow->addWidget(btnFontDir);
	auto *sizeRow = new QHBoxLayout;
	sizeRow->addWidget(new QLabel("크기"));
	sizeRow->addWidget(m_size);
	sizeRow->addWidget(new QLabel("테두리"));
	sizeRow->addWidget(m_outlineW);
	sizeRow->addWidget(m_color);
	sizeRow->addWidget(m_outline);
	sizeRow->addStretch(1);
	auto *optRow = new QHBoxLayout;
	optRow->addWidget(m_shadow);
	optRow->addWidget(m_box);
	optRow->addWidget(m_fade);
	optRow->addStretch(1);
	auto *posRow = new QHBoxLayout;
	posRow->addWidget(new QLabel("위"));
	posRow->addWidget(m_posY, 1);
	posRow->addWidget(new QLabel("아래"));

	m_editor = new QWidget;
	auto *form = new QFormLayout(m_editor);
	form->setContentsMargins(0, 0, 0, 0);
	form->addRow("내용", m_text);
	form->addRow("길이", m_len);
	form->addRow("글꼴", fontRow);
	form->addRow("모양", sizeRow);
	form->addRow("", optRow);
	form->addRow("세로 위치", posRow);
	form->addRow("", m_btnAll);

	auto *row1 = new QHBoxLayout;
	row1->addWidget(m_btnAdd);
	row1->addWidget(m_btnStart);
	row1->addWidget(m_btnEnd);
	row1->addStretch(1);
	row1->addWidget(m_btnDel);

	auto *lay = new QVBoxLayout(this);
	lay->addWidget(m_table, 1);
	lay->addLayout(row1);
	lay->addWidget(m_editor);

	refreshFonts({});
	loadEditor();
	setVideoLoaded(false);

	connect(m_btnAdd, &QPushButton::clicked, this, &SubtitlePanel::addAtPlayhead);
	connect(m_btnDel, &QPushButton::clicked, this, &SubtitlePanel::removeSelected);
	connect(m_btnStart, &QPushButton::clicked, this, [this] { setStartOrEnd(true); });
	connect(m_btnEnd, &QPushButton::clicked, this, [this] { setStartOrEnd(false); });
	connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
		loadEditor();
		emit selectionChanged(selected());
	});
	connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
		if (row >= 0 && row < m_subs.size())
			emit seekRequested(m_subs[row].start);
	});

	connect(m_text, &QPlainTextEdit::textChanged, this, &SubtitlePanel::applyEditor);
	connect(m_len, &QDoubleSpinBox::valueChanged, this, &SubtitlePanel::applyEditor);
	connect(m_font, &QComboBox::currentIndexChanged, this, &SubtitlePanel::applyEditor);
	connect(m_size, &QSpinBox::valueChanged, this, &SubtitlePanel::applyEditor);
	connect(m_outlineW, &QSpinBox::valueChanged, this, &SubtitlePanel::applyEditor);
	connect(m_shadow, &QCheckBox::toggled, this, &SubtitlePanel::applyEditor);
	connect(m_box, &QCheckBox::toggled, this, &SubtitlePanel::applyEditor);
	connect(m_fade, &QCheckBox::toggled, this, &SubtitlePanel::applyEditor);
	connect(m_posY, &QSlider::valueChanged, this, &SubtitlePanel::applyEditor);
	connect(m_color, &QPushButton::clicked, this, [this] {
		const QColor c = QColorDialog::getColor(m_colorVal, this, "글자색");
		if (c.isValid()) {
			m_colorVal = c;
			setColorButton(m_color, c);
			applyEditor();
		}
	});
	connect(m_outline, &QPushButton::clicked, this, [this] {
		const QColor c = QColorDialog::getColor(m_outlineVal, this, "테두리색");
		if (c.isValid()) {
			m_outlineVal = c;
			setColorButton(m_outline, c);
			applyEditor();
		}
	});
	connect(m_btnFont, &QPushButton::clicked, this, [this] {
		const QStringList files = QFileDialog::getOpenFileNames(this, "글꼴 추가", QSettings().value("fontDir").toString(),
									"글꼴 (*.ttf *.otf *.ttc)");
		if (files.isEmpty())
			return;
		QSettings().setValue("fontDir", QFileInfo(files.first()).absolutePath());
		QString last;
		QStringList errors;
		for (const QString &f : files) {
			QString err;
			const QString path = FontManager::addFontFile(f, &err);
			if (path.isEmpty())
				errors << QFileInfo(f).fileName() + ": " + err;
			else
				last = path;
		}
		refreshFonts(last.isEmpty() ? m_font->currentData().toString() : last);
		if (!last.isEmpty())
			applyEditor(); // 고른 자막에 새 글꼴 적용
		if (!errors.isEmpty())
			QMessageBox::warning(this, "글꼴 추가", errors.join('\n'));
	});
	connect(btnFontDir, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl::fromLocalFile(FontManager::fontsDir())); });
	connect(m_btnAll, &QPushButton::clicked, this, [this] {
		const int i = selected();
		if (i < 0 || m_subs.size() < 2)
			return;
		if (QMessageBox::question(this, "모든 자막에 적용",
					  QString("자막 %1개 모두를 지금 자막과 같은 모양으로 바꿀까요? (내용·시간은 그대로)")
						  .arg(m_subs.size())) != QMessageBox::Yes)
			return;
		const Subtitle style = m_subs[i];
		for (Subtitle &s : m_subs)
			s.copyStyleFrom(style);
		emit changed();
	});
}

void SubtitlePanel::setOutsideCheck(std::function<bool(const Subtitle &)> fn)
{
	m_outside = std::move(fn);
}

void SubtitlePanel::refreshMarks()
{
	for (int i = 0; i < m_subs.size() && i < m_table->rowCount(); ++i) {
		const bool out = m_outside && m_outside(m_subs[i]);
		for (int c = 0; c < 3; ++c)
			if (auto *it = m_table->item(i, c)) {
				it->setForeground(out ? QColor(150, 150, 150) : palette().color(QPalette::Text));
				it->setToolTip(out ? "사용하는 클립 밖에 있어서 결과 영상에 나오지 않아요" : QString());
			}
	}
}

void SubtitlePanel::setVideoLoaded(bool on)
{
	m_btnAdd->setEnabled(on);
}

void SubtitlePanel::setColorButton(QPushButton *b, const QColor &c)
{
	const QString fg = c.lightness() > 140 ? "#000" : "#fff";
	b->setStyleSheet(QString("background:%1; color:%2; padding:3px 8px;").arg(c.name(), fg));
}

void SubtitlePanel::refreshFonts(const QString &selectPath)
{
	const bool was = m_loading;
	m_loading = true;
	m_font->clear();
	m_font->addItem(FontManager::displayName({}), QString());
	m_font->setItemData(0, FontManager::qfont({}, 14), Qt::FontRole);
	for (const FontManager::Entry &e : FontManager::fonts()) {
		m_font->addItem(e.label, e.path);
		m_font->setItemData(m_font->count() - 1, FontManager::qfont(e.path, 14), Qt::FontRole);
	}
	const int idx = m_font->findData(selectPath);
	m_font->setCurrentIndex(std::max(0, idx));
	m_loading = was;
}

Subtitle SubtitlePanel::defaultStyle() const
{
	Subtitle s;
	const QJsonObject o = QJsonDocument::fromJson(QSettings().value("subtitleStyle").toByteArray()).object();
	subtitleStyleFromJson(o, &s);
	if (!s.font.isEmpty() && !QFileInfo::exists(s.font))
		s.font.clear();
	return s;
}

void SubtitlePanel::saveDefaultStyle(const Subtitle &s)
{
	QSettings().setValue("subtitleStyle", QJsonDocument(subtitleStyleToJson(s)).toJson(QJsonDocument::Compact));
}

void SubtitlePanel::setSubtitles(const QVector<Subtitle> &subs)
{
	m_subs = subs;
	refreshTable();
	loadEditor();
}

int SubtitlePanel::selected() const
{
	const auto rows = m_table->selectionModel()->selectedRows();
	return rows.isEmpty() ? -1 : rows.first().row();
}

void SubtitlePanel::select(int index)
{
	if (index >= 0 && index < m_table->rowCount()) {
		m_table->selectRow(index);
		m_table->scrollToItem(m_table->item(index, 0));
	} else {
		m_table->clearSelection();
	}
}

void SubtitlePanel::refreshTable()
{
	const int keep = selected();
	const bool was = m_loading;
	m_loading = true;
	m_table->setRowCount(int(m_subs.size()));
	for (int i = 0; i < m_subs.size(); ++i) {
		const Subtitle &s = m_subs[i];
		m_table->setItem(i, 0, new QTableWidgetItem(formatTime(s.start)));
		m_table->setItem(i, 1, new QTableWidgetItem(QString::number(s.length(), 'f', 1) + "s"));
		QString t = s.text.section('\n', 0, 0);
		if (s.text.contains('\n'))
			t += " …";
		m_table->setItem(i, 2, new QTableWidgetItem(t.isEmpty() ? "(빈 자막)" : t));
	}
	if (keep >= 0 && keep < m_subs.size())
		m_table->selectRow(keep);
	m_loading = was;
	refreshMarks();
}

void SubtitlePanel::loadEditor()
{
	const int i = selected();
	const bool has = i >= 0 && i < m_subs.size();
	m_editor->setEnabled(has);
	m_btnDel->setEnabled(has);
	m_btnStart->setEnabled(has);
	m_btnEnd->setEnabled(has);
	m_btnAll->setEnabled(has && m_subs.size() > 1);
	const Subtitle s = has ? m_subs[i] : defaultStyle();
	m_loading = true;
	if (m_text->toPlainText() != s.text || !has)
		m_text->setPlainText(has ? s.text : QString());
	m_len->setValue(has ? s.length() : 3.0);
	refreshFonts(s.font);
	m_size->setValue(s.size);
	m_outlineW->setValue(s.outlineWidth);
	m_colorVal = s.color;
	m_outlineVal = s.outline;
	setColorButton(m_color, s.color);
	setColorButton(m_outline, s.outline);
	m_shadow->setChecked(s.shadow);
	m_box->setChecked(s.box);
	m_fade->setChecked(s.fade);
	m_posY->setValue(int(std::lround(s.posY * 100)));
	m_loading = false;
}

void SubtitlePanel::applyEditor()
{
	if (m_loading)
		return;
	const int i = selected();
	if (i < 0 || i >= m_subs.size())
		return;
	Subtitle &s = m_subs[i];
	s.text = m_text->toPlainText();
	s.end = s.start + m_len->value();
	if (m_duration > 0)
		s.end = std::min(s.end, m_duration);
	s.font = m_font->currentData().toString();
	s.size = m_size->value();
	s.outlineWidth = m_outlineW->value();
	s.color = m_colorVal;
	s.outline = m_outlineVal;
	s.shadow = m_shadow->isChecked();
	s.box = m_box->isChecked();
	s.fade = m_fade->isChecked();
	s.posY = m_posY->value() / 100.0;
	saveDefaultStyle(s);
	// 목록 글자만 갱신 (편집 중인 칸의 커서를 건드리지 않도록 표 전체는 다시 만들지 않음)
	m_loading = true;
	if (auto *it = m_table->item(i, 1))
		it->setText(QString::number(s.length(), 'f', 1) + "s");
	if (auto *it = m_table->item(i, 2)) {
		QString t = s.text.section('\n', 0, 0);
		if (s.text.contains('\n'))
			t += " …";
		it->setText(t.isEmpty() ? "(빈 자막)" : t);
	}
	m_loading = false;
	emit changed();
}

void SubtitlePanel::setSelectedPosY(int index, double y)
{
	if (index < 0 || index >= m_subs.size())
		return;
	m_subs[index].posY = y;
	if (index == selected()) {
		m_loading = true;
		m_posY->setValue(int(std::lround(y * 100)));
		m_loading = false;
	}
	saveDefaultStyle(m_subs[index]);
	emit changed();
}

void SubtitlePanel::sortKeepSelection(int index)
{
	if (index < 0 || index >= m_subs.size())
		return;
	const Subtitle target = m_subs[index];
	std::stable_sort(m_subs.begin(), m_subs.end(),
			 [](const Subtitle &a, const Subtitle &b) { return a.start < b.start; });
	int newIndex = 0;
	for (int k = 0; k < m_subs.size(); ++k)
		if (m_subs[k].start == target.start && m_subs[k].text == target.text && m_subs[k].end == target.end) {
			newIndex = k;
			break;
		}
	m_loading = true;
	refreshTable();
	m_loading = false;
	select(newIndex);
}

void SubtitlePanel::addAtPlayhead()
{
	Subtitle s = defaultStyle();
	s.start = m_playhead;
	s.end = m_playhead + 3.0;
	if (m_duration > 0) {
		s.end = std::min(s.end, m_duration);
		if (s.end - s.start < 0.3)
			s.start = std::max(0.0, s.end - 3.0);
	}
	s.text = "";
	m_subs.push_back(s);
	sortKeepSelection(int(m_subs.size()) - 1);
	loadEditor();
	m_text->setFocus();
	emit changed();
}

void SubtitlePanel::removeSelected()
{
	const int i = selected();
	if (i < 0 || i >= m_subs.size())
		return;
	m_subs.removeAt(i);
	refreshTable();
	select(std::min(i, int(m_subs.size()) - 1));
	loadEditor();
	emit changed();
}

void SubtitlePanel::setStartOrEnd(bool start)
{
	const int i = selected();
	if (i < 0 || i >= m_subs.size())
		return;
	Subtitle &s = m_subs[i];
	if (start) {
		const double len = s.length();
		s.start = m_playhead; // 길이는 그대로 두고 옮김
		s.end = s.start + len;
		if (m_duration > 0)
			s.end = std::min(s.end, m_duration);
	} else {
		if (m_playhead <= s.start + 0.3) {
			QMessageBox::information(this, "자막 끝", "끝은 자막 시작보다 뒤여야 해요.");
			return;
		}
		s.end = m_playhead;
	}
	sortKeepSelection(i);
	loadEditor();
	emit changed();
}
