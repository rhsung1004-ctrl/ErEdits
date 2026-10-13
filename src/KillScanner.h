#pragma once

#include "HudKillWatcher.h"
#include "Segments.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QRectF>

// 영상 전체를 ffmpeg 로 훑으며 킬 숫자 영역(160x32 밝기)을 초당 5장씩 받아 HudKillWatcher 에 넣음
// 몇 시간짜리 다시보기도 메모리에 쌓지 않고 흘려 보내며 처리
class KillScanner : public QObject {
	Q_OBJECT
public:
	enum Mode {
		Precise, // 모든 프레임을 풀어 초당 5장 (정확, 느림)
		Fast,    // 키프레임만 풀기 (보통 2초 간격, 훨씬 빠름, 킬 시각 오차 ±2초)
	};

	explicit KillScanner(QObject *parent = nullptr);
	~KillScanner() override;

	// roi: 영상 크기 대비 0~1 비율 영역
	void start(const QString &path, const QRectF &roi, double duration, Mode mode);
	void cancel();
	bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

signals:
	void progress(double seconds, double duration); // 분석한 위치
	void found(const KillEvent &e);
	void finished(bool ok, const QString &message);

private:
	void launch(bool hwaccel);
	void onData();
	void onFinished(int code, QProcess::ExitStatus st);

	QProcess m_proc;
	HudKillWatcher m_watcher;
	QByteArray m_buf;
	QString m_path;
	QRectF m_roi;
	double m_duration = 0;
	Mode m_mode = Precise;
	bool m_hw = true;
	bool m_cancelled = false;
	qint64 m_frames = 0;
	qint64 m_lastReportFrame = -1000;
	int m_found = 0;
	QString m_err;
};
