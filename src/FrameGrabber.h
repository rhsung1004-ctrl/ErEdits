#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QVector>

// 여러 시각의 장면을 한 장씩 꺼냄 — ffmpeg 를 장면마다 띄우지 않고 10장씩 묶어 한 번에
//  각 시각마다 -ss 로 바로 찾아가 0.5초만 읽으므로 몇 시간짜리 영상도 빠름
//  묶음 결과가 모자라면(영상 끝 등) 그 묶음만 한 장씩 다시
class FrameGrabber : public QObject {
	Q_OBJECT
public:
	explicit FrameGrabber(QObject *parent = nullptr);
	~FrameGrabber() override;

	// vf: 장면마다 적용할 필터 (crop·scale 등) — 결과는 w x h RGB
	void start(const QString &path, const QVector<double> &times, const QString &vf, int w, int h);
	void cancel();
	bool isRunning() const { return m_running; }

	static constexpr int kBatch = 10;

signals:
	void frame(int index, const QByteArray &rgb); // 못 꺼낸 장면은 빈 배열
	void finished();

private:
	void next();
	void onFinished(int code, QProcess::ExitStatus st);

	QProcess m_proc;
	QString m_path, m_vf;
	int m_w = 0, m_h = 0;
	QVector<double> m_times;
	QVector<int> m_queue;   // 아직 꺼내지 않은 번호
	QVector<int> m_batch;   // 지금 꺼내는 번호
	QVector<int> m_single;  // 묶음이 실패해서 한 장씩 다시 할 번호
	QByteArray m_buf;
	bool m_running = false;
	bool m_cancelled = false;
};
