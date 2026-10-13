#pragma once

#include <QRectF>
#include <QString>

class QProcess;

// 프로그램 옆의 ffmpeg.exe (없으면 PATH 의 ffmpeg)
QString ffmpegPath();

// 분석처럼 오래 걸리는 ffmpeg 작업: 콘솔 창 없이, 낮은 우선순위로 (다른 작업을 덜 방해)
void setQuietProcess(QProcess &proc, bool lowPriority);

// 1:02:03.4 / 2:03.4 형식
QString formatTime(double sec, bool tenths = true);

struct VideoInfo {
	bool ok = false;
	double duration = 0; // 초
	int width = 0, height = 0;
	double fps = 0;
	bool hasAudio = false;
	QString error;
};

// ffmpeg -i 출력에서 길이·해상도·프레임 수·소리 유무를 읽음 (몇 초 이내)
VideoInfo probeVideo(const QString &path);

// 영상 전체가 게임 화면일 때 킬 숫자 영역 기본값 (영상 크기 대비 0~1 비율)
QRectF defaultKillRoi(int videoW, int videoH);

// ffmpeg crop 필터용 숫자 (소수점 6자리)
QString num(double v);
