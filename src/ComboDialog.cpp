#include "ComboDialog.h"

#include <QColorDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QPushButton *btn(const QString &text, const QString &tip = {})
{
	auto *b = new QPushButton(text);
	b->setToolTip(tip);
	b->setAutoDefault(false);
	return b;
}
} // namespace

ComboDialog::ComboDialog(const Subtitle &style, const QString &currentName, QWidget *parent)
	: QDialog(parent), m_work(ComboColors::presets()), m_style(style)
{
	setWindowTitle("스킬 콤보 색 (캐릭터별)");
	resize(860, 560);

	auto *help = new QLabel("자막에 <b>Q-F-W-E-R-E-D</b> 처럼 키를 <b>-</b> 로 이어 쓰면 키마다 아래 색으로 바뀌어요. "
				"키 하나만 있는 글자나 일반 문장은 그대로예요. 캐릭터마다 프리셋을 만들어 두고 자막에서 골라 쓰세요.");
	help->setWordWrap(true);

	m_list = new QListWidget;
	m_list->setMaximumWidth(220);
	auto *bAdd = btn("추가…", "기본 색(QWERD 주황, F 하늘)으로 새 프리셋");
	auto *bDup = btn("복제…", "지금 프리셋을 복사해서 새 프리셋");
	auto *bRen = btn("이름 바꾸기…");
	auto *bDel = btn("삭제");

	m_keys = new QTableWidget(0, 2);
	m_keys->setHorizontalHeaderLabels({"키", "색 (더블클릭해서 변경)"});
	m_keys->verticalHeader()->setVisible(false);
	m_keys->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_keys->setColumnWidth(0, 120);
	m_keys->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_keys->setSelectionMode(QAbstractItemView::SingleSelection);
	auto *bKeyAdd = btn("＋ 키 추가", "예: 평, AA, Space, W2");
	auto *bKeyDel = btn("키 삭제");
	m_dash = btn("'-' 색");
	auto *bDashWhite = btn("흰색으로");

	m_sample = new QLineEdit(QSettings().value("comboSample", "Q-F-W-E-R-E-D").toString());
	m_sample->setToolTip("미리보기 글자 (마음대로 바꿔서 확인해 보세요)");
	m_preview = new QLabel;
	m_preview->setFixedHeight(120);
	m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
	m_preview->setAlignment(Qt::AlignCenter);
	m_preview->setStyleSheet("background:#2b3a2b; border:1px solid #444;");

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText("저장");
	buttons->button(QDialogButtonBox::Cancel)->setText("취소");

	auto *leftBtns = new QHBoxLayout;
	leftBtns->addWidget(bAdd);
	leftBtns->addWidget(bDup);
	auto *leftBtns2 = new QHBoxLayout;
	leftBtns2->addWidget(bRen);
	leftBtns2->addWidget(bDel);
	auto *left = new QVBoxLayout;
	left->addWidget(new QLabel("프리셋 (캐릭터)"));
	left->addWidget(m_list, 1);
	left->addLayout(leftBtns);
	left->addLayout(leftBtns2);

	auto *keyBtns = new QHBoxLayout;
	keyBtns->addWidget(bKeyAdd);
	keyBtns->addWidget(bKeyDel);
	keyBtns->addStretch(1);
	keyBtns->addWidget(m_dash);
	keyBtns->addWidget(bDashWhite);
	auto *sampleRow = new QHBoxLayout;
	sampleRow->addWidget(new QLabel("미리보기 글자"));
	sampleRow->addWidget(m_sample, 1);
	auto *right = new QVBoxLayout;
	right->addWidget(new QLabel("키와 색"));
	m_keys->setMinimumHeight(200);
	right->addWidget(m_keys, 1);
	right->addLayout(keyBtns);
	right->addLayout(sampleRow);
	right->addWidget(m_preview);

	auto *body = new QHBoxLayout;
	body->addLayout(left);
	body->addLayout(right, 1);
	auto *lay = new QVBoxLayout(this);
	lay->addWidget(help);
	lay->addLayout(body, 1);
	lay->addWidget(buttons);

	connect(buttons, &QDialogButtonBox::accepted, this, [this] {
		storeKeys();
		ComboColors::setPresets(m_work);
		QSettings().setValue("comboSample", m_sample->text());
		accept();
	});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_list, &QListWidget::currentRowChanged, this, [this] { loadPreset(); });

	auto askName = [this](const QString &title, const QString &init) -> QString {
		bool ok = false;
		const QString name =
			QInputDialog::getText(this, title, "캐릭터(프리셋) 이름:", QLineEdit::Normal, init, &ok).trimmed();
		if (!ok || name.isEmpty())
			return {};
		for (const ComboPreset &p : m_work)
			if (p.name == name) {
				QMessageBox::information(this, title, "같은 이름의 프리셋이 이미 있어요.");
				return {};
			}
		return name;
	};
	connect(bAdd, &QPushButton::clicked, this, [this, askName] {
		const QString name = askName("프리셋 추가", "");
		if (name.isEmpty())
			return;
		storeKeys();
		ComboPreset p = ComboColors::builtinDefault();
		p.name = name;
		m_work.push_back(p);
		loadList(name);
	});
	connect(bDup, &QPushButton::clicked, this, [this, askName] {
		const int i = current();
		if (i < 0)
			return;
		const QString name = askName("프리셋 복제", m_work[i].name + " 2");
		if (name.isEmpty())
			return;
		storeKeys();
		ComboPreset p = m_work[i];
		p.name = name;
		m_work.push_back(p);
		loadList(name);
	});
	connect(bRen, &QPushButton::clicked, this, [this, askName] {
		const int i = current();
		if (i < 0)
			return;
		const QString name = askName("이름 바꾸기", m_work[i].name);
		if (name.isEmpty())
			return;
		storeKeys();
		m_work[i].name = name;
		loadList(name);
	});
	connect(bDel, &QPushButton::clicked, this, [this] {
		const int i = current();
		if (i < 0)
			return;
		if (m_work.size() <= 1) {
			QMessageBox::information(this, "삭제", "프리셋이 하나는 있어야 해요.");
			return;
		}
		if (QMessageBox::question(this, "삭제",
					  QString("'%1' 프리셋을 지울까요?\n이 프리셋을 쓰던 자막은 콤보 색이 꺼져요.")
						  .arg(m_work[i].name)) != QMessageBox::Yes)
			return;
		m_work.removeAt(i);
		loadList(m_work[std::max(0, i - 1)].name);
	});

	connect(bKeyAdd, &QPushButton::clicked, this, [this] {
		bool ok = false;
		const QString key =
			QInputDialog::getText(this, "키 추가", "키 (예: 평, AA, Space):", QLineEdit::Normal, "", &ok).trimmed();
		if (!ok || key.isEmpty())
			return;
		if (key.contains('-')) {
			QMessageBox::information(this, "키 추가", "키에는 '-' 를 넣을 수 없어요 (구분자라서).");
			return;
		}
		const int row = m_keys->rowCount();
		m_loading = true;
		m_keys->insertRow(row);
		m_keys->setItem(row, 0, new QTableWidgetItem(key));
		setKeyColorCell(row, QColor("#ffb35c"));
		m_loading = false;
		storeKeys();
		updatePreview();
	});
	connect(bKeyDel, &QPushButton::clicked, this, [this] {
		const auto rows = m_keys->selectionModel()->selectedRows();
		if (rows.isEmpty())
			return;
		m_keys->removeRow(rows.first().row());
		storeKeys();
		updatePreview();
	});
	connect(m_keys, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
		if (col != 1)
			return;
		const QColor old = m_keys->item(row, 1)->data(Qt::UserRole).value<QColor>();
		const QColor c = QColorDialog::getColor(old, this, QString("'%1' 색").arg(m_keys->item(row, 0)->text()));
		if (!c.isValid())
			return;
		setKeyColorCell(row, c);
		storeKeys();
		updatePreview();
	});
	connect(m_keys, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *it) {
		if (m_loading || it->column() != 0)
			return;
		if (it->text().contains('-')) {
			m_loading = true;
			it->setText(it->text().remove('-'));
			m_loading = false;
		}
		storeKeys();
		updatePreview();
	});
	connect(m_dash, &QPushButton::clicked, this, [this] {
		const int i = current();
		if (i < 0)
			return;
		const QColor c = QColorDialog::getColor(m_work[i].dash, this, "'-' 색");
		if (!c.isValid())
			return;
		m_work[i].dash = c;
		setColorButton(m_dash, c);
		updatePreview();
	});
	connect(bDashWhite, &QPushButton::clicked, this, [this] {
		const int i = current();
		if (i < 0)
			return;
		m_work[i].dash = Qt::white;
		setColorButton(m_dash, Qt::white);
		updatePreview();
	});
	connect(m_sample, &QLineEdit::textChanged, this, &ComboDialog::updatePreview);

	QString first = currentName;
	bool exists = false;
	for (const ComboPreset &p : m_work)
		exists = exists || p.name == first;
	if (!exists && !m_work.isEmpty())
		first = m_work.first().name;
	loadList(first);
}

