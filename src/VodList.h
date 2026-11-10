#pragma once

#include <QHash>
#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// 방송 목록: 여러 방송 다시보기를 넣어 두고 골라 가며 편집
//  - 누르면 그 방송을 열어 편집 (분석·편집 결과는 방송마다 따로 저장돼 있어서 그대로 이어짐)
//  - [모두 킬 찾기]: 아직 분석하지 않은 방송을 차례로 분석
//  - 체크한 방송의 클립을 목록 순서대로 한 편으로 합쳐 내보내기 (끌어서 순서 바꾸기)
//  - 목록은 프로그램을 다시 켜도 남음
class VodList : public QWidget {
	Q_OBJECT
public:
	explicit VodList(QWidget *parent = nullptr);

	struct Status {
		bool analyzed = false;  // 끝까지 분석함
		bool started = false;   // 조금이라도 분석함
		int kills = 0;
		int clips = 0;          // 켜 둔 클립
		double length = 0;      // 켜 둔 클립 길이 합
		QString text;
	};
	static Status readStatus(const QString &path);

	void addPath(const QString &path);
	void setCurrent(const QString &path);
	void refreshStatus(const QString &path);
	void refreshAll();
	QStringList paths() const;
	QStringList checkedPaths() const; // 합치기에 넣을 방송 (목록 순서)
	void setBatchRunning(bool on);
	void setBusy(bool busy);

signals:
	void openRequested(const QString &path);
	void addRequested();
	void analyzeAllRequested();
	void stopRequested();
	void combineRequested();

private:
	QListWidgetItem *itemFor(const QString &path) const;
	void updateItem(QListWidgetItem *it, bool reread);
	void save();
	void updateButtons();

	QListWidget *m_list;
	QLabel *m_summary;
	QPushButton *m_btnAdd, *m_btnRemove, *m_btnAll, *m_btnCombine;
	QString m_current;
	QHash<QString, Status> m_status; // 마지막으로 읽은 상태
	bool m_batch = false;
	bool m_busy = false;
	bool m_loading = false;
};
