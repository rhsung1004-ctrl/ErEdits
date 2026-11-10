#include "JoinedPlayer.h"

#include <QTimer>
#include <QUrl>

#include <cmath>
#include <cstdlib>

JoinedPlayer::JoinedPlayer(QObject *parent) : QObject(parent), m_p(new QMediaPlayer(this))
{
	connect(m_p, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
		if (m_switching || m_cur < 0)
			return;
		if (m_seekRetries > 0 && std::llabs(ms - m_pendingLocal) > 300)
			return; // 다시 찾아가는 중 (checkSeek)
		m_lastPos = qint64(std::llround(m_parts[m_cur].offset * 1000)) + ms;
		emit positionChanged(m_lastPos);
	});
	connect(m_p, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
		// 재생 중에 파일을 바꾸면 앞 파일의 "다 읽음" 알림이 늦게 오기도 해서, 새 파일을 읽기 시작한 뒤의 알림만 봄
		if (m_switching && st == QMediaPlayer::LoadingMedia)
			m_sawLoading = true;
		if (m_switching && m_sawLoading && (st == QMediaPlayer::LoadedMedia || st == QMediaPlayer::BufferedMedia)) {
			finishSwitch();
			return;
		}
		// 한 파일이 끝나면 다음 파일 처음부터 이어서
		if (st == QMediaPlayer::EndOfMedia && !m_switching && m_state == QMediaPlayer::PlayingState &&
		    m_cur + 1 < m_parts.size())
			switchTo(m_cur + 1, 0, true);
	});
	connect(m_p, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
		if (m_switching)
			return; // 파일을 바꾸는 동안의 멈춤은 알리지 않음
		if (s == QMediaPlayer::StoppedState && m_state == QMediaPlayer::PlayingState && m_cur + 1 < m_parts.size())
			return; // 곧 다음 파일로 넘어감
		setState(s);
	});
}

void JoinedPlayer::setState(QMediaPlayer::PlaybackState s)
{
	if (s == m_state)
		return;
	m_state = s;
	emit playbackStateChanged(s);
}

void JoinedPlayer::setSource(const QUrl &url)
{
	const QString path = url.toLocalFile();
	m_parts.clear();
	if (isJoined(path))
		m_parts = readJoined(path);
	if (m_parts.isEmpty()) {
		JoinedPart one;
		one.file = path;
		one.duration = 1e12;
		m_parts.push_back(one);
	}
	m_cur = -1;
	m_lastPos = 0;
	setState(QMediaPlayer::StoppedState);
	switchTo(0, 0, false);
}

int JoinedPlayer::partAt(qint64 ms) const
{
	const double t = ms / 1000.0;
	for (int i = 0; i < m_parts.size(); ++i)
		if (t < m_parts[i].offset + m_parts[i].duration)
			return i;
	return int(m_parts.size()) - 1;
}

void JoinedPlayer::switchTo(int part, qint64 localMs, bool playAfter)
{
	m_lastPos = qint64(std::llround(m_parts[part].offset * 1000)) + localMs;
	if (part == m_cur && !m_switching) {
		m_p->setPosition(localMs);
		if (playAfter)
			m_p->play();
		return;
	}
	m_pendingLocal = localMs;
	m_pendingPlay = playAfter;
	if (part == m_cur)
		return; // 같은 파일을 여는 중 → 위치만 바꿔 둠
	m_cur = part;
	m_switching = true;
	m_sawLoading = false;
	const int ticket = ++m_switchTicket;
	m_p->setSource(QUrl::fromLocalFile(m_parts[part].file));
	emit positionChanged(m_lastPos);
	// 알림이 오지 않는 경우를 대비해 잠시 뒤 그냥 진행
	QTimer::singleShot(3000, this, [this, ticket] {
		if (m_switching && ticket == m_switchTicket)
			finishSwitch();
	});
}

void JoinedPlayer::finishSwitch()
{
	m_switching = false;
	m_p->setPosition(m_pendingLocal);
	if (m_pendingPlay)
		m_p->play();
	else
		m_p->pause();
	// 파일을 연 직후 찾아가기가 무시되는 경우가 있어서 (멈춰 있을 때도) 잠시 뒤 확인하고 다시
	m_seekRetries = m_pendingLocal > 300 ? 6 : 0;
	if (m_seekRetries > 0)
		QTimer::singleShot(150, this, &JoinedPlayer::checkSeek);
}

void JoinedPlayer::checkSeek()
{
	if (m_seekRetries <= 0 || m_switching)
		return;
	if (std::llabs(m_p->position() - m_pendingLocal) <= 300) {
		m_seekRetries = 0;
		return;
	}
	--m_seekRetries;
	m_p->setPosition(m_pendingLocal);
	QTimer::singleShot(200, this, &JoinedPlayer::checkSeek);
}

qint64 JoinedPlayer::position() const
{
	if (m_switching || m_cur < 0)
		return m_lastPos;
	return qint64(std::llround(m_parts[m_cur].offset * 1000)) + m_p->position();
}

void JoinedPlayer::setPosition(qint64 ms)
{
	if (m_parts.isEmpty())
		return;
	const int i = partAt(ms);
	const qint64 local = std::max<qint64>(0, ms - qint64(std::llround(m_parts[i].offset * 1000)));
	if (i == m_cur && !m_switching) {
		m_lastPos = ms;
		m_seekRetries = 0;
		m_p->setPosition(local);
		return;
	}
	switchTo(i, local, m_state == QMediaPlayer::PlayingState);
}

void JoinedPlayer::play()
{
	if (m_parts.isEmpty())
		return;
	setState(QMediaPlayer::PlayingState);
	if (m_switching) {
		m_pendingPlay = true;
		return;
	}
	m_p->play();
}

void JoinedPlayer::pause()
{
	setState(QMediaPlayer::PausedState);
	if (m_switching) {
		m_pendingPlay = false;
		return;
	}
	m_p->pause();
}
