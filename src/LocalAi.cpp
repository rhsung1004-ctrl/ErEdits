#include "LocalAi.h"

#include "Util.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>

#include <algorithm>
#include <cmath>

// ───────────────────────── 설정 ─────────────────────────

AiConfig AiConfig::load()
{
	QSettings s;
	AiConfig c;
	c.whisperExe = s.value("ai/whisperExe").toString();
	c.whisperModel = s.value("ai/whisperModel").toString();
	c.ollamaUrl = s.value("ai/ollamaUrl", c.ollamaUrl).toString();
	c.ollamaModel = s.value("ai/ollamaModel", c.ollamaModel).toString();
	return c;
}

void AiConfig::save() const
{
	QSettings s;
	s.setValue("ai/whisperExe", whisperExe);
	s.setValue("ai/whisperModel", whisperModel);
	s.setValue("ai/ollamaUrl", ollamaUrl);
	s.setValue("ai/ollamaModel", ollamaModel);
}

bool AiConfig::whisperReady() const
{
	return QFileInfo(whisperExe).isFile() && QFileInfo(whisperModel).isFile();
}

// ───────────────────────── 읽고 쓰기 (테스트 가능) ─────────────────────────

QVector<LocalAi::Line> LocalAi::parseWhisperJson(const QByteArray &json, double offset)
{
	QVector<Line> out;
	const QJsonArray tr = QJsonDocument::fromJson(json).object()["transcription"].toArray();
	for (const auto &v : tr) {
		const QJsonObject o = v.toObject();
		const QJsonObject off = o["offsets"].toObject();
		const QString text = o["text"].toString().trimmed();
		if (text.isEmpty() || text == "[BLANK_AUDIO]" || text.startsWith("[") || text.startsWith("("))
			continue;
		out.push_back({offset + off["from"].toDouble() / 1000.0, offset + off["to"].toDouble() / 1000.0, text});
	}
	return out;
}

QString LocalAi::buildPrompt(const FunCandidate &c, const QVector<Line> &lines)
{
	QString p;
	p += "너는 게임 방송(이터널리턴) 다시보기를 롱폼 영상으로 편집하는 편집자를 돕는다.\n"
	     "롱폼은 게임 속 잘한 장면 위주이고, 중간중간 분위기를 바꿔 줄 '재밌는 장면'을 짧게 넣는다.\n"
	     "아래는 시청자 채팅이 갑자기 몰린 순간 앞뒤의 방송인 말(음성 인식, 틀린 글자가 있을 수 있음)과 그때 채팅이다.\n\n";
	p += "[방송인 말] (번호, 초, 내용)\n";
	if (lines.isEmpty())
		p += "(말이 거의 없음)\n";
	int peakLine = -1;
	for (int i = 0; i < lines.size(); ++i) {
		p += QString("%1. %2초 %3\n").arg(i).arg(lines[i].t0 - (c.peak - kBefore), 0, 'f', 0).arg(lines[i].text);
		if (peakLine < 0 && lines[i].t1 >= c.peak - 1)
			peakLine = i;
	}
	p += QString("\n채팅 반응이 터진 때: 약 %1초").arg(kBefore, 0, 'f', 0);
	if (peakLine >= 0)
		p += QString(" (%1번 줄 근처)").arg(peakLine);
	p += "\n\n[그때 채팅]\n";
	if (c.chatSample.isEmpty())
		p += "(채팅 내용은 모름 — 채팅 속도만 빨라짐. 방송인 말로 판단)\n";
	for (const QString &m : c.chatSample)
		p += "- " + m + "\n";
	p += "\n판단할 것:\n"
	     "1) 롱폼에 넣을 만큼 재밌는 장면인가? (드립·리액션·웃긴 상황·티키타카는 예, 단순 인사·후원 읽기·광고·잡음은 아니오)\n"
	     "2) 재밌는 정도 1~10\n"
	     "3) 이 장면만 떼어 봐도 이해되도록 시작 줄과 끝 줄 번호 (반응이 터진 줄은 꼭 포함, 보통 10~40초)\n"
	     "4) 편집자가 알아보기 쉬운 한 줄 설명 (20자 안쪽, 한국어)\n\n"
	     "JSON 하나로만 답해:\n"
	     "{\"funny\": true, \"score\": 7, \"start_line\": 3, \"end_line\": 9, \"title\": \"...\", \"reason\": \"...\"}";
	return p;
}

