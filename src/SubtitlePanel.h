#pragma once

#include "Subtitles.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;

// 자막 탭: 자막 목록 + 고른 자막 편집 (글자, 시간, 글꼴·크기·색·테두리·그림자·박스·위치)
class SubtitlePanel : public QWidget {
	Q_OBJECT
public:
	explicit SubtitlePanel(QWidget *parent = nullptr);

	void setSubtitles(const QVector<Subtitle> &subs);
	const QVector<Subtitle> &subtitles() const { return m_subs; }
	int selected() const;
	void select(int index);
	void setPlayhead(double t) { m_playhead = t; }
	void setDuration(double d) { m_duration = d; }
	void setVideoLoaded(bool on);

	// 결과 영상에 안 나오는 자막(사용 클립 밖) 표시용
	void setOutsideCheck(std::function<bool(const Subtitle &)> fn);
	void refreshMarks();

	// 미리보기에서 끌어 옮길 때
	void setSelectedPosY(int index, double y);

signals:
	void changed();                     // 자막 내용이 바뀜 (저장·미리보기 갱신)
	void selectionChanged(int index);
	void seekRequested(double t);

private:
	void refreshTable();
	void loadEditor();
	void applyEditor();
	void refreshFonts(const QString &selectPath);
	void addAtPlayhead();
	void removeSelected();
	void setStartOrEnd(bool start);
	void sortKeepSelection(int index);
	void setColorButton(QPushButton *b, const QColor &c);
	Subtitle defaultStyle() const;
	void saveDefaultStyle(const Subtitle &s);

	QVector<Subtitle> m_subs;
	double m_playhead = 0;
	double m_duration = 0;
	bool m_loading = false;
	std::function<bool(const Subtitle &)> m_outside;

	QTableWidget *m_table;
	QPushButton *m_btnAdd, *m_btnDel, *m_btnStart, *m_btnEnd, *m_btnAll;
	QPlainTextEdit *m_text;
	QDoubleSpinBox *m_len;
	QComboBox *m_font;
	QPushButton *m_btnFont;
	QSpinBox *m_size, *m_outlineW;
	QPushButton *m_color, *m_outline;
	QColor m_colorVal = Qt::white, m_outlineVal = Qt::black;
	QCheckBox *m_shadow, *m_box, *m_fade;
	QSlider *m_posY;
	QWidget *m_editor;
};
