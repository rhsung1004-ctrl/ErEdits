// 화면 없이 엔진만 검사하는 도구
//   erkillcut_cli scan <영상> [fast] [x y w h]
//   erkillcut_cli rank <영상> [최대등수] [fast]       — 킬 + 결과 화면 찾기 → 킬 숫자 확인 → 등수 읽기 → 판별 결과
//   erkillcut_cli readhud <그림> [그림 ...]            — 킬 숫자 줄(TK·K·A)만 잘라 낸 그림에서 숫자 읽기
//   erkillcut_cli chzzkpage <응답.json>                — 치지직 다시보기 채팅 응답 한 장 읽기
//   erkillcut_cli highlights <영상> <채팅.json> [AI]   — 킬·소리·채팅 → 잘한 장면 점수·재밌는 장면 후보 (AI: 로컬 AI 확인까지)
//        채팅.json = [[초, "내용"], ...]  /  AI 설정은 ERKCLI_WHISPER, ERKCLI_WMODEL, ERKCLI_OLLAMA, ERKCLI_MODEL
//   erkillcut_cli readframe <그림> [그림 ...]          — 결과 화면 캡처 한 장에서 등수 읽기
//   erkillcut_cli export <영상> <결과.mp4> <시작-끝,시작-끝,...> [크로스페이드] [페이드인] [페이드아웃] ["시작-끝|글자;..." [글꼴파일]]
#include "Exporter.h"
#include "Games.h"
#include "KillScanner.h"
#include "KillVerifier.h"
#include "AudioLevels.h"
#include "ChzzkChat.h"
#include "Highlights.h"
#include "LocalAi.h"
#include "RankReader.h"
#include "Util.h"

#include "FontManager.h"

#include <QGuiApplication>
#include <QImage>
#include <memory>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTextStream>

