#include "AudioLevels.h"

#include "Util.h"

#include <cmath>

namespace {
constexpr int kRate = 8000;
}

AudioLevels::AudioLevels(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, &AudioLevels::onData);
	connect(&m_proc, &QProcess::finished, this, &AudioLevels::onFinished);
}

AudioLevels::~AudioLevels()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

float AudioLevels::rmsDb(const int16_t *s, int n)
{
	if (n <= 0)
		return -90.f;
	double acc = 0;
	for (int i = 0; i < n; ++i)
		acc += double(s[i]) * s[i];
	const double rms = std::sqrt(acc / n) / 32768.0;
	return float(std::max(-90.0, 20.0 * std::log10(rms + 1e-9)));
}

void AudioLevels::start(const QString &path, double duration)
{
	cancel();
	m_cancelled = false;
	m_buf.clear();
	m_db.clear();
	m_duration = duration;
	setQuietProcess(m_proc, true);
	m_proc.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-i", path, "-vn", "-sn", "-dn",
				    "-ac", "1", "-ar", QString::number(kRate), "-f", "s16le", "-"});
}

void AudioLevels::cancel()
{
	if (m_proc.state() == QProcess::NotRunning)
		return;
	m_cancelled = true;
	m_proc.kill();
	m_proc.waitForFinished(3000);
}

void AudioLevels::onData()
{
	m_buf += m_proc.readAllStandardOutput();
	const qsizetype sec = qsizetype(kRate) * 2;
	qsizetype off = 0;
	while (m_buf.size() - off >= sec) {
		m_db.push_back(rmsDb(reinterpret_cast<const int16_t *>(m_buf.constData() + off), kRate));
		off += sec;
	}
	if (off > 0) {
		m_buf.remove(0, off);
		if (m_db.size() % 30 == 0)
			emit progress(m_db.size(), m_duration);
	}
}

void AudioLevels::onFinished(int code, QProcess::ExitStatus st)
{
	if (m_cancelled)
		return;
	onData();
	if (m_buf.size() >= 2)
		m_db.push_back(rmsDb(reinterpret_cast<const int16_t *>(m_buf.constData()), int(m_buf.size() / 2)));
	m_buf.clear();
	const bool ok = st == QProcess::NormalExit && code == 0 && !m_db.isEmpty();
	emit finished(ok, m_db);
}
