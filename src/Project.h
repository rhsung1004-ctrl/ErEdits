#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// 프로젝트 파일 (.erkc): 방송 목록 + 방송마다 분석·편집 결과 전부를 한 파일에
//  - 원하는 곳에 저장해 두면 프로그램을 다시 깔거나 영상을 옮겨도 그대로 이어서 편집
//  - 영상 경로는 절대 경로와 프로젝트 파일 기준 상대 경로를 함께 저장 (옮겼으면 찾아서 다시 연결)
struct ProjectVod {
	QString path;       // 영상 (묶음이면 .ffconcat — 열 때 parts 로 다시 만듦)
	QStringList parts;  // 묶음일 때 원본 파일들 (순서대로)
	bool checked = true; // 합쳐 내보내기에 넣음
	double duration = 0;
	QJsonObject cache;  // 분석·편집 결과 (MainWindow::saveCache 와 같은 모양)
	QJsonObject ai;     // AI 편집점 결과
};

struct ProjectData {
	QVector<ProjectVod> vods;
	int current = -1;
	QJsonObject exportSettings;
};

// 원자적으로 저장 (중간에 꺼져도 앞 내용이 남음)
bool writeJsonFile(const QString &path, const QJsonObject &o, QString *error = nullptr);
QJsonObject readJsonFile(const QString &path);

bool saveProjectFile(const QString &file, const ProjectData &d, QString *error);
bool loadProjectFile(const QString &file, ProjectData *d, QString *error);

// 옮겨진 파일 찾기: 원래 경로 → 프로젝트 기준 상대 경로 → 프로젝트 폴더의 같은 이름
QString resolveProjectPath(const QString &projectFile, const QString &absPath, const QString &relPath);

constexpr const char *kProjectSuffix = "erkc";
