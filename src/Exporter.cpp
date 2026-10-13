#include "Exporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {
QString fmt(double v)
{
	return QString::number(v, 'f', 3);
}
int even(double v)
{
	int i = int(std::lround(v));
	return std::max(2, i - (i % 2));
}
} // namespace

Exporter::Exporter(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] {
		m_out += m_proc.readAllStandardOutput();
		// -progress pipe:1 → "out_time_us=12345678" 줄들
		int nl;
		double last = -1;
		while ((nl = m_out.indexOf('\n')) >= 0) {
			const QByteArray line = m_out.left(nl).trimmed();
			m_out.remove(0, nl + 1);
			if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
				bool ok = false;
				const double us = line.mid(line.indexOf('=') + 1).toDouble(&ok);
				if (ok && us >= 0)
					last = us / 1e6;
			}
		}
		if (last >= 0 && m_total > 0)
			emit progress(std::clamp(last / m_total, 0.0, 1.0));
	});
	connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
		m_err += QString::fromUtf8(m_proc.readAllStandardError());
		if (m_err.size() > 6000)
			m_err = m_err.right(6000);
	});
	connect(&m_proc, &QProcess::finished, this, &Exporter::onFinished);
}

Exporter::~Exporter()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

double Exporter::effectiveCrossfade(const QVector<Segment> &segments, double wanted)
{
	if (segments.size() < 2 || wanted <= 0.04)
		return 0;
	double shortest = 1e9;
	for (const Segment &s : segments)
		shortest = std::min(shortest, s.length());
	return std::max(0.0, std::min(wanted, shortest / 2 - 0.05));
}

double Exporter::outputLength(const QVector<Segment> &segments, double crossfade)
{
	double sum = 0;
	for (const Segment &s : segments)
		sum += s.length();
	return std::max(0.0, sum - std::max(0, int(segments.size()) - 1) * crossfade);
}

QString Exporter::buildFilter(const QVector<Segment> &segs, const VideoInfo &info, const ExportSettings &s, int W, int H,
			      double fps, double *totalOut)
{
	const int n = int(segs.size());
	const double d = effectiveCrossfade(segs, s.crossfade);
	const QString rate = QString::number(fps, 'g', 6);
	QStringList parts;

	for (int i = 0; i < n; ++i) {
		const QString L = fmt(segs[i].length());
		// 길이를 프레임 단위로 정확히 맞추고(모자라면 마지막 장면 유지), 16:9 캔버스 가운데에 맞춤
		parts << QString("[%1:v]setpts=PTS-STARTPTS,fps=%2,tpad=stop_mode=clone:stop_duration=2,trim=duration=%3,"
				 "setpts=PTS-STARTPTS,scale=%4:%5:force_original_aspect_ratio=decrease:flags=bicubic,"
				 "pad=%4:%5:(ow-iw)/2:(oh-ih)/2:color=black,setsar=1,format=yuv420p[v%1]")
				 .arg(i)
				 .arg(rate, L)
				 .arg(W)
				 .arg(H);
		if (info.hasAudio)
			parts << QString("[%1:a]asetpts=PTS-STARTPTS,aresample=48000:async=1,"
					 "aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo,"
					 "apad=whole_dur=%2,atrim=duration=%2,asetpts=PTS-STARTPTS[a%1]")
					 .arg(i)
					 .arg(L);
		else
			parts << QString("anullsrc=r=48000:cl=stereo,atrim=duration=%2,asetpts=PTS-STARTPTS[a%1]").arg(i).arg(L);
	}

	QString vLast = "v0", aLast = "a0";
	if (n > 1 && d > 0) {
		double cum = segs[0].length();
		for (int k = 1; k < n; ++k) {
			const double offset = cum - k * d; // 앞까지 이어진 영상이 끝나기 d초 전
			const QString vo = QString("vx%1").arg(k), ao = QString("ax%1").arg(k);
			parts << QString("[%1][v%2]xfade=transition=fade:duration=%3:offset=%4[%5]")
					 .arg(vLast)
					 .arg(k)
					 .arg(fmt(d), fmt(offset), vo);
			parts << QString("[%1][a%2]acrossfade=d=%3:c1=tri:c2=tri[%4]").arg(aLast).arg(k).arg(fmt(d), ao);
			vLast = vo;
			aLast = ao;
			cum += segs[k].length();
		}
	} else if (n > 1) {
		QString in;
		for (int i = 0; i < n; ++i)
			in += QString("[v%1][a%1]").arg(i);
		parts << in + QString("concat=n=%1:v=1:a=1[vcat][acat]").arg(n);
		vLast = "vcat";
		aLast = "acat";
	}

	const double total = outputLength(segs, d);
	if (totalOut)
		*totalOut = total;
	const double fi = std::min(s.fadeIn, total / 3);
	const double fo = std::min(s.fadeOut, total / 3);
	QString vf = "null", af = "anull";
	QStringList vfs, afs;
	if (fi > 0.04) {
		vfs << QString("fade=t=in:st=0:d=%1").arg(fmt(fi));
		afs << QString("afade=t=in:st=0:d=%1").arg(fmt(fi));
	}
	if (fo > 0.04) {
		vfs << QString("fade=t=out:st=%1:d=%2").arg(fmt(total - fo), fmt(fo));
		afs << QString("afade=t=out:st=%1:d=%2").arg(fmt(total - fo), fmt(fo));
	}
	if (!vfs.isEmpty()) {
		vf = vfs.join(',');
		af = afs.join(',');
	}
	parts << QString("[%1]%2,format=yuv420p[vout]").arg(vLast, vf);
	parts << QString("[%1]%2[aout]").arg(aLast, af);
	return parts.join(";\n");
}