bool LocalAi::applyAnswer(const QByteArray &answerIn, const QVector<Line> &lines, FunCandidate *c)
{
	QByteArray a = answerIn;
	// <think>…</think> 나 앞뒤 말이 붙어 와도 JSON 부분만
	const qsizetype b = a.indexOf('{'), e = a.lastIndexOf('}');
	if (b < 0 || e <= b)
		return false;
	a = a.mid(b, e - b + 1);
	QJsonParseError pe;
	const QJsonObject o = QJsonDocument::fromJson(a, &pe).object();
	if (pe.error != QJsonParseError::NoError || !o.contains("funny"))
		return false;
	const QJsonValue f = o["funny"];
	const bool funny = f.isBool() ? f.toBool() : (f.toString() == "true" || f.toString() == "예");
	c->ai = funny ? FunCandidate::Keep : FunCandidate::Drop;
	c->aiScore = std::clamp(o["score"].toInt(funny ? 5 : 2), 1, 10);
	c->title = o["title"].toString().trimmed().left(30);
	if (c->title.isEmpty())
		c->title = o["reason"].toString().trimmed().left(30);
	const int s = o["start_line"].toInt(-1), en = o["end_line"].toInt(-1);
	if (!lines.isEmpty() && s >= 0 && en >= s && en < lines.size()) {
		double st = lines[s].t0 - 0.5;
		double ed = std::max(lines[en].t1 + 1.5, c->peak + 3.0); // 웃음이 터진 순간까지는 보이게
		st = std::min(st, c->peak - 4.0);
		if (ed - st > 90)
			st = ed - 90;
		if (ed - st >= 6) {
			c->aiStart = std::max(0.0, st);
			c->aiEnd = ed;
		}
	}
	return true;
}

// ───────────────────────── 실행 ─────────────────────────

LocalAi::LocalAi(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this))
{
	m_proc.setProcessChannelMode(QProcess::MergedChannels);
	connect(&m_proc, &QProcess::readyRead, this, [this] { m_lastError = QString::fromLocal8Bit(m_proc.readAll()).right(400); });
}

LocalAi::~LocalAi()
{
	m_cancelled = true;
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
}

void LocalAi::cancel()
{
	if (!m_running)
		return;
	m_cancelled = true;
	m_running = false;
	disconnect(&m_proc, &QProcess::finished, this, nullptr);
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(3000);
	}
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->abort();
	}
	m_tmp.reset();
}

void LocalAi::finish(bool ok, const QString &msg)
{
	m_running = false;
	m_tmp.reset();
	emit finished(ok, msg);
}

void LocalAi::start(const QString &videoPath, const AiConfig &cfg, const QVector<FunCandidate> &cands,
		    const QVector<int> &indices)
{
	cancel();
	m_cancelled = false;
	m_video = videoPath;
	m_cfg = cfg;
	m_cands = cands;
	m_todo = indices;
	m_lines = QVector<QVector<Line>>(indices.size());
	m_pos = 0;
	m_judged = 0;
	m_running = true;
	m_tmp = std::make_unique<QTemporaryDir>();
	if (m_todo.isEmpty()) {
		finish(true, "확인할 후보가 없어요");
		return;
	}
	if (cfg.whisperReady()) {
		nextTranscribe();
	} else {
		emit status("음성 인식(whisper) 설정이 없어 채팅만 보고 판단해요…");
		nextJudge();
	}
}

