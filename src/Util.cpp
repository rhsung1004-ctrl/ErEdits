#include "Util.h"

#include "HudKillWatcher.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>

#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

QString ffmpegPath()
{
#ifdef _WIN32
	const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
#else
	const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg";
#endif
	return QFileInfo::exists(bundled) ? bundled : QStringLiteral("ffmpeg");
}

void setQuietProcess(QProcess &proc, bool lowPriority)
{
#ifdef _WIN32
	proc.setCreateProcessArgumentsModifier([lowPriority](QProcess::CreateProcessArguments *args) {
		args->flags |= CREATE_NO_WINDOW;
		if (lowPriority)
			args->flags |= BELOW_NORMAL_PRIORITY_CLASS;
	});
#else
	Q_UNUSED(proc);
	Q_UNUSED(lowPriority);
#endif
}

QString formatTime(double sec, bool tenths)
{
	if (sec < 0)
		sec = 0;
	const long long tenthsTotal = (long long)std::llround(sec * 10.0);
	const long long whole = tenths ? tenthsTotal / 10 : (long long)std::llround(sec);
	const int h = int(whole / 3600), m = int(whole / 60 % 60), s = int(whole % 60);
	QString out = h > 0 ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
			    : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
	if (tenths)
		out += QString(".%1").arg(tenthsTotal % 10);
	return out;
}

static VideoInfo probeOne(const QString &path);

VideoInfo probeVideo(const QString &path)
{
	if (!isJoined(path))
		return probeOne(path);
	// 묶음: 파일마다 확인해 길이를 더함 (크기·프레임은 첫 파일)
	const auto parts = readJoined(path);
	VideoInfo info;
	if (parts.isEmpty()) {
		info.error = "묶은 파일 목록을 읽지 못했어요";
		return info;
	}
	info.hasAudio = true;
	for (int i = 0; i < parts.size(); ++i) {
		const VideoInfo one = probeOne(parts[i].file);
		if (!one.ok) {
			info.ok = false;
			info.error = QString("묶은 파일을 열 수 없어요: %1\n%2").arg(QFileInfo(parts[i].file).fileName(), one.error);
			return info;
		}
		if (i == 0) {
			info.width = one.width;
			info.height = one.height;
			info.fps = one.fps;
		}
		info.hasAudio = info.hasAudio && one.hasAudio;
	}
	info.duration = parts.back().offset + parts.back().duration;
	info.ok = info.duration > 0 && info.width > 0;
	return info;
}

static VideoInfo probeOne(const QString &path)
{
	VideoInfo info;
	QProcess p;
	setQuietProcess(p, false);
	p.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-i", path});
	if (!p.waitForStarted(5000)) {
		info.error = "ffmpeg 를 실행하지 못했어요 (프로그램 폴더의 ffmpeg.exe 확인)";
		return info;
	}
	p.waitForFinished(20000);
	const QString text = QString::fromUtf8(p.readAllStandardError());

	static const QRegularExpression reDur(R"(Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?))");
	const auto md = reDur.match(text);
	if (md.hasMatch())
		info.duration = md.captured(1).toDouble() * 3600 + md.captured(2).toDouble() * 60 + md.captured(3).toDouble();

	// 첫 번째 영상 스트림 줄
	static const QRegularExpression reVideo(R"(Stream #[^\n]*Video:[^\n]*)");
	const auto mv = reVideo.match(text);
	if (mv.hasMatch()) {
		const QString line = mv.captured(0);
		static const QRegularExpression reSize(R"([ ,](\d{2,5})x(\d{2,5})[ ,\[])");
		const auto ms = reSize.match(line);
		if (ms.hasMatch()) {
			info.width = ms.captured(1).toInt();
			info.height = ms.captured(2).toInt();
		}
		static const QRegularExpression reFps(R"(([\d.]+)\s*fps)");
		static const QRegularExpression reTbr(R"(([\d.]+)\s*tbr)");
		// 방송 녹화는 프레임이 빠져 평균(fps)이 56.9 처럼 나오기도 함 → 그럴 땐 원래 속도(tbr)를 씀
		const auto mf = reFps.match(line), mt = reTbr.match(line);
		const double f = mf.hasMatch() ? mf.captured(1).toDouble() : 0;
		const double r = mt.hasMatch() ? mt.captured(1).toDouble() : 0;
		if (f > 0 && r > 0 && r <= 120 && std::abs(f - r) / r > 0.02)
			info.fps = r;
		else if (f > 0)
			info.fps = f;
		else if (r > 0)
			info.fps = r;
		// 가장 가까운 표준 속도로 (1.5% 안일 때만)
		double best = 0, bestErr = 0.015;
		for (double std : {23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0, 120.0}) {
			const double err = std::abs(info.fps - std) / std;
			if (err < bestErr) {
				best = std;
				bestErr = err;
			}
		}
		if (best > 0)
			info.fps = best;
	}
	info.hasAudio = text.contains(QRegularExpression(R"(Stream #[^\n]*Audio:)"));
	info.ok = info.width > 0 && info.height > 0 && info.duration > 0;
	if (!info.ok)
		info.error = "영상 정보를 읽지 못했어요 (지원하지 않는 파일이거나 손상된 파일)";
	if (info.fps <= 0 || info.fps > 240)
		info.fps = 30;
	return info;
}

