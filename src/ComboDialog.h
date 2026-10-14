#pragma once

#include "ComboColors.h"
#include "Subtitles.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;

// 스킬 콤보 색 프리셋 편집 (캐릭터별)
//  왼쪽: 프리셋 목록 (추가·복제·이름 바꾸기·삭제), 오른쪽: 키와 색, '-' 색, 미리보기
class ComboDialog : public QDialog {
	Q_OBJECT
public:
	// style: 미리보기에 쓸 자막 모양(글꼴·크기·테두리 등), current: 처음 고를 프리셋
	ComboDialog(const Subtitle &style, const QString &current, QWidget *parent = nullptr);
	QString selectedName() const;

protected:
	void showEvent(QShowEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void loadList(const QString &select);
	void loadPreset();
	void storeKeys();
	void updatePreview();
	int current() const;
	void setColorButton(QPushButton *b, const QColor &c);
	void setKeyColorCell(int row, const QColor &c);

	QVector<ComboPreset> m_work;
	Subtitle m_style;
	bool m_loading = false;

	QListWidget *m_list;
	QTableWidget *m_keys;
	QPushButton *m_dash;
	QLineEdit *m_sample;
	QLabel *m_preview;
};
