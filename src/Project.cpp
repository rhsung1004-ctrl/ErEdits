#include "Project.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

bool writeJsonFile(const QString &path, const QJsonObject &o, QString *error)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) < 0 || !f.commit()) {
		if (error)
			*error = f.errorString();
		return false;
	}
	return true;
}

QJsonObject readJsonFile(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(f.readAll()).object();
}

static QString rel(const QString &projectFile, const QString &path)
{
	return QFileInfo(projectFile).absoluteDir().relativeFilePath(path);
}

bool saveProjectFile(const QString &file, const ProjectData &d, QString *error)
{
	QJsonArray vods;
	for (const ProjectVod &v : d.vods) {
		QJsonObject o;
		if (v.parts.isEmpty()) {
			o["path"] = v.path;
			o["rel"] = rel(file, v.path);
		} else {
			QJsonArray parts;
			for (const QString &p : v.parts)
				parts.append(QJsonObject{{"path", p}, {"rel", rel(file, p)}});
			o["parts"] = parts;
		}
		o["checked"] = v.checked;
		o["duration"] = v.duration;
		if (!v.cache.isEmpty())
			o["cache"] = v.cache;
		if (!v.ai.isEmpty())
			o["ai"] = v.ai;
		vods.append(o);
	}
	QJsonObject root;
	root["format"] = "ERKillCut project";
	root["version"] = 1;
	root["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	root["current"] = d.current;
	root["vods"] = vods;
	root["export"] = d.exportSettings;
	// 앞 판은 .bak 으로 남겨 둠
	if (QFileInfo::exists(file)) {
		QFile::remove(file + ".bak");
		QFile::copy(file, file + ".bak");
	}
	return writeJsonFile(file, root, error);
}

bool loadProjectFile(const QString &file, ProjectData *d, QString *error)
{
	QFile f(file);
	if (!f.open(QIODevice::ReadOnly)) {
		if (error)
			*error = "파일을 열 수 없어요: " + f.errorString();
		return false;
	}
	QJsonParseError pe;
	const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
	if (pe.error != QJsonParseError::NoError || root["format"].toString() != "ERKillCut project") {
		if (error)
			*error = "ERKillCut 프로젝트 파일이 아니거나 망가졌어요";
		return false;
	}
	*d = ProjectData();
	for (const QJsonValue &val : root["vods"].toArray()) {
		const QJsonObject o = val.toObject();
		ProjectVod v;
		if (o.contains("parts")) {
			for (const QJsonValue &p : o["parts"].toArray()) {
				const QJsonObject po = p.toObject();
				v.parts << resolveProjectPath(file, po["path"].toString(), po["rel"].toString());
			}
		} else {
			v.path = resolveProjectPath(file, o["path"].toString(), o["rel"].toString());
		}
		v.checked = o["checked"].toBool(true);
		v.duration = o["duration"].toDouble();
		v.cache = o["cache"].toObject();
		v.ai = o["ai"].toObject();
		d->vods.push_back(v);
	}
	d->current = root["current"].toInt(-1);
	d->exportSettings = root["export"].toObject();
	return true;
}

QString resolveProjectPath(const QString &projectFile, const QString &absPath, const QString &relPath)
{
	if (!absPath.isEmpty() && QFileInfo::exists(absPath))
		return absPath;
	const QDir dir = QFileInfo(projectFile).absoluteDir();
	if (!relPath.isEmpty()) {
		const QString r = QDir::cleanPath(dir.filePath(relPath));
		if (QFileInfo::exists(r))
			return r;
	}
	const QString same = dir.filePath(QFileInfo(absPath).fileName());
	if (!absPath.isEmpty() && QFileInfo::exists(same))
		return same;
	return absPath; // 못 찾음 (열 때 물어봄)
}
