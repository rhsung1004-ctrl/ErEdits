#include "RankReader.h"

#include "FrameGrabber.h"
#include "ResultScreen.h"
#include "Util.h"

#include <algorithm>

RankReader::RankReader(QObject *parent) : QObject(parent), m_grab(new FrameGrabber(this))
{
	connect(m_grab, &FrameGrabber::frame, this, [this](int k, const QByteArray &rgb) {
		if (!m_running || k < 0 || k >= m_asked.size())
			return;
		const auto [ci, t] = m_asked[k];
		if (m_found[ci].isEmpty() && rgb.size() >= qsizetype(rank::kFullW) * rank::kFullH * 3) {
			const rank::Reading r = rank::readRank(reinterpret_cast<const uint8_t *>(rgb.constData()));
			if (r.ok()) {
				RankReading rr;
				rr.time = t;
				rr.screen = r.screen;
				rr.rank = r.rank;
				rr.weak = r.weak;
				m_found[ci].push_back(rr);
			}
		}
		if (m_round == 1)
			emit progress(++m_done, int(m_cands.size()));
	});
	connect(m_grab, &FrameGrabber::finished, this, [this] {
		if (!m_running)
			return;
		if (m_round == 1) {
			runRound(2);
			return;
		}
		m_running = false;
		QVector<RankReading> out;
		for (const auto &f : m_found)
			out += f;
		emit progress(int(m_cands.size()), int(m_cands.size()));
		emit finished(true, out);
	});
}

RankReader::~RankReader() = default;

void RankReader::start(const QString &path, const QRectF &game, const QVector<ResultCandidate> &cands)
{
	cancel();
	m_path = path;
	m_game = game;
	m_cands = cands;
	m_found = QVector<QVector<RankReading>>(cands.size());
	m_done = 0;
	m_running = true;
	runRound(1);
}

void RankReader::cancel()
{
	if (!m_running)
		return;
	m_running = false;
	m_grab->cancel();
}

void RankReader::runRound(int round)
{
	m_round = round;
	m_asked.clear();
	QVector<double> times;
	for (int i = 0; i < m_cands.size(); ++i) {
		const ResultCandidate &c = m_cands[i];
		if (round == 1) {
			m_asked.push_back({i, c.mid()});
		} else {
			// 가운데에서 못 읽었고 장면이 둘 이상이면 처음·마지막 장면 (먼저 읽힌 것 하나만 씀)
			if (!m_found[i].isEmpty() || c.t1 - c.t0 < 0.15)
				continue;
			m_asked.push_back({i, c.t0 + 0.05});
			m_asked.push_back({i, c.t1 - 0.05});
		}
	}
	for (const auto &a : m_asked)
		times.push_back(std::max(0.0, a.second));
	const QString vf = QString("crop=w='iw*%1':h='ih*%2':x='iw*%3':y='ih*%4',scale=%5:%6:flags=bicubic")
				   .arg(num(m_game.width()), num(m_game.height()), num(m_game.x()), num(m_game.y()))
				   .arg(rank::kFullW)
				   .arg(rank::kFullH);
	m_grab->start(m_path, times, vf, rank::kFullW, rank::kFullH);
}
