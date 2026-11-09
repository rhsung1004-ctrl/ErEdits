#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QVector>

#include <cstdint>

// 다시보기 소리 크기를 1초마다 dB 로 (ffmpeg 로 소리만 8kHz 한 채널로 풀어 흘려 보내며 계산)
// 몇 시간짜리도 1~2분 정도, 메모리에 쌓지 않음
class AudioLevels : public QObject {
	Q_OBJECT
public:
	explicit AudioLevels(QObject *parent = nullptr);
	~AudioLevels() override;

	void start(const QString &path, double duration);
	void cancel();
	bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

	// 16비트 소리 표본 → 1초 dB (테스트용)
	static float rmsDb(const int16_t *s, int n);

signals:
	void progress(double sec, double duration);
	void finished(bool ok, const QVector<float> &db);

private:
	void onData();
	void onFinished(int code, QProcess::ExitStatus st);

	QProcess m_proc;
	QByteArray m_buf;
	QVector<float> m_db;
	double m_duration = 0;
	bool m_cancelled = false;
};
