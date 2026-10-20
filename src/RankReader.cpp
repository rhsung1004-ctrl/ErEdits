#include "RankReader.h"

#include "Util.h"

#include <algorithm>

RankReader::RankReader(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] { m_buf += m_proc.readAllStandardOutput(); });
	connect(&m_proc, &QProcess::finished, this, &RankReader::onFinished);
}

RankReader::~RankReader()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

void RankReader::start(const QString &path, const QRectF &game, const QVector<ResultCandidate> &cands)
{
	cancel();
	m_path = path;
	m_game = game;
	m_cands = cands;
	m_out.clear();
	m_index = 0;
	m_try = 0;
	m_cancelled = false;
	m_running = true;
	next();
}

void RankReader::cancel()
{
	if (!m_running)
		return;
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
	m_running = false;
}

void RankReader::next()
{
	if (m_cancelled)
		return;
	if (m_index >= m_cands.size()) {
		m_running = false;
		emit progress(int(m_cands.size()), int(m_cands.size()));
		emit finished(true, m_out);
		return;
	}
	const ResultCandidate &c = m_cands[m_index];
	// 가운데 → (못 읽으면) 처음 보인 장면 → 마지막으로 보인 장면
	if (m_try == 0)
		m_time = c.mid();
	else if (m_try == 1)
		m_time = c.t0 + 0.05;
	else
		m_time = c.t1 - 0.05;
	m_time = std::max(0.0, m_time);

	const QString vf = QString("crop=w='iw*%1':h='ih*%2':x='iw*%3':y='ih*%4',scale=%5:%6:flags=bicubic")
				   .arg(num(m_game.width()), num(m_game.height()), num(m_game.x()), num(m_game.y()))
				   .arg(rank::kFullW)
				   .arg(rank::kFullH);
	m_buf.clear();
	setQuietProcess(m_proc, false);
	m_proc.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-ss", num(m_time), "-i", m_path,
				    "-frames:v", "1", "-an", "-sn", "-dn", "-vf", vf, "-f", "rawvideo", "-pix_fmt",
				    "rgb24", "-"});
}

void RankReader::onFinished(int, QProcess::ExitStatus)
{
	if (m_cancelled)
		return;
	m_buf += m_proc.readAllStandardOutput();
	const qsizetype need = qsizetype(rank::kFullW) * rank::kFullH * 3;
	rank::Reading r;
	if (m_buf.size() >= need)
		r = rank::readRank(reinterpret_cast<const uint8_t *>(m_buf.constData()));
	m_buf.clear();

	const ResultCandidate &c = m_cands[m_index];
	const bool shortRun = c.t1 - c.t0 < 0.15; // 한 장면뿐이면 앞·뒤도 같은 장면
	if (r.ok() || m_try >= 2 || shortRun) {
		if (r.ok()) {
			RankReading rr;
			rr.time = m_time;
			rr.screen = r.screen;
			rr.rank = r.rank;
			rr.weak = r.weak;
			m_out.push_back(rr);
		}
		++m_index;
		m_try = 0;
		emit progress(m_index, int(m_cands.size()));
	} else {
		++m_try;
	}
	next();
}