void ComboDialog::showEvent(QShowEvent *e)
{
	QDialog::showEvent(e);
	updatePreview();
}

void ComboDialog::resizeEvent(QResizeEvent *e)
{
	QDialog::resizeEvent(e);
	updatePreview();
}

int ComboDialog::current() const
{
	const int i = m_list->currentRow();
	return (i >= 0 && i < m_work.size()) ? i : -1;
}

QString ComboDialog::selectedName() const
{
	const int i = current();
	return i >= 0 ? m_work[i].name : QString();
}

void ComboDialog::setColorButton(QPushButton *b, const QColor &c)
{
	const QString fg = c.lightness() > 140 ? "#000" : "#fff";
	b->setStyleSheet(QString("background:%1; color:%2; padding:3px 10px;").arg(c.name(), fg));
}

void ComboDialog::setKeyColorCell(int row, const QColor &c)
{
	auto *it = new QTableWidgetItem(c.name().toUpper());
	it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
	it->setBackground(c);
	it->setForeground(c.lightness() > 140 ? Qt::black : Qt::white);
	it->setData(Qt::UserRole, c);
	const bool was = m_loading;
	m_loading = true;
	m_keys->setItem(row, 1, it);
	m_loading = was;
}

void ComboDialog::loadList(const QString &select)
{
	m_loading = true;
	m_list->clear();
	int sel = 0;
	for (int i = 0; i < m_work.size(); ++i) {
		m_list->addItem(m_work[i].name);
		if (m_work[i].name == select)
			sel = i;
	}
	m_loading = false;
	m_list->setCurrentRow(sel);
	loadPreset();
}