void LocalAi::nextTranscribe()
{
	if (m_cancelled)
		return;
	if (m_pos >= m_todo.size()) {
		m_pos = 0;
		nextJudge();
		return;
	}
	const FunCandidate &c = m_cands[m_todo[m_pos]];
	emit status(QString("음성 인식 중… %1 / %2 (%3)").arg(m_pos + 1).arg(m_todo.size()).arg(formatTime(c.peak, false)));
	const double from = std::max(0.0, c.peak - kBefore);
	disconnect(&m_proc, &QProcess::finished, this, nullptr);
	connect(&m_proc, &QProcess::finished, this, &LocalAi::onExtracted);
	setQuietProcess(m_proc, false);
	m_proc.start(ffmpegPath(), QStringList{"-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-ss", num(from), "-t",
					       num(c.peak + kAfter - from)} +
					   inputArgs(m_video) +
					   QStringList{"-vn", "-ac", "1", "-ar", "16000", "-c:a", "pcm_s16le", m_tmp->filePath("a.wav")});
}

void LocalAi::onExtracted(int code, QProcess::ExitStatus)
{
	if (m_cancelled)
		return;
	disconnect(&m_proc, &QProcess::finished, this, nullptr);
	if (code != 0) {
		++m_pos; // 이 후보는 말 없이
		nextTranscribe();
		return;
	}
	connect(&m_proc, &QProcess::finished, this, &LocalAi::onWhisper);
	QFile::remove(m_tmp->filePath("a.json"));
	setQuietProcess(m_proc, false);
	m_proc.start(m_cfg.whisperExe, {"-m", m_cfg.whisperModel, "-f", m_tmp->filePath("a.wav"), "-l", "ko", "-oj",
					"-of", m_tmp->filePath("a"), "-np"});
}

void LocalAi::onWhisper(int code, QProcess::ExitStatus st)
{
	if (m_cancelled)
		return;
	disconnect(&m_proc, &QProcess::finished, this, nullptr);
	QFile f(m_tmp->filePath("a.json"));
	if (st != QProcess::NormalExit || code != 0 || !f.open(QIODevice::ReadOnly)) {
		if (m_pos == 0) { // 첫 후보부터 실패 → 설정 문제
			finish(false, "whisper 실행에 실패했어요. [AI 설정]에서 whisper-cli.exe 와 모델 파일을 확인해 주세요.\n" +
					      m_lastError.trimmed());
			return;
		}
		++m_pos;
		nextTranscribe();
		return;
	}
	const FunCandidate &c = m_cands[m_todo[m_pos]];
	m_lines[m_pos] = parseWhisperJson(f.readAll(), std::max(0.0, c.peak - kBefore));
	QStringList t;
	for (const Line &l : m_lines[m_pos])
		t << QString("%1  %2").arg(formatTime(l.t0, false), l.text);
	m_cands[m_todo[m_pos]].transcript = t.join("\n");
	++m_pos;
	nextTranscribe();
}

void LocalAi::nextJudge()
{
	if (m_cancelled)
		return;
	if (m_pos >= m_todo.size()) {
		finish(true, QString("후보 %1개를 확인했어요").arg(m_judged));
		return;
	}
	const FunCandidate &c = m_cands[m_todo[m_pos]];
	emit status(QString("AI 판단 중… %1 / %2 (%3)").arg(m_pos + 1).arg(m_todo.size()).arg(formatTime(c.peak, false)));
	QJsonObject body{{"model", m_cfg.ollamaModel},
			 {"prompt", buildPrompt(c, m_lines[m_pos])},
			 {"stream", false},
			 {"format", "json"},
			 {"options", QJsonObject{{"temperature", 0.2}, {"num_ctx", 4096}}}};
	postOllama(QJsonDocument(body).toJson(QJsonDocument::Compact), true);
}

