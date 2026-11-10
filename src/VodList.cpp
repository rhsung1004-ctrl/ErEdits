#include "VodList.h"

#include "Segments.h"
#include "Util.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

VodList::VodList(QWidget *parent) : QWidget(parent)
{
	auto *help = new QLabel("여러 방송을 넣어 두고 <b>눌러서</b> 하나씩 편집해요. 분석·편집한 내용은 방송마다 저장돼요.<br>"
				"체크한 방송은 <b>목록 순서대로</b> 한 편으로 합쳐 내보낼 수 있어요 (끌어서 순서 바꾸기).");
	help->setWordWrap(true);
	m_list = new QListWidget;
	m_list->setDragDropMode(QAbstractItemView::InternalMove);
	m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	m_list->setStyleSheet("QListWidget::item { padding: 6px 4px; }");
	m_summary = new QLabel;
	m_summary->setWordWrap(true);

	m_btnAdd = new QPushButton("＋ 방송 추가…");
	m_btnAdd->setToolTip("방송 다시보기를 목록에 넣어요. 여러 파일을 고르면 각각 다른 방송으로 들어가요.\n"
			     "한 방송이 여러 파일로 나뉘어 있으면 [📂 방송 다시보기 열기]로 함께 골라 묶으세요.");
	m_btnRemove = new QPushButton("목록에서 빼기");
	m_btnRemove->setToolTip("목록에서만 빼요 (영상 파일과 분석 결과는 그대로)");
	m_btnAll = new QPushButton("🔍 모두 킬 찾기");
	m_btnAll->setToolTip("아직 끝까지 분석하지 않은 방송을 위에서부터 차례로 분석해요 (킬 찾기 → 킬 숫자 확인 → 등수 읽기).\n"
			     "킬 숫자 영역은 마지막에 쓴 프리셋을 써요.");
	m_btnCombine = new QPushButton("🎬 체크한 방송 합쳐서 내보내기…");
	m_btnCombine->setToolTip("체크한 방송들의 켜 둔 클립을 목록 순서대로 이어 한 편으로 내보내요.\n"
				 "결과 크기·페이드·소리 크기 맞추기는 [3. 내보내기] 설정을 따라요.");
	m_btnCombine->setStyleSheet("font-weight:bold; padding:6px;");

	auto *row = new QHBoxLayout;
	row->addWidget(m_btnAdd);
	row->addWidget(m_btnRemove);
	row->addStretch(1);
	row->addWidget(m_btnAll);
	auto *l = new QVBoxLayout(this);
	l->addWidget(help);
	l->addLayout(row);
	l->addWidget(m_list, 1);
	l->addWidget(m_summary);
	l->addWidget(m_btnCombine);

	connect(m_btnAdd, &QPushButton::clicked, this, &VodList::addRequested);
	connect(m_btnRemove, &QPushButton::clicked, this, [this] {
		QListWidgetItem *it = m_list->currentItem();
		if (!it)
			return;
		delete m_list->takeItem(m_list->row(it));
		save();
		updateButtons();
	});
	connect(m_btnAll, &QPushButton::clicked, this, [this] {
		if (m_batch)
			emit stopRequested();
		else
			emit analyzeAllRequested();
	});
	connect(m_btnCombine, &QPushButton::clicked, this, &VodList::combineRequested);
	connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
		const QString p = it->data(Qt::UserRole).toString();
		if (p != m_current)
			emit openRequested(p);
	});
	connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
		if (!m_loading) {
			save();
			updateButtons();
		}
	});
	connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, [this] {
		refreshAll();
		save();
	});

	// 저장된 목록
	m_loading = true;
	for (const QString &e : QSettings().value("vodList").toStringList()) {
		const QString path = e.mid(2);
		if (path.isEmpty() || (!QFileInfo::exists(path)))
			continue;
		auto *it = new QListWidgetItem;
		it->setData(Qt::UserRole, path);
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		it->setCheckState(e.startsWith("1") ? Qt::Checked : Qt::Unchecked);
		m_list->addItem(it);
	}
	m_loading = false;
	refreshAll();
}

VodList::Status VodList::readStatus(const QString &path)
{
	Status st;
	QFile f(scanCachePath(path));
	if (f.open(QIODevice::ReadOnly)) {
		const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
		st.analyzed = o["complete"].toBool();
		st.started = o["scannedUntil"].toDouble() > 0 || !o["events"].toArray().isEmpty();
		for (const KillEvent &e : eventsFromJson(o["events"].toArray()))
			st.kills += e.kill && e.check != KillEvent::Rejected ? 1 : 0;
		for (const Segment &s : segmentsFromJson(o["segments"].toArray()))
			if (s.enabled) {
				++st.clips;
				st.length += s.length();
			}
	}
	if (st.analyzed)
		st.text = QString("분석 끝 · 킬 %1 · 클립 %2개 (%3)").arg(st.kills).arg(st.clips).arg(formatTime(st.length, false));
	else if (st.started)
		st.text = QString("중간까지 분석 · 클립 %1개").arg(st.clips);
	else
		st.text = "아직 분석 안 함";
	return st;
}