QRectF defaultKillRoi(int videoW, int videoH)
{
	if (videoW <= 0 || videoH <= 0)
		return QRectF(0.8, 0.0, 0.2, 0.05);
	const double u = videoH / 720.0; // 게임 화면 높이 720 기준 1px
	const double w = HudKillWatcher::kW * u, h = HudKillWatcher::kH * u;
	const double x = videoW - (HudKillWatcher::kW + HudKillWatcher::kRoiRight) * u;
	const double y = HudKillWatcher::kRoiTop * u;
	return QRectF(x / videoW, y / videoH, w / videoW, h / videoH);
}

QRectF gameRectFromKillRoi(const QRectF &roi, int videoW, int videoH)
{
	const QRectF full(0, 0, 1, 1);
	if (videoW <= 0 || videoH <= 0 || roi.height() <= 0)
		return full;
	const double u = roi.height() * videoH / HudKillWatcher::kH; // 게임 화면 높이 720 기준 1px
	const double gw = 1280 * u, gh = 720 * u;
	const double right = (roi.x() + roi.width()) * videoW + HudKillWatcher::kRoiRight * u;
	const double top = roi.y() * videoH - HudKillWatcher::kRoiTop * u;
	QRectF g((right - gw) / videoW, top / videoH, gw / videoW, gh / videoH);
	// 게임 화면이 영상보다 크거나 너무 작으면 계산을 믿지 않음
	if (g.width() > 1.08 || g.height() > 1.08 || g.width() < 0.3 || g.height() < 0.3)
		return full;
	// 영상 전체와 거의 같으면 (상자를 손으로 맞춘 오차) 영상 전체
	if (std::abs(g.width() - 1) < 0.06 && std::abs(g.height() - 1) < 0.06 && std::abs(g.x()) < 0.04 &&
	    std::abs(g.y()) < 0.04)
		return full;
	return g.intersected(full);
}

QString num(double v)
{
	return QString::number(v, 'f', 6);
}

// ───────────────────────── 여러 파일 묶기 ─────────────────────────

bool isJoined(const QString &path)
{
	return path.endsWith(".ffconcat", Qt::CaseInsensitive);
}

QVector<JoinedPart> readJoined(const QString &path)
{
	QVector<JoinedPart> out;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
		return out;
	double offset = 0;
	const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
	for (QString line : lines) {
		line = line.trimmed();
		if (line.startsWith("file ")) {
			QString p = line.mid(5).trimmed();
			// 'a'\''b' 형태 되돌리기
			p.replace("'\\''", "\x01");
			p.remove('\'');
			p.replace("\x01", "'");
			JoinedPart part;
			part.file = QDir::toNativeSeparators(p);
			part.offset = offset;
			out.push_back(part);
		} else if (line.startsWith("duration ") && !out.isEmpty()) {
			out.back().duration = line.mid(9).trimmed().toDouble();
			offset = out.back().offset + out.back().duration;
		}
	}
	return out;
}