void LocalAi::postOllama(const QByteArray &bodyIn, bool withThink)
{
	QByteArray body = bodyIn;
	if (withThink) { // 생각 모드가 있는 모델(qwen3 등)은 끄기 — 빠르고 JSON 만 나옴
		QJsonObject o = QJsonDocument::fromJson(bodyIn).object();
		o["think"] = false;
		body = QJsonDocument(o).toJson(QJsonDocument::Compact);
	}
	QNetworkRequest r(QUrl(m_cfg.ollamaUrl.trimmed() + "/api/generate"));
	r.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	r.setTransferTimeout(180000);
	m_reply = m_net->post(r, body);
	connect(m_reply, &QNetworkReply::finished, this, [this, rep = m_reply, bodyIn, withThink] {
		rep->deleteLater();
		if (m_cancelled)
			return;
		const QByteArray data = rep->readAll();
		const QJsonObject o = QJsonDocument::fromJson(data).object();
		if (rep->error() != QNetworkReply::NoError || o.contains("error")) {
			const QString err = o["error"].toString(rep->errorString());
			if (withThink && err.contains("think", Qt::CaseInsensitive)) { // 생각 모드 없는 모델
				postOllama(bodyIn, false);
				return;
			}
			if (rep->error() == QNetworkReply::ConnectionRefusedError || rep->error() == QNetworkReply::HostNotFoundError) {
				finish(false, "Ollama 에 연결하지 못했어요. Ollama 가 켜져 있는지 확인해 주세요.");
				return;
			}
			if (err.contains("not found", Qt::CaseInsensitive)) {
				finish(false, QString("Ollama 에 '%1' 모델이 없어요. 명령 창에서  ollama pull %1  을 먼저 실행해 주세요.")
						      .arg(m_cfg.ollamaModel));
				return;
			}
			FunCandidate c = m_cands[m_todo[m_pos]];
			c.ai = FunCandidate::Failed;
			emit candidateDone(m_todo[m_pos], c);
		} else {
			FunCandidate &c = m_cands[m_todo[m_pos]];
			if (!applyAnswer(o["response"].toString().toUtf8(), m_lines[m_pos], &c))
				c.ai = FunCandidate::Failed;
			else
				++m_judged;
			emit candidateDone(m_todo[m_pos], c);
		}
		++m_pos;
		nextJudge();
	});
}

void LocalAi::check(const AiConfig &cfg)
{
	QStringList msg;
	bool ok = true;
	if (cfg.whisperExe.isEmpty() && cfg.whisperModel.isEmpty()) {
		msg << "• 음성 인식: 설정 안 함 (채팅만 보고 판단)";
	} else if (!QFileInfo(cfg.whisperExe).isFile()) {
		msg << "• 음성 인식: whisper-cli.exe 를 찾지 못했어요";
		ok = false;
	} else if (!QFileInfo(cfg.whisperModel).isFile()) {
		msg << "• 음성 인식: 모델 파일(.bin)을 찾지 못했어요";
		ok = false;
	} else {
		msg << "• 음성 인식: 준비됨";
	}
	QNetworkRequest r(QUrl(cfg.ollamaUrl.trimmed() + "/api/tags"));
	r.setTransferTimeout(5000);
	QNetworkReply *rep = m_net->get(r);
	connect(rep, &QNetworkReply::finished, this, [this, rep, cfg, msg, ok]() mutable {
		rep->deleteLater();
		if (rep->error() != QNetworkReply::NoError) {
			msg << "• Ollama: 연결 안 됨 — Ollama 를 설치하고 켜 주세요";
			emit checked(false, msg.join("\n"));
			return;
		}
		QStringList names;
		for (const auto &m : QJsonDocument::fromJson(rep->readAll()).object()["models"].toArray())
			names << m.toObject()["name"].toString();
		const QString want = cfg.ollamaModel.contains(':') ? cfg.ollamaModel : cfg.ollamaModel + ":latest";
		if (names.contains(want) || names.contains(cfg.ollamaModel)) {
			msg << QString("• Ollama: 준비됨 (%1)").arg(cfg.ollamaModel);
		} else {
			msg << QString("• Ollama: 켜져 있지만 '%1' 모델이 없어요 → 명령 창에서  ollama pull %1").arg(cfg.ollamaModel);
			if (!names.isEmpty())
				msg << "   있는 모델: " + names.join(", ");
			ok = false;
		}
		emit checked(ok, msg.join("\n"));
	});
}
