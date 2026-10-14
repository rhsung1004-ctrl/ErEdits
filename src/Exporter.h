#pragma once

#include "Segments.h"
#include "Subtitles.h"
#include "Util.h"

#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <memory>

struct ExportSettings {
	int height = 1080;      // 결과 세로 크기 (가로는 16:9 로 자동), 0 = 원본 높이
	double fps = 0;         // 0 = 원본 (최대 60)
	double crossfade = 0.5; // 클립 사이 크로스페이드 (초, 0 = 그냥 이어 붙임)
	double fadeIn = 1.0;    // 영상 처음 페이드 인 (초, 0 = 없음)
	double fadeOut = 1.0;   // 영상 마지막 페이드 아웃 (초, 0 = 없음)
	bool cpuOnly = false;   // 그래픽카드 인코더를 쓰지 않음
	QString outPath;
};

// 고른 구간들을 원본에서 잘라 16:9 한 편으로 합침 (ffmpeg 한 번)
//  - 구간마다 -ss 로 바로 찾아가서 읽으므로 몇 시간짜리 원본도 앞부분을 다 풀지 않음
//  - 클립 사이: xfade(영상) + acrossfade(소리), 처음/끝: fade + afade
class Exporter : public QObject {
	Q_OBJECT
public:
	explicit Exporter(QObject *parent = nullptr);
	~Exporter() override;

	void start(const QString &input, const VideoInfo &info, const QVector<Segment> &segments,
		   const QVector<Subtitle> &subtitles, const ExportSettings &s);
	void cancel();
	bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

	// 실제로 쓰일 크로스페이드 길이 (가장 짧은 클립의 절반을 넘지 않게)
	static double effectiveCrossfade(const QVector<Segment> &segments, double wanted);
	// 결과 영상 길이
	static double outputLength(const QVector<Segment> &segments, double crossfade);
	// 자막 한 조각: 미리 그려 둔 투명 PNG 를 결과 시간 start~end 에 x,y 로 겹침
	//  (미리보기와 같은 Qt 그리기 코드로 만들기 때문에 글꼴·색·테두리·콤보 색이 똑같이 나옴)
	struct Overlay {
		int input = 0;           // ffmpeg 입력 번호
		int x = 0, y = 0;
		double start = 0, end = 0; // 결과 영상 기준 초
		double fadeIn = 0, fadeOut = 0;
	};
	// ffmpeg 필터 그래프 (테스트용으로 공개)
	static QString buildFilter(const QVector<Segment> &segments, const QVector<Overlay> &overlays,
				   const VideoInfo &info, const ExportSettings &s, int outW, int outH, double fps,
				   double *totalOut);
	QString lastFilter() const { return m_lastFilter; }

signals:
	void progress(double fraction);
	void finished(bool ok, const QString &message);

private:
	void run(const QString &encoder);
	void onFinished(int code, QProcess::ExitStatus st);
	static QString detectEncoder();
	static QStringList encoderArgs(const QString &encoder, double fps);
	static bool ffmpegHasNewScriptOption();

	QProcess m_proc;
	std::unique_ptr<QTemporaryFile> m_script;
	QString m_input;
	VideoInfo m_info;
	QVector<Segment> m_segs;
	QVector<Subtitle> m_subs;
	std::unique_ptr<QTemporaryDir> m_tmp;
	ExportSettings m_set;
	QString m_encoder;
	double m_total = 0;
	bool m_cancelled = false;
	QString m_err;
	QByteArray m_out;
	QString m_lastFilter;
};
