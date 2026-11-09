#pragma once

#include "Util.h"

#include <QMediaPlayer>
#include <QObject>

class QAudioOutput;
class QVideoSink;

// 미리보기 재생: 파일 하나는 그대로, 여러 파일 묶음(.ffconcat)은 파일을 바꿔 가며 하나의 시간축처럼
//  QMediaPlayer 와 같은 이름의 함수만 (메인 창이 쓰는 것)
//  - 위치는 묶은 영상 기준 ms
//  - 한 파일이 끝나면 다음 파일로 이어서 재생
class JoinedPlayer : public QObject {
	Q_OBJECT
public:
	explicit JoinedPlayer(QObject *parent = nullptr);

	void setAudioOutput(QAudioOutput *out) { m_p->setAudioOutput(out); }
	void setVideoSink(QVideoSink *sink) { m_p->setVideoSink(sink); }
	void setSource(const QUrl &url);
	qint64 position() const;
	void setPosition(qint64 ms);
	void play();
	void pause();
	QMediaPlayer::PlaybackState playbackState() const { return m_state; }
	int partCount() const { return int(m_parts.size()); }

signals:
	void positionChanged(qint64 ms);
	void playbackStateChanged(QMediaPlayer::PlaybackState s);

private:
	int partAt(qint64 ms) const;
	void switchTo(int part, qint64 localMs, bool playAfter);
	void setState(QMediaPlayer::PlaybackState s);
	void finishSwitch();
	void checkSeek();

	QMediaPlayer *m_p;
	QVector<JoinedPart> m_parts; // 파일 하나면 한 칸 (offset 0)
	int m_cur = -1;
	bool m_switching = false;
	bool m_sawLoading = false;
	int m_switchTicket = 0;
	int m_seekRetries = 0;
	qint64 m_pendingLocal = 0;
	bool m_pendingPlay = false;
	qint64 m_lastPos = 0;
	QMediaPlayer::PlaybackState m_state = QMediaPlayer::StoppedState;
};