QListWidgetItem *VodList::itemFor(const QString &path) const
{
	for (int i = 0; i < m_list->count(); ++i)
		if (m_list->item(i)->data(Qt::UserRole).toString() == path)
			return m_list->item(i);
	return nullptr;
}

void VodList::updateItem(QListWidgetItem *it, bool reread)
{
	const QString path = it->data(Qt::UserRole).toString();
	if (reread || !m_status.contains(path))
		m_status.insert(path, readStatus(path));
	const Status st = m_status.value(path);
	const bool cur = path == m_current;
	const bool old = m_loading;
	m_loading = true;
	it->setText(QString("%1%2. %3\n      %4")
			    .arg(cur ? "▶ " : "")
			    .arg(m_list->row(it) + 1)
			    .arg(displayName(path), st.text));
	it->setToolTip(QDir::toNativeSeparators(path));
	QFont f = it->font();
	f.setBold(cur);
	it->setFont(f);
	m_loading = old;
}

void VodList::addPath(const QString &path)
{
	if (path.isEmpty())
		return;
	if (!itemFor(path)) {
		auto *it = new QListWidgetItem;
		it->setData(Qt::UserRole, path);
		it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
		m_loading = true;
		it->setCheckState(Qt::Checked);
		m_list->addItem(it);
		m_loading = false;
		save();
	}
	refreshStatus(path);
	updateButtons();
}

void VodList::setCurrent(const QString &path)
{
	m_current = path;
	refreshAll();
	if (QListWidgetItem *it = itemFor(path))
		m_list->setCurrentItem(it);
}

void VodList::refreshStatus(const QString &path)
{
	if (QListWidgetItem *it = itemFor(path))
		updateItem(it, true);
	updateButtons();
}

void VodList::refreshAll()
{
	for (int i = 0; i < m_list->count(); ++i)
		updateItem(m_list->item(i), false);
	updateButtons();
}

QStringList VodList::paths() const
{
	QStringList out;
	for (int i = 0; i < m_list->count(); ++i)
		out << m_list->item(i)->data(Qt::UserRole).toString();
	return out;
}

QStringList VodList::checkedPaths() const
{
	QStringList out;
	for (int i = 0; i < m_list->count(); ++i)
		if (m_list->item(i)->checkState() == Qt::Checked)
			out << m_list->item(i)->data(Qt::UserRole).toString();
	return out;
}

void VodList::setBatchRunning(bool on)
{
	m_batch = on;
	updateButtons();
}

void VodList::setBusy(bool busy)
{
	m_busy = busy;
	updateButtons();
}

void VodList::save()
{
	QStringList out;
	for (int i = 0; i < m_list->count(); ++i)
		out << QString("%1|%2").arg(m_list->item(i)->checkState() == Qt::Checked ? 1 : 0).arg(
			       m_list->item(i)->data(Qt::UserRole).toString());
	QSettings().setValue("vodList", out);
}

void VodList::updateButtons()
{
	m_btnAll->setText(m_batch ? "⏹ 모두 킬 찾기 멈추기" : "🔍 모두 킬 찾기");
	m_btnAll->setEnabled(m_batch || (!m_busy && m_list->count() > 0));
	m_btnAdd->setEnabled(!m_batch);
	m_btnRemove->setEnabled(!m_batch && !m_busy);
	m_list->setEnabled(!m_batch);
	int clips = 0, vods = 0, waiting = 0;
	double len = 0;
	for (int i = 0; i < m_list->count(); ++i) {
		const Status st = m_status.value(m_list->item(i)->data(Qt::UserRole).toString());
		waiting += st.analyzed ? 0 : 1;
		if (m_list->item(i)->checkState() == Qt::Checked && st.clips > 0) {
			++vods;
			clips += st.clips;
			len += st.length;
		}
	}
	m_btnCombine->setEnabled(!m_busy && !m_batch && vods > 0);
	m_btnCombine->setText(vods > 1 ? QString("🎬 체크한 방송 %1개 합쳐서 내보내기…").arg(vods)
				       : QString("🎬 체크한 방송 합쳐서 내보내기…"));
	if (m_list->count() == 0)
		m_summary->setText("<span style='color:#999'>[＋ 방송 추가…]로 방송을 넣거나, 방송을 열면 여기에 들어와요.</span>");
	else
		m_summary->setText(QString("방송 %1개 · 분석 안 끝남 %2개 · 합치기: 방송 %3개, 클립 %4개, 약 %5")
					   .arg(m_list->count())
					   .arg(waiting)
					   .arg(vods)
					   .arg(clips)
					   .arg(formatTime(len, false)));
}
