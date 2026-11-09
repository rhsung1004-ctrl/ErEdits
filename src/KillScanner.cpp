#include "KillScanner.h"

#include "Util.h"

namespace {
constexpr double kRate = 5.0; // 초당 검사 장면 수 (ERShorts 와 같음)
}

KillScanner::KillScanner(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, &KillScanner::onData);
	connect(&m_proc, &QProcess::readyReadStandardError, this, [this] {
		m_err += QString::fromUtf8(m_proc.readAllStandardError());
		if (m_err.size() > 4000)
			m_err = m_err.right(4000);
	});
	connect(&m_proc, &QProcess::finished, this, &KillScanner::onFinished);
}

KillScanner::~KillScanner()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

void KillScanner::start(const QString &path, const QRectF &roi, double duration, Mode mode)
{
	cancel();
	m_path = path;
	m_roi = roi;
	m_duration = duration;
	m_mode = mode;
	launch(true);
}

void KillScanner::cancel()
{
	if (m_proc.state() == QProcess::NotRunning)
		return;
	m_cancelled = true;
	m_proc.kill();
	m_proc.waitForFinished(3000);
}

void KillScanner::launch(bool hwaccel)
{
	m_hw = hwaccel;
	m_cancelled = false;
	m_watcher.reset();
	m_buf.clear();
	m_err.clear();
	m_frames = 0;
	m_lastReportFrame = -1000;
	m_found = 0;
	m_results.reset();
	m_withResults = !m_game.isEmpty();
	m_withKill = !m_roi.isEmpty();
	m_withChat = !m_chat.isEmpty() && m_mode == Precise;
	m_chatMeter.reset();
	m_chatLines.clear();

	// 지정 영역만 잘라 160x32 로 줄이고 밝기만 (영역 비율로 자르므로 해상도와 무관)
	auto crop = [](const QRectF &r) {
		return QString("crop=w='iw*%1':h='ih*%2':x='iw*%3':y='ih*%4'")
			.arg(num(r.width()), num(r.height()), num(r.x()), num(r.y()));
	};
	const QString killChain = crop(m_roi) + QString(",scale=%1:%2:flags=area,format=gray")
							.arg(HudKillWatcher::kW)
							.arg(HudKillWatcher::kH);
	QStringList args{"-hide_banner", "-nostdin", "-loglevel", "error"};
	if (m_mode == Fast)
		args << "-skip_frame" << "nokey";
	if (hwaccel)
		args << "-hwaccel" << "auto";
	args << inputArgs(m_path) << "-an" << "-sn" << "-dn";
	if (m_withKill && !m_withResults && !m_withChat) {
		args << "-vf" << QString("fps=%1,").arg(kRate) + killChain << "-f" << "rawvideo" << "-pix_fmt" << "gray"
		     << "-";
	} else {
		// 같은 장면을 나눠 각각 잘라 너비 320 RGB 로 맞춘 뒤 위아래로 붙임
		QStringList chains, labels;
		const int W = rank::kSmallW;
		if (m_withKill)
			chains << QString("%1,format=rgb24,pad=%2:%3:0:0:black").arg(killChain).arg(W).arg(HudKillWatcher::kH);
		if (m_withResults)
			chains << crop(m_game) + QString(",scale=%1:%2:flags=area,format=rgb24").arg(W).arg(rank::kSmallH);
		if (m_withChat)
			chains << crop(m_chat) + QString(",scale=%1:%2:flags=area,format=gray,format=rgb24,pad=%3:%2:0:0:black")
							 .arg(ChatMeter::kW)
							 .arg(ChatMeter::kH)
							 .arg(W);
		QString fc = QString("[0:v]fps=%1").arg(kRate);
		if (chains.size() == 1) {
			fc += "," + chains[0] + "[out]";
		} else {
			fc += QString(",split=%1").arg(chains.size());
			for (int i = 0; i < chains.size(); ++i)
				fc += QString("[s%1]").arg(i);
			fc += ";";
			for (int i = 0; i < chains.size(); ++i)
				fc += QString("[s%1]%2[p%1];").arg(i).arg(chains[i]);
			for (int i = 0; i < chains.size(); ++i)
				fc += QString("[p%1]").arg(i);
			fc += QString("vstack=inputs=%1[out]").arg(chains.size());
		}
		args << "-filter_complex" << fc << "-map" << "[out]" << "-f" << "rawvideo" << "-pix_fmt" << "rgb24" << "-";
	}
	setQuietProcess(m_proc, true);
	m_proc.start(ffmpegPath(), args);
}

