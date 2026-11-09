#pragma once

#include "Games.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QRectF>

// 2단계: 결과 화면 후보마다 원본 장면을 한 장씩 꺼내(ffmpeg) 등수를 읽음
//  - 게임 화면 영역만 잘라 1920x1080 으로 맞춘 뒤 rank::readRank
//  - 가운데 장면에서 못 읽으면 후보 구간의 앞·뒤 장면으로 한 번씩 더
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
	void next();
	void onFinished(int code, QProcess::ExitStatus st);

	QProcess m_proc;
	QString m_path;
	QRectF m_game;
	QVector<ResultCandidate> m_cands;
	QVector<RankReading> m_out;
	QByteArray m_buf;
	int m_index = 0;
	int m_try = 0; // 0 가운데, 1 앞, 2 뒤
	double m_time = 0;
	bool m_running = false;
	bool m_cancelled = false;
};