QString makeJoined(const QStringList &files, QString *error)
{
	if (files.size() < 2) {
		if (error)
			*error = "파일을 두 개 이상 골라 주세요";
		return {};
	}
	QString text = "ffconcat version 1.0\n# ERKillCut 묶음 — 한 방송이 여러 파일로 나뉜 것을 순서대로 이어 붙임\n";
	int w0 = 0, h0 = 0;
	bool audio0 = true;
	QStringList abs;
	for (int i = 0; i < files.size(); ++i) {
		const QString a = QFileInfo(files[i]).absoluteFilePath();
		const VideoInfo vi = probeOne(a);
		if (!vi.ok) {
			if (error)
				*error = QString("%1 을(를) 열 수 없어요: %2").arg(QFileInfo(a).fileName(), vi.error);
			return {};
		}
		if (i == 0) {
			w0 = vi.width;
			h0 = vi.height;
			audio0 = vi.hasAudio;
		} else if (vi.width != w0 || vi.height != h0) {
			if (error)
				*error = QString("해상도가 다른 파일은 묶을 수 없어요\n%1: %2×%3\n%4: %5×%6")
						 .arg(QFileInfo(files[0]).fileName())
						 .arg(w0)
						 .arg(h0)
						 .arg(QFileInfo(a).fileName())
						 .arg(vi.width)
						 .arg(vi.height);
			return {};
		} else if (vi.hasAudio != audio0) {
			if (error)
				*error = "소리가 있는 파일과 없는 파일은 묶을 수 없어요";
			return {};
		}
		QString q = QDir::fromNativeSeparators(a);
		q.replace("'", "'\\''");
		text += QString("file '%1'\nduration %2\n").arg(q, num(vi.duration));
		abs << a;
	}
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/joined";
	QDir().mkpath(dir);
	const QString name = QCryptographicHash::hash(abs.join('|').toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
	const QString path = dir + "/" + name + ".ffconcat";
	// 내용이 같으면 다시 쓰지 않음 (수정 시각이 바뀌면 분석 결과 저장 위치가 달라짐)
	QFile old(path);
	if (old.open(QIODevice::ReadOnly) && QString::fromUtf8(old.readAll()) == text)
		return path;
	old.close();
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) {
		if (error)
			*error = "묶음 목록을 저장하지 못했어요: " + path;
		return {};
	}
	return path;
}

QStringList inputArgs(const QString &path)
{
	if (isJoined(path))
		return {"-f", "concat", "-safe", "0", "-i", path};
	return {"-i", path};
}

static bool inaccurateSeek(const QString &file)
{
	static const QStringList ext{"ts", "m2ts", "mts", "m2t", "tp", "trp", "flv", "mpg", "mpeg", "vob"};
	return ext.contains(QFileInfo(file).suffix().toLower());
}

double seekPreRoll(const QString &path)
{
	static QHash<QString, double> cache;
	auto it = cache.constFind(path);
	if (it != cache.constEnd())
		return *it;
	QString probe;
	if (isJoined(path)) {
		for (const JoinedPart &p : readJoined(path))
			if (probe.isEmpty() && inaccurateSeek(p.file))
				probe = p.file;
	} else if (inaccurateSeek(path)) {
		probe = path;
	}
	double v = 0;
	if (!probe.isEmpty()) {
		// 앞 60초의 키프레임 간격을 재서 가장 긴 간격 + 0.5초 (키프레임만 풀어서 빠름)
		v = 3.0;
		QProcess p;
		setQuietProcess(p, false);
		p.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-skip_frame", "nokey", "-t", "60", "-i", probe, "-map", "0:v:0",
				       "-vf", "showinfo", "-f", "null", "-"});
		if (p.waitForFinished(30000)) {
			static const QRegularExpression re(R"(pts_time:\s*([0-9.]+))");
			auto it = re.globalMatch(QString::fromUtf8(p.readAllStandardError()));
			double last = -1, gap = 0;
			int n = 0;
			while (it.hasNext()) {
				const double t = it.next().captured(1).toDouble();
				if (last >= 0)
					gap = std::max(gap, t - last);
				last = t;
				++n;
			}
			if (n >= 3)
				v = std::clamp(gap + 0.5, 1.0, 12.0);
		} else {
			p.kill();
			p.waitForFinished(2000);
		}
	}
	cache.insert(path, v);
	return v;
}

QString displayName(const QString &path)
{
	if (!isJoined(path))
		return QFileInfo(path).fileName();
	const auto parts = readJoined(path);
	if (parts.isEmpty())
		return QFileInfo(path).fileName();
	return QString("%1 외 %2개").arg(QFileInfo(parts.first().file).fileName()).arg(parts.size() - 1);
}
