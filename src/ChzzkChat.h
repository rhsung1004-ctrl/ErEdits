#pragma once

#include "Highlights.h"

#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

// 치지직 다시보기 채팅 가져오기 (비공식 웹 API — 치지직 웹 플레이어가 쓰는 주소)
//  GET https://api.chzzk.naver.com/service/v1/videos/{번호}/chats?playerMessageTime={ms}&previousVideoChatSize=50
//  → content.videoChats[] (playerMessageTime, content), content.nextPlayerMessageTime 으로 다음 장
class ChzzkChat : public QObject {
	Q_OBJECT
public:
	explicit ChzzkChat(QObject *parent = nullptr);

	// "https://chzzk.naver.com/video/1234567" 또는 "1234567" → 1234567 (못 찾으면 0)
	static qint64 videoNumber(const QString &text);

	void start(qint64 videoNo);
	void cancel();
	bool isRunning() const { return m_running; }

	// 테스트용: 응답 JSON 한 장을 읽어 메시지·다음 위치를 꺼냄 (다음이 없으면 -1)
	static QVector<ChatMessage> parsePage(const QByteArray &json, qint64 *next, QString *error);

signals:
	void info(const QString &title, double duration);   // 영상 제목·길이 (초)
	void progress(double reachedSec, double duration, int count);
	void finished(bool ok, const QString &message, const QVector<ChatMessage> &chats);

private:
	void requestInfo();
	void requestPage();
	void fail(const QString &msg);

	QNetworkAccessManager *m_net;
	QPointer<QNetworkReply> m_reply;
	qint64 m_video = 0;
	qint64 m_cursor = 0;
	double m_duration = 0;
	int m_retries = 0;
	int m_emptyPages = 0;
	bool m_running = false;
	QVector<ChatMessage> m_out;
};