void KillScanner::onData()
{
	m_buf += m_proc.readAllStandardOutput();
	const int kw = HudKillWatcher::kW, kh = HudKillWatcher::kH, W = rank::kSmallW;
	const bool packed = !(m_withKill && !m_withResults && !m_withChat);
	const int rows = (m_withKill ? kh : 0) + (m_withResults ? rank::kSmallH : 0) + (m_withChat ? ChatMeter::kH : 0);
	const int fsz = packed ? W * rows * 3 : kw * kh;
	const int n = int(m_buf.size() / fsz);
	if (n <= 0)
		return;
	std::vector<HudKillWatcher::Event> events;
	std::vector<rank::Candidate> cands;
	for (int i = 0; i < n; ++i) {
		const double t = double(m_frames) / kRate;
		const auto *frame = reinterpret_cast<const uint8_t *>(m_buf.constData()) + size_t(i) * fsz;
		if (!packed) {
			m_watcher.feed(frame, t, &events);
		} else {
			const uint8_t *part = frame;
			if (m_withKill) {
				// 킬 숫자 밝기 (R=G=B), 왼쪽 160칸
				m_gray.resize(size_t(kw * kh));
				for (int y = 0; y < kh; ++y)
					for (int x = 0; x < kw; ++x)
						m_gray[size_t(y * kw + x)] = part[(size_t(y) * W + size_t(x)) * 3];
				m_watcher.feed(m_gray.data(), t, &events);
				part += size_t(kh) * W * 3;
			}
			if (m_withResults) {
				m_results.feed(part, t, &cands);
				part += size_t(rank::kSmallH) * W * 3;
			}
			if (m_withChat) {
				m_chatGray.resize(size_t(ChatMeter::kW * ChatMeter::kH));
				for (int y = 0; y < ChatMeter::kH; ++y)
					for (int x = 0; x < ChatMeter::kW; ++x)
						m_chatGray[size_t(y * ChatMeter::kW + x)] = part[(size_t(y) * W + size_t(x)) * 3];
				const int fresh = m_chatMeter.feed(m_chatGray.data());
				const int sec = int(t);
				if (m_chatLines.size() <= sec)
					m_chatLines.resize(sec + 1);
				m_chatLines[sec] += float(fresh);
			}
		}
		++m_frames;
	}
	m_buf.remove(0, n * fsz);
	for (const auto &e : events) {
		KillEvent k;
		k.time = e.time;
		k.kill = e.kill;
		k.assist = e.assist;
		++m_found;
		emit found(k);
	}
	for (const auto &c : cands)
		emit resultFound(ResultCandidate{c.t0, c.t1});
	if (m_frames - m_lastReportFrame >= 25) { // 5초 분량마다
		m_lastReportFrame = m_frames;
		emit progress(double(m_frames) / kRate, m_duration);
	}
}

void KillScanner::onFinished(int code, QProcess::ExitStatus st)
{
	onData();
	if (m_cancelled) {
		emit finished(false, "분석을 멈췄어요");
		return;
	}
	const bool ok = st == QProcess::NormalExit && code == 0;
	// 하드웨어 디코딩 때문에 바로 실패했으면 CPU 디코딩으로 한 번 더
	if (!ok && m_hw && m_frames < 25) {
		launch(false);
		return;
	}
	if (!ok && m_frames == 0) {
		emit finished(false, "영상을 읽지 못했어요: " + m_err.trimmed().right(300));
		return;
	}
	if (m_withResults) {
		std::vector<rank::Candidate> cands;
		m_results.finish(&cands);
		for (const auto &c : cands)
			emit resultFound(ResultCandidate{c.t0, c.t1});
	}
	emit progress(double(m_frames) / kRate, m_duration);
	emit finished(true, ok ? QString() : "영상 끝부분을 읽다가 멈췄어요 (그 앞까지는 분석됨)");
}
