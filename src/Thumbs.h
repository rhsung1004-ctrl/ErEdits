#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QVector>

// 편집 타임라인에 그릴 작은 장면 (1초에 한 장, 높이 54px)
//  - 몇 시간짜리 다시보기 전체가 아니라 클립 구간(앞뒤 여유 포함)만 ffmpeg 로 뽑음
//  - 영상별 캐시 폴더에 저장해 두고 다음에 열 때는 바로 읽음
class Thumbs : public QObject {
	Q_OBJECT
public:
	static constexpr int kHeight = 54;

	explicit Thumbs(QObject *parent = nullptr);
	~Thumbs() override;

	void setVideo(const QString &path); // 다른 영상이면 비움
	// 원본 [from, to] 구간의 장면을 준비 (이미 있는 초는 건너뜀)
	void request(double from, double to);
	// 원본 시간 t 의 장면 (아직 없으면 null)
	QImage frameAt(double t) const;

signals:
	void updated();

private:
	struct Job {
		int first = 0, count = 0;
	};
	void startNext();
	void onFinished(int code, QProcess::ExitStatus st);
	QString fileFor(int second) const;

	QString m_path;
	QString m_dir;
	QHash<int, QImage> m_frames;
	QSet<int> m_queued; // 뽑는 중이거나 뽑을 예정인 초
	QVector<Job> m_jobs;
	Job m_cur;
	QProcess m_proc;
};
