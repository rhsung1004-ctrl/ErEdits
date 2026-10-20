#include "Util.h"

#include "HudKillWatcher.h"

#include <QCoreApplication>
#include <QFileInfo>
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

VideoInfo probeVideo(const QString &path)
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
		auto mf = reFps.match(line);
		if (!mf.hasMatch())
			mf = reTbr.match(line);
		if (mf.hasMatch())
			info.fps = mf.captured(1).toDouble();
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
