#pragma once

#include "ChatMeter.h"
#include "Games.h"
#include "HudKillWatcher.h"
#include "ResultScreen.h"
#include "Segments.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QRectF>
#include <QVector>

// 영상 전체를 ffmpeg 로 훑으며 킬 숫자 영역(160x32 밝기)을 초당 5장씩 받아 HudKillWatcher 에 넣음
// 몇 시간짜리 다시보기도 메모리에 쌓지 않고 흘려 보내며 처리
//
// 결과 화면 찾기를 켜면 같은 장면에서 게임 화면 전체를 320x180 으로 줄인 것도 함께 받아
// rank::ResultWatcher 로 결과 화면(파란 버튼) 후보를 찾음 — 영상을 한 번만 풀어서 둘 다 처리
//   ffmpeg 출력 한 장 = 위 32줄: 킬 숫자(밝기를 RGB 로, 왼쪽 160칸) / 아래 180줄: 게임 화면 RGB
//
// 화면 채팅 보기를 켜면 방송 화면의 채팅창 영역도 192x256 밝기로 받아 ChatMeter 로 초마다 새 채팅 줄 수를 셈
//   (정확하게 방식에서만 — 키프레임만 보면 채팅이 몇 줄 밀렸는지 알 수 없음)
//   출력 한 장 = [킬 숫자 32줄] [게임 화면 180줄] [채팅 256줄], 켠 것만, 너비 320 RGB
// roi 를 비우면 킬은 안 찾고 나머지만 (이전에 분석한 영상에 화면 채팅만 더할 때)
class KillScanner : public QObject {
	Q_OBJECT
public:
	enum Mode {
		Precise, // 모든 프레임을 풀어 초당 5장 (정확, 느림)
		Fast,    // 키프레임만 풀기 (보통 2초 간격, 훨씬 빠름, 킬 시각 오차 ±2초)
	};

	explicit KillScanner(QObject *parent = nullptr);
	~KillScanner() override;

	// roi: 영상 크기 대비 0~1 비율 영역
	void start(const QString &path, const QRectF &roi, double duration, Mode mode);
	// 다음 start 부터 결과 화면 후보도 찾음 (game: 게임 화면 영역, 0~1 비율). 빈 영역이면 끔
	void setResultDetection(const QRectF &game) { m_game = game; }
	// 다음 start 부터 화면 채팅 속도도 잼 (chat: 채팅창 영역, 0~1 비율). 빈 영역이면 끔
	void setChatDetection(const QRectF &chat) { m_chat = chat; }
	// 초마다 새로 올라온 채팅 줄 수 (분석이 끝난 뒤, 화면 채팅을 켰을 때만)
	const QVector<float> &chatLines() const { return m_chatLines; }
	bool chatMeasured() const { return m_withChat; }
	void cancel();
	bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

signals:
	void progress(double seconds, double duration); // 분석한 위치
	void found(const KillEvent &e);
	void resultFound(const ResultCandidate &c); // 결과 화면 후보 (버튼이 보인 구간)
	void finished(bool ok, const QString &message);

private:
	void launch(bool hwaccel);
	void onData();
	void onFinished(int code, QProcess::ExitStatus st);

	QProcess m_proc;
	HudKillWatcher m_watcher;
	rank::ResultWatcher m_results;
	QRectF m_game;     // 비어 있으면 결과 화면 찾기 끔
	bool m_withResults = false;
	QRectF m_chat;
	bool m_withChat = false;
	bool m_withKill = true;
	ChatMeter m_chatMeter;
	QVector<float> m_chatLines;
	std::vector<uint8_t> m_chatGray;
	std::vector<uint8_t> m_gray;
	QByteArray m_buf;
	QString m_path;
	QRectF m_roi;
	double m_duration = 0;
	Mode m_mode = Precise;
	bool m_hw = true;
	bool m_cancelled = false;
	qint64 m_frames = 0;
	qint64 m_lastReportFrame = -1000;
	int m_found = 0;
	QString m_err;
};
