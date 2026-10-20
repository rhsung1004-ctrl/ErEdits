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

	// 지정 영역만 잘라 160x32 로 줄이고 밝기만 (영역 비율로 자르므로 해상도와 무관)
	const QString killChain = QString("crop=w='iw*%1':h='ih*%2':x='iw*%3':y='ih*%4',scale=%5:%6:flags=area,format=gray")
					  .arg(num(m_roi.width()), num(m_roi.height()), num(m_roi.x()), num(m_roi.y()))
					  .arg(HudKillWatcher::kW)
					  .arg(HudKillWatcher::kH);
	QStringList args{"-hide_banner", "-nostdin", "-loglevel", "error"};
	if (m_mode == Fast)
		args << "-skip_frame" << "nokey";
	if (hwaccel)
		args << "-hwaccel" << "auto";
	args << "-i" << m_path << "-an" << "-sn" << "-dn";
	if (!m_withResults) {
		args << "-vf" << QString("fps=%1,").arg(kRate) + killChain << "-f" << "rawvideo" << "-pix_fmt" << "gray"
		     << "-";
	} else {
		// 같은 장면을 둘로 나눠: 킬 숫자(밝기 → RGB, 오른쪽을 검게 채워 너비 320) + 게임 화면 320x180 → 위아래로 붙임
		const QString fc =
			QString("[0:v]fps=%1,split=2[a][b];"
				"[a]%2,format=rgb24,pad=%3:%4:0:0:black[k];"
				"[b]crop=w='iw*%5':h='ih*%6':x='iw*%7':y='ih*%8',scale=%3:%9:flags=area,format=rgb24[r];"
				"[k][r]vstack=inputs=2[out]")
				.arg(kRate)
				.arg(killChain)
				.arg(rank::kSmallW)
				.arg(HudKillWatcher::kH)
				.arg(num(m_game.width()), num(m_game.height()), num(m_game.x()), num(m_game.y()))
				.arg(rank::kSmallH);
		args << "-filter_complex" << fc << "-map" << "[out]" << "-f" << "rawvideo" << "-pix_fmt" << "rgb24" << "-";
	}
	setQuietProcess(m_proc, true);
	m_proc.start(ffmpegPath(), args);
}

void KillScanner::onData()
{
	m_buf += m_proc.readAllStandardOutput();
	const int kw = HudKillWatcher::kW, kh = HudKillWatcher::kH;
	const int fsz = m_withResults ? rank::kSmallW * (kh + rank::kSmallH) * 3 : kw * kh;
	const int n = int(m_buf.size() / fsz);
	if (n <= 0)
		return;
	std::vector<HudKillWatcher::Event> events;
	std::vector<rank::Candidate> cands;
	for (int i = 0; i < n; ++i) {
		const double t = double(m_frames) / kRate;
		const auto *frame = reinterpret_cast<const uint8_t *>(m_buf.constData()) + size_t(i) * fsz;
		if (!m_withResults) {
			m_watcher.feed(frame, t, &events);
		} else {
			// 위 32줄 왼쪽 160칸 = 킬 숫자 밝기 (R=G=B)
			m_gray.resize(size_t(kw * kh));
			for (int y = 0; y < kh; ++y)
				for (int x = 0; x < kw; ++x)
					m_gray[size_t(y * kw + x)] = frame[(size_t(y) * rank::kSmallW + size_t(x)) * 3];
			m_watcher.feed(m_gray.data(), t, &events);
			m_results.feed(frame + size_t(kh) * rank::kSmallW * 3, t, &cands);
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
