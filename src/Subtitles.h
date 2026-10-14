#pragma once

#include "Segments.h"

#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QString>
#include <QVector>

class QPainter;
class QRectF;

// 자막 하나 — 시간은 "원본 영상 기준"으로 저장
//  → 클립 길이를 바꾸거나 크로스페이드 길이를 바꿔도 자막은 그 장면에 붙어 있음
//  크기·두께는 1080p 기준 픽셀 (다른 해상도로 내보내면 비율대로 커지고 작아짐)
struct Subtitle {
	double start = 0;
	double end = 3;
	QString text;
	QString font;          // 글꼴 파일 경로 (빈 문자열 = 기본 글꼴)
	int size = 64;
	QColor color = Qt::white;
	QColor outline = Qt::black;
	int outlineWidth = 4;  // 0 = 테두리 없음
	bool shadow = true;
	bool box = false;      // 반투명 검은 박스
	bool fade = true;      // 0.2초 페이드로 나타나고 사라짐
	double posY = 0.9;     // 자막 아래쪽 끝의 세로 위치 (화면 높이 대비 0~1)
	QString combo = QStringLiteral("기본"); // 스킬 콤보 색 프리셋 이름 (빈 문자열 = 사용 안 함)

	double length() const { return end - start; }
	void copyStyleFrom(const Subtitle &o);
};

constexpr double kSubtitleFade = 0.2;

// 결과 영상에서 자막이 보이는 구간 (클립 사이에 걸치면 여러 조각)
struct SubtitlePiece {
	double outStart = 0, outEnd = 0;
	bool fadeIn = false, fadeOut = false; // 자막 처음/끝이 이 조각에 있을 때만
};
QVector<SubtitlePiece> mapSubtitle(const Subtitle &s, const QVector<Segment> &enabledSegs, double crossfade);

// 원본 시간 t 가 결과 영상의 몇 초인지 (사용 클립 밖이면 -1)
double sourceToOutput(double t, const QVector<Segment> &enabledSegs, double crossfade);

// 미리보기용 그리기: canvas = 16:9 결과 화면이 그려진 자리, alpha = 0~1
// 반환: 자막이 차지한 영역 (끌어서 옮기기용)
QRectF paintSubtitle(QPainter &p, const QRectF &canvas, const Subtitle &s, double alpha);
double subtitleAlphaAt(const Subtitle &s, double t);

// 내보내기용: 결과 화면(W x H) 크기로 자막만 그린 투명 이미지 (자막이 있는 부분만 잘라서)
// topLeft = 결과 화면에서 이미지가 놓일 자리. 빈 자막이면 null 이미지
QImage renderSubtitleImage(const Subtitle &s, int W, int H, QPoint *topLeft);

QJsonArray subtitlesToJson(const QVector<Subtitle> &subs);
QVector<Subtitle> subtitlesFromJson(const QJsonArray &a);
QJsonObject subtitleStyleToJson(const Subtitle &s);
void subtitleStyleFromJson(const QJsonObject &o, Subtitle *s);
