#pragma once

#include <QRectF>
#include <QString>
#include <QVector>

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

// 킬 숫자 영역으로 게임 화면 전체 위치를 거꾸로 계산 (영상 크기 대비 0~1 비율)
//  킬 숫자 줄은 게임 화면 오른쪽 위에 있고 크기가 화면 높이에 비례하므로, 영역 크기로 게임 화면 크기(16:9)를 알 수 있음
//  - 웹캠·채팅 때문에 게임 화면이 작게 들어간 방송도 결과 화면 위치를 맞출 수 있음
//  - 계산이 영상 전체와 거의 같거나 이상하면 영상 전체를 게임 화면으로 봄
QRectF gameRectFromKillRoi(const QRectF &roi, int videoW, int videoH);

// ffmpeg crop 필터용 숫자 (소수점 6자리)
QString num(double v);

// ── 여러 파일을 한 방송으로 묶기 ──
// 한 방송이 여러 파일로 나뉘어 있으면 ffmpeg 의 이어 붙이기 목록(.ffconcat)을 만들어 하나의 영상처럼 다룸
//  - 분석·내보내기는 ffmpeg 가 목록을 그대로 한 영상으로 읽음 (inputArgs)
//  - 미리보기는 JoinedPlayer 가 파일을 바꿔 가며 재생
struct JoinedPart {
	QString file;
	double offset = 0;   // 묶은 영상에서 이 파일이 시작하는 시각 (초)
	double duration = 0;
};
bool isJoined(const QString &path);
QVector<JoinedPart> readJoined(const QString &path);
// files 순서대로 묶은 목록을 만들고 그 경로를 돌려줌 (같은 파일 묶음이면 같은 경로 → 분석 결과도 이어서 씀)
// 해상도가 다르면 묶을 수 없음 → 빈 문자열 + error
QString makeJoined(const QStringList &files, QString *error);
// ffmpeg 에 이 영상을 넣는 인자 ("-i" 경로, 묶음이면 이어 붙이기 형식으로)
QStringList inputArgs(const QString &path);
// 이 영상에서 한 장면을 꺼낼 때 앞에서 미리 읽을 시간 (초)
// .ts 등은 찾아가기가 키프레임 사이에 떨어져 다음 키프레임까지(최대 몇 초) 늦은 장면이 나오므로
//  영상의 키프레임 간격만큼 앞부터 읽음 (mp4·mkv 는 0)
double seekPreRoll(const QString &path);
// 화면에 보일 이름 ("a.mkv" 또는 "a.mkv 외 2개")
QString displayName(const QString &path);
