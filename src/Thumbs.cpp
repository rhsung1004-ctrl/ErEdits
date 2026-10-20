#include "Thumbs.h"

#include "Util.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

Thumbs::Thumbs(QObject *parent) : QObject(parent)
{
	m_proc.setProcessChannelMode(QProcess::MergedChannels);
	connect(&m_proc, &QProcess::readyRead, this, [this] { m_proc.readAll(); });
	connect(&m_proc, &QProcess::finished, this, &Thumbs::onFinished);
}

Thumbs::~Thumbs()
{
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(1000);
	}
}

void Thumbs::setVideo(const QString &path)
{
	if (path == m_path)
		return;
	m_jobs.clear(); // 멈추는 동안 다음 작업이 시작되지 않게 먼저 비움
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(1000);
	}
	m_path = path;
	m_frames.clear();
	m_queued.clear();
	m_jobs.clear();
	m_dir.clear();
	if (path.isEmpty())
		return;
	const QFileInfo fi(path);
	const QByteArray key = (fi.absoluteFilePath() + "|" + QString::number(fi.size()) + "|" +
				QString::number(fi.lastModified().toSecsSinceEpoch()))
				       .toUtf8();
	m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs/" +
		QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(16);
	QDir().mkpath(m_dir);
}

QString Thumbs::fileFor(int second) const
{
	return m_dir + QString("/%1.jpg").arg(second, 6, 10, QChar('0'));
}

void Thumbs::request(double from, double to)
{
	if (m_path.isEmpty())
		return;
	const int a = std::max(0, int(std::floor(from))), b = std::max(a, int(std::ceil(to)));
	bool loaded = false;
	int runStart = -1;
	auto flush = [&](int end) {
		if (runStart < 0)
			return;
		// 너무 긴 구간은 나눠서 (앞쪽부터 빨리 보이게)
		for (int s = runStart; s < end; s += 60) {
			Job j;
			j.first = s;
			j.count = std::min(60, end - s);
			m_jobs.push_back(j);
		}
		runStart = -1;
	};
	for (int sec = a; sec <= b; ++sec) {
		if (m_frames.contains(sec) || m_queued.contains(sec)) {
			flush(sec);
			continue;
		}
		const QString f = fileFor(sec);
		if (QFile::exists(f)) {
			const QImage img(f);
			if (!img.isNull()) {
				m_frames.insert(sec, img);
				loaded = true;
				flush(sec);
				continue;
			}
		}
		m_queued.insert(sec);
		if (runStart < 0)
			runStart = sec;
	}
	flush(b + 1);
	if (loaded)
		emit updated();
	startNext();
}

void Thumbs::startNext()
{
	if (m_proc.state() != QProcess::NotRunning || m_jobs.isEmpty())
		return;
	m_cur = m_jobs.takeFirst();
	// 1초에 한 장: 칸 가운데(+0.5초) 장면. -start_number 로 파일 이름 = 원본 초
	setQuietProcess(m_proc, true);
	m_proc.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-ss",
				    QString::number(m_cur.first), "-t", QString::number(m_cur.count), "-i", m_path, "-an",
				    "-sn", "-vf", QString("fps=1,scale=-2:%1").arg(kHeight), "-q:v", "7", "-start_number",
				    QString::number(m_cur.first), m_dir + "/%06d.jpg"});
}

void Thumbs::onFinished(int, QProcess::ExitStatus)
{
	bool any = false;
	for (int s = m_cur.first; s < m_cur.first + m_cur.count; ++s) {
		m_queued.remove(s);
		const QImage img(fileFor(s));
		if (!img.isNull()) {
			m_frames.insert(s, img);
			any = true;
		}
	}
	if (any)
		emit updated();
	startNext();
}

QImage Thumbs::frameAt(double t) const
{
	const int s = std::max(0, int(std::floor(t)));
	auto it = m_frames.constFind(s);
	if (it != m_frames.constEnd())
		return *it;
	it = m_frames.constFind(s - 1);
	return it != m_frames.constEnd() ? *it : QImage();
}
