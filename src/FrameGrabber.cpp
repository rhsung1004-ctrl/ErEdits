#include "FrameGrabber.h"

#include "Util.h"

#include <algorithm>

FrameGrabber::FrameGrabber(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] { m_buf += m_proc.readAllStandardOutput(); });
	connect(&m_proc, &QProcess::finished, this, &FrameGrabber::onFinished);
}

FrameGrabber::~FrameGrabber()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

void FrameGrabber::start(const QString &path, const QVector<double> &times, const QString &vf, int w, int h)
{
	cancel();
	m_path = path;
	m_vf = vf;
	m_w = w;
	m_h = h;
	m_times = times;
	m_queue.clear();
	m_single.clear();
	for (int i = 0; i < times.size(); ++i)
		m_queue.push_back(i);
	m_cancelled = false;
	m_running = true;
	next();
}

void FrameGrabber::cancel()
{
	if (!m_running)
		return;
	m_cancelled = true;
	m_running = false;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

void FrameGrabber::next()
{
	if (m_cancelled)
		return;
	m_batch.clear();
	if (!m_single.isEmpty()) {
		m_batch.push_back(m_single.takeFirst());
	} else {
		while (!m_queue.isEmpty() && m_batch.size() < kBatch)
			m_batch.push_back(m_queue.takeFirst());
	}
	if (m_batch.isEmpty()) {
		m_running = false;
		emit finished();
		return;
	}
	QStringList args{"-hide_banner", "-nostdin", "-loglevel", "error"};
	for (int i : m_batch)
		args << "-ss" << num(std::max(0.0, m_times[i])) << "-t" << "0.5" << inputArgs(m_path);
	// 장면마다 첫 한 장만 → 위아래로 쌓아 키 큰 그림 한 장으로 (concat 은 4장 이상에서 빠지는 일이 있어 vstack)
	const QString chain = m_vf + ",trim=end_frame=1,setpts=0,setsar=1,format=rgb24";
	QString fc;
	if (m_batch.size() == 1) {
		fc = "[0:v]" + chain + "[out]";
	} else {
		for (int k = 0; k < m_batch.size(); ++k)
			fc += QString("[%1:v]%2[v%1];").arg(k).arg(chain);
		for (int k = 0; k < m_batch.size(); ++k)
			fc += QString("[v%1]").arg(k);
		fc += QString("vstack=inputs=%1[out]").arg(m_batch.size());
	}
	args << "-filter_complex" << fc << "-map" << "[out]" << "-an" << "-f" << "rawvideo" << "-pix_fmt" << "rgb24" << "-";
	m_buf.clear();
	setQuietProcess(m_proc, false);
	m_proc.start(ffmpegPath(), args);
}

void FrameGrabber::onFinished(int, QProcess::ExitStatus)
{
	if (m_cancelled)
		return;
	m_buf += m_proc.readAllStandardOutput();
	const qsizetype fsz = qsizetype(m_w) * m_h * 3;
	const int got = int(m_buf.size() / fsz); // 쌓은 그림은 장면을 위에서부터 차례로 담고 있음
	if (got == m_batch.size()) {
		for (int k = 0; k < got; ++k)
			emit frame(m_batch[k], m_buf.mid(k * fsz, fsz));
	} else if (m_batch.size() > 1) {
		// 어느 장면이 빠졌는지 모르니 이 묶음은 한 장씩 다시
		m_single += m_batch;
	} else {
		emit frame(m_batch[0], got >= 1 ? m_buf.left(fsz) : QByteArray());
	}
	m_buf.clear();
	next();
}
