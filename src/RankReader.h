#pragma once

#include "Games.h"

#include <QObject>
#include <QRectF>

class FrameGrabber;

// 2단계: 결과 화면 후보마다 원본 장면을 한 장씩 꺼내 등수를 읽음
//  - 게임 화면 영역만 잘라 1920x1080 으로 맞춘 뒤 rank::readRank
//  - 1차: 후보 가운데 장면 전부 (10장씩 묶어서) → 못 읽은 후보만 2차: 처음·마지막 장면
class RankReader : public QObject {
	Q_OBJECT
public:
	explicit RankReader(QObject *parent = nullptr);
	~RankReader() override;

	// game: 영상 크기 대비 0~1 비율의 게임 화면 영역
	void start(const QString &path, const QRectF &game, const QVector<ResultCandidate> &cands);
	void cancel();
	bool isRunning() const { return m_running; }

signals:
	void progress(int done, int total);
	void finished(bool ok, const QVector<RankReading> &readings);

private:
	void runRound(int round);

	FrameGrabber *m_grab;
	QString m_path;
	QRectF m_game;
	QVector<ResultCandidate> m_cands;
	QVector<QVector<RankReading>> m_found; // 후보마다 읽은 것 (가장 먼저 읽힌 하나)
	QVector<QPair<int, double>> m_asked;   // 이번 차례 장면 번호 → (후보 번호, 시각)
	int m_round = 0;
	int m_done = 0;
	bool m_running = false;
};
