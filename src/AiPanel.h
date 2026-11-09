#pragma once

#include "Highlights.h"
#include "LocalAi.h"

#include <QWidget>

#include <functional>

class AudioLevels;
class KillScanner;
class ChzzkChat;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

// AI 편집점 탭
//  - 치지직 다시보기 채팅 + 소리 크기 → 킬 클립 점수(잘한 장면), 재밌는 장면 후보
//  - 로컬 AI(whisper.cpp + Ollama)로 후보 확인
// 클립 목록은 메인 창이 갖고, 이 탭은 넣기·분량 맞추기를 요청만 함
class AiPanel : public QWidget {
	Q_OBJECT
public:
	explicit AiPanel(QWidget *parent = nullptr);
	~AiPanel() override;

	void setVideo(const QString &path, double duration, bool hasAudio, const QString &cacheFile);
	void setSegmentsProvider(std::function<QVector<Segment>()> f) { m_segments = std::move(f); }
	void setTimeProvider(std::function<double()> f) { m_now = std::move(f); }
	// 화면 채팅: 킬 찾기 때 같이 잴지, 채팅창 영역(0~1 비율, 끄면 빈 영역)
	QRectF screenChatRect() const;
	// 킬 찾기가 잰 초마다 새 채팅 줄 수
	void setScreenChat(const QVector<float> &lines);
	bool hasSignals() const { return m_sig.hasChat || m_sig.hasAudio; }
	static QRectF defaultChatRect() { return QRectF(0.735, 0.24, 0.265, 0.36); }
	const Signals &sig() const { return m_sig; }
	const QVector<FunCandidate> &candidates() const { return m_cands; }
	// 메인 창 클립이 바뀐 뒤: 넣음 표시 맞추기
	void syncAdded(const QVector<Segment> &segs);
	bool isBusy() const;

signals:
	void signalsChanged(); // 점수 다시 매기기
	void funClipsRequested(const QVector<int> &indices, bool add);
	void fillRequested(double seconds);
	void breaksRequested(double every);
	void playRequested(double from, double to);

private:
	void fetchChat();
	void pickChatRect();
	void scanScreenChat();
	void findPoints();
	void recompute();
	void runAi();
	void openSettings();
	void refreshTable();
	void refreshStatus();
	void save();
	void load();

	QString m_path, m_cache;
	double m_duration = 0;
	bool m_hasAudio = false;
	std::function<QVector<Segment>()> m_segments;
	std::function<double()> m_now;
	QVector<float> m_screen; // 화면 채팅 (초마다 새 줄 수)
	KillScanner *m_chatScan;

	QVector<ChatMessage> m_chat;
	QVector<float> m_loud;
	Signals m_sig;
	QVector<FunCandidate> m_cands;
	bool m_wantCompute = false; // 소리 분석이 끝나면 계산

	ChzzkChat *m_chzzk;
	AudioLevels *m_audio;
	LocalAi *m_ai;
	QTimer *m_recalc;

	QCheckBox *m_screenOn;
	QPushButton *m_btnChatRect;
	QPushButton *m_btnScreenScan;
	QLabel *m_screenStatus;
	class QGroupBox *m_chzzkBox;
	QLineEdit *m_url;
	QPushButton *m_btnChat;
	QDoubleSpinBox *m_offset;
	QDoubleSpinBox *m_delay;
	QLabel *m_chatStatus;
	QPushButton *m_btnFind;
	QLabel *m_status;
	QSpinBox *m_target;
	QPushButton *m_btnFill;
	QTableWidget *m_table;
	QCheckBox *m_hideDrop;
	QPushButton *m_btnAi;
	QPushButton *m_btnAiSettings;
	QSpinBox *m_every;
	QPushButton *m_btnBreaks;
	QLabel *m_aiStatus;
	bool m_filling = false;
};