bool Exporter::ffmpegHasNewScriptOption()
{
	// ffmpeg 7 부터 "-/filter_complex 파일" 형식 (-filter_complex_script 는 사라지는 중)
	static int cached = -1;
	if (cached < 0) {
		QProcess p;
		setQuietProcess(p, false);
		p.start(ffmpegPath(), {"-hide_banner", "-version"});
		p.waitForFinished(10000);
		const QString first = QString::fromUtf8(p.readAllStandardOutput()).section('\n', 0, 0);
		static const QRegularExpression re(R"(ffmpeg version n?(\d+))");
		const auto m = re.match(first);
		if (first.contains("version N-"))
			cached = 1; // 개발판(git master)
		else
			cached = (m.hasMatch() && m.captured(1).toInt() >= 7) ? 1 : 0;
	}
	return cached == 1;
}

QString Exporter::detectEncoder()
{
	static QString cached;
	if (!cached.isEmpty())
		return cached;
	for (const QString enc : {"h264_nvenc", "h264_amf", "h264_qsv"}) {
		QProcess p;
		setQuietProcess(p, false);
		p.start(ffmpegPath(), {"-hide_banner", "-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
				       "color=c=black:s=1280x720:r=30:d=0.5", "-c:v", enc, "-f", "null", "-"});
		if (p.waitForFinished(15000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
			cached = enc;
			return cached;
		}
		p.kill();
	}
	cached = "libx264";
	return cached;
}

QStringList Exporter::encoderArgs(const QString &enc, double fps)
{
	QStringList a{"-c:v", enc};
	if (enc == "h264_nvenc")
		a << "-preset" << "p5" << "-rc" << "vbr" << "-cq" << "20" << "-b:v" << "0";
	else if (enc == "h264_amf")
		a << "-quality" << "quality" << "-rc" << "cqp" << "-qp_i" << "20" << "-qp_p" << "22";
	else if (enc == "h264_qsv")
		a << "-preset" << "slow" << "-global_quality" << "21";
	else
		a << "-preset" << "medium" << "-crf" << "19";
	a << "-g" << QString::number(std::max(1, int(std::lround(fps * 2))));
	return a;
}

void Exporter::start(const QString &input, const VideoInfo &info, const QVector<Segment> &segments,
		     const ExportSettings &s)
{
	cancel();
	m_input = input;
	m_info = info;
	m_segs.clear();
	for (const Segment &seg : segments)
		if (seg.enabled && seg.length() > 0.2)
			m_segs.push_back(seg);
	m_set = s;
	if (m_segs.isEmpty()) {
		emit finished(false, "내보낼 클립이 없어요");
		return;
	}
	run(s.cpuOnly ? QStringLiteral("libx264") : detectEncoder());
}

void Exporter::run(const QString &encoder)
{
	m_encoder = encoder;
	m_cancelled = false;
	m_err.clear();
	m_out.clear();

	const int outH = even(m_set.height > 0 ? m_set.height : m_info.height);
	const int outW = even(outH * 16.0 / 9.0);
	double fps = m_set.fps > 0 ? m_set.fps : m_info.fps;
	fps = std::clamp(fps, 10.0, 60.0);

	const QString filter = buildFilter(m_segs, m_info, m_set, outW, outH, fps, &m_total);
	m_script = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/erkillcut_filter_XXXXXX.txt");
	if (!m_script->open()) {
		emit finished(false, "임시 파일을 만들지 못했어요");
		return;
	}
	m_script->write(filter.toUtf8());
	m_script->flush();
	m_script->close();

	QStringList args{"-hide_banner", "-nostdin", "-y", "-loglevel", "error", "-progress", "pipe:1", "-nostats"};
	for (const Segment &seg : m_segs) {
		// 앞뒤로 조금 넉넉히 읽고 필터에서 정확히 자름
		args << "-ss" << fmt(seg.start) << "-t" << fmt(seg.length() + 1.0) << "-i" << m_input;
	}
	if (ffmpegHasNewScriptOption())
		args << "-/filter_complex" << m_script->fileName();
	else
		args << "-filter_complex_script" << m_script->fileName();
	args << "-map" << "[vout]" << "-map" << "[aout]";
	args << encoderArgs(encoder, fps);
	args << "-c:a" << "aac" << "-b:a" << "192k" << "-movflags" << "+faststart" << m_set.outPath;

	QDir().mkpath(QFileInfo(m_set.outPath).absolutePath());
	setQuietProcess(m_proc, false);
	m_proc.start(ffmpegPath(), args);
}

void Exporter::cancel()
{
	if (m_proc.state() == QProcess::NotRunning)
		return;
	m_cancelled = true;
	m_proc.kill();
	m_proc.waitForFinished(3000);
}

void Exporter::onFinished(int code, QProcess::ExitStatus st)
{
	if (m_cancelled) {
		QFile::remove(m_set.outPath);
		emit finished(false, "내보내기를 멈췄어요");
		return;
	}
	if (st == QProcess::NormalExit && code == 0) {
		emit progress(1.0);
		emit finished(true, QString("완료 (%1, 인코더 %2)").arg(formatTime(m_total, false), m_encoder));
		return;
	}
	// 그래픽카드 인코더가 도중에 실패하면 CPU 인코더로 다시
	if (m_encoder != "libx264") {
		QFile::remove(m_set.outPath);
		run("libx264");
		return;
	}
	QFile::remove(m_set.outPath);
	emit finished(false, "내보내기 실패: " + m_err.trimmed().right(400));
}