#include <cstring>

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
	if (a[1] == "chzzkfetch" && a.size() >= 3) { // 영상 없이: 다시보기 주소/번호
		auto *cz = new ChzzkChat(&app);
		QObject::connect(cz, &ChzzkChat::info, [&out](const QString &t, double d) { out << "info " << t << " " << d << "\n"; });
		QObject::connect(cz, &ChzzkChat::progress, [&out](double at, double d, int n) { out << "progress " << at << "/" << d << " n=" << n << "\n"; });
		QObject::connect(cz, &ChzzkChat::finished, [&out, &app](bool ok, const QString &m, const QVector<ChatMessage> &c) {
			out << "finished ok=" << ok << " " << m << " count=" << c.size() << "\n";
			if (!c.isEmpty())
				out << "first=" << c.front().time << " " << c.front().text << " last=" << c.back().time << " " << c.back().text << "\n";
			out.flush();
			app.exit(0);
		});
		cz->start(ChzzkChat::videoNumber(a[2]));
		return app.exec();
	}

	if (a[1] == "chzzkpage" && a.size() >= 3) {
		QFile f(a[2]);
		f.open(QIODevice::ReadOnly);
		qint64 next = 0;
		QString err;
		const auto msgs = ChzzkChat::parsePage(f.readAll(), &next, &err);
		for (const ChatMessage &m : msgs)
			out << "chat " << m.time << " " << m.text << "\n";
		out << "next=" << next << " error=" << err << "\n";
		return 0;
	}

	if (a[1] == "readhud") { // 킬 숫자 줄만 잘라 낸 그림
		for (int i = 2; i < a.size(); ++i) {
			const int W = HudKillWatcher::kW * rank::kHudScale, H = HudKillWatcher::kH * rank::kHudScale;
			const QImage img = QImage(a[i]).convertToFormat(QImage::Format_RGB888).scaled(W, H, Qt::IgnoreAspectRatio,
												      Qt::SmoothTransformation);
			std::vector<uint8_t> rgb(size_t(W) * H * 3);
			for (int y = 0; y < H; ++y)
				memcpy(&rgb[size_t(y) * W * 3], img.constScanLine(y), size_t(W) * 3);
			int v[3] = {};
			bool weak = true;
			const bool ok = rank::readHudCounts(rgb.data(), W, H, v, &weak);
			out << a[i] << " ok=" << ok << " TK " << v[0] << " K " << v[1] << " A " << v[2] << " weak=" << weak << "\n";
		}
		return 0;
	}

	if (a[1] == "readframe") {
		for (int i = 2; i < a.size(); ++i) {
			const QImage img = QImage(a[i]).convertToFormat(QImage::Format_RGB888).scaled(
				rank::kFullW, rank::kFullH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			if (img.isNull()) {
				out << a[i] << " 읽기 실패\n";
				continue;
			}
			std::vector<uint8_t> rgb(size_t(rank::kFullW) * rank::kFullH * 3);
			for (int y = 0; y < rank::kFullH; ++y)
				memcpy(&rgb[size_t(y) * rank::kFullW * 3], img.constScanLine(y), size_t(rank::kFullW) * 3);
			const QImage small = img.scaled(rank::kSmallW, rank::kSmallH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			std::vector<uint8_t> srgb(size_t(rank::kSmallW) * rank::kSmallH * 3);
			for (int y = 0; y < rank::kSmallH; ++y)
				memcpy(&srgb[size_t(y) * rank::kSmallW * 3], small.constScanLine(y), size_t(rank::kSmallW) * 3);
			const rank::Reading r = rank::readRank(rgb.data());
			out << a[i] << " button=" << rank::findResultButton(srgb.data(), nullptr) << " screen=" << rank::screenName(r.screen)
			    << " rank=" << r.rank << " weak=" << r.weak << " d=" << r.distance << "\n";
		}
		return 0;
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

	if (a[1] == "highlights" && a.size() >= 4) {
		QFile cf(a[3]);
		cf.open(QIODevice::ReadOnly);
		const QVector<ChatMessage> chat = chatFromJson(QJsonDocument::fromJson(cf.readAll()).array());
		const bool useAi = a.size() > 4 && a[4] == "AI";
		const QRectF roi = defaultKillRoi(info.width, info.height);
		auto *sc = new KillScanner(&app);
		auto *kv = new KillVerifier(&app);
		auto *al = new AudioLevels(&app);
		auto *ai = new LocalAi(&app);
		auto events = std::make_shared<QVector<KillEvent>>();
		auto loud = std::make_shared<QVector<float>>();
		auto screen = std::make_shared<QVector<float>>();
		if (qEnvironmentVariableIsSet("ERKCLI_SCREENCHAT")) // 채팅 파일 대신 화면 채팅
			sc->setChatDetection(QRectF(0.735, 0.24, 0.265, 0.36));
		auto cands = std::make_shared<QVector<FunCandidate>>();
		SignalParams sp;
		QObject::connect(sc, &KillScanner::found, [=](const KillEvent &e) { events->push_back(e); });
		QObject::connect(sc, &KillScanner::finished, [=, &out](bool, const QString &) {
			if (sc->chatMeasured()) {
				*screen = sc->chatLines();
				out << "screen-chat";
				for (float v : *screen)
					out << " " << v;
				out << "\n";
			}
			kv->start(a[2], roi, *events);
		});
		QObject::connect(kv, &KillVerifier::finished, [=](const QVector<KillEvent> &ev) {
			*events = ev;
			al->start(a[2], info.duration);
		});
		QObject::connect(al, &AudioLevels::finished, [=, &out, &app](bool ok, const QVector<float> &db) {
			*loud = db;
			out << "audio ok=" << ok << " seconds=" << db.size() << "\n";
			const Signals sig = computeSignals(chat, *screen, *loud, info.duration, sp);
			QVector<Segment> segs = buildSegments(*events, SegmentParams{}, info.duration);
			scoreSegments(&segs, *events, {}, sig);
			for (const Segment &s : segs)
				out << "clip " << formatTime(s.start) << " - " << formatTime(s.end) << " score=" << s.score << " [" << s.note
				    << "]\n";
			*cands = findFunCandidates(sig, chat, sp, segs, info.duration);
			for (const FunCandidate &c : *cands)
				out << "fun peak=" << formatTime(c.peak) << " " << formatTime(c.start) << "~" << formatTime(c.end)
				    << " score=" << c.score << " [" << c.reason << "] sample=" << c.chatSample.mid(0, 3).join(" | ") << "\n";
			QVector<Segment> withFun = segs;
			for (int i : pickFunForBreaks(segs, *cands, 30)) {
				Segment f;
				f.kind = Segment::Fun;
				f.start = (*cands)[i].clipStart();
				f.end = (*cands)[i].clipEnd();
				withFun.push_back(f);
				out << "break-pick " << formatTime((*cands)[i].peak) << "\n";
			}
			fillToLength(&withFun, 60);
			for (const Segment &s : withFun)
				out << "fill60 " << formatTime(s.start) << " on=" << s.enabled << " kind=" << s.kind << " len=" << s.length()
				    << "\n";
			out.flush();
			if (!useAi) {
				app.exit(0);
				return;
			}
			AiConfig cfg;
			cfg.whisperExe = qEnvironmentVariable("ERKCLI_WHISPER");
			cfg.whisperModel = qEnvironmentVariable("ERKCLI_WMODEL");
			cfg.ollamaUrl = qEnvironmentVariable("ERKCLI_OLLAMA", "http://localhost:11434");
			cfg.ollamaModel = qEnvironmentVariable("ERKCLI_MODEL", "qwen3:8b");
			QVector<int> idx;
			for (int i = 0; i < cands->size(); ++i)
				idx.push_back(i);
			ai->start(a[2], cfg, *cands, idx);
		});
		QObject::connect(ai, &LocalAi::status, [&out](const QString &s) {
			out << "ai-status " << s << "\n";
			out.flush();
		});
		QObject::connect(ai, &LocalAi::candidateDone, [&out](int i, const FunCandidate &c) {
			out << "ai " << i << " state=" << c.ai << " score=" << c.aiScore << " title=" << c.title << " clip="
			    << formatTime(c.clipStart()) << "~" << formatTime(c.clipEnd()) << "\n   transcript: "
			    << QString(c.transcript).replace("\n", " / ") << "\n";
			out.flush();
		});
		QObject::connect(ai, &LocalAi::finished, [&out, &app](bool ok, const QString &m) {
			out << "ai finished ok=" << ok << " " << m << "\n";
			out.flush();
			app.exit(0);
		});
		sc->start(a[2], roi, info.duration, KillScanner::Precise);
		return app.exec();
	}

	if (a[1] == "rank") {
		const int maxRank = a.size() > 3 && a[3] != "fast" ? a[3].toInt() : 3;
		const bool fast = a.contains("fast");
		const QRectF roi = defaultKillRoi(info.width, info.height);
		const QRectF game(0, 0, 1, 1);
		KillScanner sc;
		KillVerifier kv;
		RankReader rd;
		QVector<KillEvent> events;
		QVector<ResultCandidate> cands;
		QObject::connect(&sc, &KillScanner::found, [&](const KillEvent &e) {
			events.push_back(e);
			out << "event t=" << formatTime(e.time) << " kill=" << e.kill << " assist=" << e.assist << "\n";
		});
		QObject::connect(&sc, &KillScanner::resultFound, [&](const ResultCandidate &c) {
			cands.push_back(c);
			out << "result-screen " << formatTime(c.t0) << " ~ " << formatTime(c.t1) << "\n";
		});
		QObject::connect(&sc, &KillScanner::finished, [&](bool ok, const QString &msg) {
			out << "scan finished ok=" << ok << " " << msg << "\n";
			out.flush();
			kv.start(a[2], roi, events);
		});
		QObject::connect(&kv, &KillVerifier::finished, [&](const QVector<KillEvent> &ev) {
			events = ev;
			static const char *names[] = {"미확인", "확인됨", "잘못 잡힘", "못 읽음"};
			for (const KillEvent &e : events)
				out << "verify t=" << formatTime(e.time) << " " << names[e.check] << " kill=" << e.kill << "x" << e.kills
				    << " assist=" << e.assist << "\n";
			out.flush();
			rd.start(a[2], game, cands);
		});
		QObject::connect(&rd, &RankReader::finished, [&](bool, const QVector<RankReading> &rs) {
			for (const RankReading &r : rs)
				out << "reading t=" << formatTime(r.time) << " " << rank::screenName(r.screen) << " " << r.rank
				    << "위" << (r.weak ? " (흐릿)" : "") << "\n";
			const auto games = buildGames(cands, rs, events, info.duration);
			GameFilter f;
			f.enabled = true;
			f.maxRank = maxRank;
			for (int i = 0; i < games.size(); ++i) {
				const GameResult &g = games[i];
				out << "game " << i + 1 << " " << formatTime(g.start) << " ~ " << formatTime(g.end) << " rank="
				    << g.rank << " " << statusText(g.status) << " kills=" << g.kills
				    << (keepGame(g, f) ? " 남김" : " 뺌") << " [" << g.evidence << "]\n";
			}
			const auto kept = filterEvents(events, games, f);
			for (const Segment &s : buildSegments(kept, SegmentParams{}, info.duration))
				out << "segment " << formatTime(s.start) << " - " << formatTime(s.end) << " kills=" << s.kills
				    << "\n";
			out.flush();
			app.exit(0);
		});
		sc.setResultDetection(game);
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
