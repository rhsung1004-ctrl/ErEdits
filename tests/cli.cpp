// 화면 없이 엔진만 검사하는 도구
//   erkillcut_cli scan <영상> [fast] [x y w h]
//   erkillcut_cli export <영상> <결과.mp4> <시작-끝,시작-끝,...> [크로스페이드] [페이드인] [페이드아웃]
#include "Exporter.h"
#include "KillScanner.h"
#include "Util.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
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
		double total = 0;
		out << Exporter::buildFilter(segs, info, s, 1280, 720, info.fps, &total) << "\nexpected length " << total
		    << "\n";
		out.flush();
		Exporter ex;
		QObject::connect(&ex, &Exporter::finished, [&](bool ok, const QString &msg) {
			out << "finished ok=" << ok << " " << msg << "\n";
			out.flush();
			app.exit(ok ? 0 : 1);
		});
		ex.start(a[2], info, segs, s);
		return app.exec();
	}
	return 2;
}