void ComboDialog::loadPreset()
{
	if (m_loading)
		return;
	const int i = current();
	m_loading = true;
	m_keys->setRowCount(0);
	if (i >= 0) {
		const ComboPreset &p = m_work[i];
		m_keys->setRowCount(int(p.keys.size()));
		for (int r = 0; r < p.keys.size(); ++r) {
			m_keys->setItem(r, 0, new QTableWidgetItem(p.keys[r].key));
			setKeyColorCell(r, p.keys[r].color);
		}
		setColorButton(m_dash, p.dash);
	}
	m_loading = false;
	updatePreview();
}

void ComboDialog::storeKeys()
{
	const int i = current();
	if (i < 0 || m_loading)
		return;
	QVector<ComboKey> keys;
	for (int r = 0; r < m_keys->rowCount(); ++r) {
		auto *k = m_keys->item(r, 0);
		auto *c = m_keys->item(r, 1);
		if (!k || !c || k->text().trimmed().isEmpty())
			continue;
		keys.push_back({k->text().trimmed(), c->data(Qt::UserRole).value<QColor>()});
	}
	m_work[i].keys = keys;
}

void ComboDialog::updatePreview()
{
	const int i = current();
	if (i < 0) {
		m_preview->clear();
		return;
	}
	// 고치는 중인 색을 저장하지 않고 미리보기에만 적용
	ComboPreset cur = m_work[i];
	cur.name = QStringLiteral("__preview__");
	ComboColors::setOverride(cur);
	Subtitle s = m_style;
	s.text = m_sample->text().isEmpty() ? QStringLiteral("Q-F-W-E-R-E-D") : m_sample->text().section('\n', 0, 0);
	s.combo = cur.name;
	s.box = false;
	const int w = std::max(300, m_preview->width() - 4), h = std::max(100, m_preview->height() - 4);
	QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
	// 글자 높이가 미리보기의 약 40% 가 되도록 가상의 결과 화면 크기를 정하고, 넘치면 폭에 맞춰 줄임
	double canvasH = 0.4 * h * 1080.0 / std::max(16, s.size);
	s.posY = 0.5;
	for (int pass = 0; pass < 2; ++pass) {
		img.fill(QColor(43, 58, 43));
		QPainter p(&img);
		const QRectF r = paintSubtitle(p, QRectF(0, 0.72 * h - 0.5 * canvasH, w, canvasH), s, 1.0);
		p.end();
		if (r.width() <= w * 0.92)
			break;
		canvasH *= w * 0.92 / r.width();
	}
	ComboColors::clearOverride();
	m_preview->setPixmap(QPixmap::fromImage(img));
}
