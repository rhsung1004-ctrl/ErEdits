// 화면 없이 엔진만 검사하는 도구
//   erkillcut_cli scan <영상> [fast] [x y w h]
//   erkillcut_cli export <영상> <결과.mp4> <시작-끝,시작-끝,...> [크로스페이드] [페이드인] [페이드아웃] ["시작-끝|글자;..." [글꼴파일]]
#include "Exporter.h"
#include "KillScanner.h"
#include "Util.h"

#include "FontManager.h"

#include <QGuiApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QGuiApplication app(argc, argv);
	FontManager::init();
	QTextStream out(stdout);
	const QStringList a = app.arguments();
	if (a.size() < 3) {
		out << "usage: scan <video> [fast] [x y w h] | export <video> <out> <s-e,...> [xfade fin fout]\n";
		return 2;
	}
	const VideoInfo info = probeVideo(a[2]);
	out << "info ok=" << info.ok << " " << info.width << "x" << info.height << " fps=" << info.fps
	    << " dur=" << info.duration << " audio=" << info.hasAudio << "\n";
	out.flush();
	if (!info.ok)
		return 1;

	if (a[1] == "scan") {
		bool fast = a.contains("fast");
		QRectF roi = defaultKillRoi(info.width, info.height);
		const int idx = fast ? 4 : 3;
		if (a.size() >= idx + 4)
			roi = QRectF(a[idx].toDouble(), a[idx + 1].toDouble(), a[idx + 2].toDouble(), a[idx + 3].toDouble());
		KillScanner sc;
		QVector<KillEvent> events;
		QObject::connect(&sc, &KillScanner::found, [&](const KillEvent &e) {
			events.push_back(e);
			out << "event t=" << formatTime(e.time) << " kill=" << e.kill << " assist=" << e.assist << "\n";
			out.flush();
		});
		QObject::connect(&sc, &KillScanner::finished, [&](bool ok, const QString &msg) {
			out << "finished ok=" << ok << " " << msg << "\n";
			const auto segs = buildSegments(events, SegmentParams{}, info.duration);
			for (const Segment &s : segs)
				out << "segment " << formatTime(s.start) << " - " << formatTime(s.end) << " kills=" << s.kills
				    << "\n";
			out.flush();
			app.exit(ok ? 0 : 1);
		});
		sc.start(a[2], roi, info.duration, fast ? KillScanner::Fast : KillScanner::Precise);
		return app.exec();
	}

	if (a[1] == "export" && a.size() >= 5) {
		QVector<Segment> segs;
		for (const QString &p : a[4].split(',', Qt::SkipEmptyParts)) {
			Segment s;
			s.start = p.section('-', 0, 0).toDouble();
			s.end = p.section('-', 1, 1).toDouble();
			segs.push_back(s);
		}
		ExportSettings s;
		s.outPath = a[3];
		s.height = 720;
		if (a.size() > 5)
			s.crossfade = a[5].toDouble();
		if (a.size() > 6)
			s.fadeIn = a[6].toDouble();
		if (a.size() > 7)
			s.fadeOut = a[7].toDouble();
		s.cpuOnly = true;
		QVector<Subtitle> subs;
		if (a.size() > 8)
			for (const QString &p : a[8].split(';', Qt::SkipEmptyParts)) {
				Subtitle sb;
				sb.start = p.section('|', 0, 0).section('-', 0, 0).toDouble();
				sb.end = p.section('|', 0, 0).section('-', 1, 1).toDouble();
				sb.text = p.section('|', 1).replace("\\n", "\n");
				sb.box = subs.size() % 2 == 1;
				if (a.size() > 9) {
					QString err;
					sb.font = FontManager::addFontFile(a[9], &err);
				}
				subs.push_back(sb);
			}
		Exporter ex;
		QObject::connect(&ex, &Exporter::finished, [&](bool ok, const QString &msg) {
			out << ex.lastFilter() << "\n";
			out << "finished ok=" << ok << " " << msg << "\n";
			out.flush();
			app.exit(ok ? 0 : 1);
		});
		if (qEnvironmentVariableIsSet("COMBO"))
			for (Subtitle &sb : subs)
				sb.combo = qEnvironmentVariable("COMBO");
		ex.start(a[2], info, segs, subs, s);
		return app.exec();
	}
	return 2;
}
