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

	// 지정 영역만 잘라 160x32 로 줄이고 밝기만 (영역 비율로 자르므로 해상도와 무관)
	const QString vf = QString("fps=%1,crop=w='iw*%2':h='ih*%3':x='iw*%4':y='ih*%5',scale=%6:%7:flags=area,format=gray")
				   .arg(kRate)
				   .arg(num(m_roi.width()), num(m_roi.height()), num(m_roi.x()), num(m_roi.y()))
				   .arg(HudKillWatcher::kW)
				   .arg(HudKillWatcher::kH);
	QStringList args{"-hide_banner", "-nostdin", "-loglevel", "error"};
	if (m_mode == Fast)
		args << "-skip_frame" << "nokey";
	if (hwaccel)
		args << "-hwaccel" << "auto";
	args << "-i" << m_path << "-an" << "-sn" << "-dn" << "-vf" << vf << "-f" << "rawvideo" << "-pix_fmt" << "gray"
	     << "-";
	setQuietProcess(m_proc, true);
	m_proc.start(ffmpegPath(), args);
}

void KillScanner::onData()
{
	m_buf += m_proc.readAllStandardOutput();
	const int fsz = HudKillWatcher::kW * HudKillWatcher::kH;
	const int n = int(m_buf.size() / fsz);
	if (n <= 0)
		return;
	std::vector<HudKillWatcher::Event> events;
	for (int i = 0; i < n; ++i) {
		const double t = double(m_frames) / kRate;
		m_watcher.feed(reinterpret_cast<const uint8_t *>(m_buf.constData()) + size_t(i) * fsz, t, &events);
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
	emit progress(double(m_frames) / kRate, m_duration);
	emit finished(true, ok ? QString() : "영상 끝부분을 읽다가 멈췄어요 (그 앞까지는 분석됨)");
}
