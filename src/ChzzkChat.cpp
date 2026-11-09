#include "ChzzkChat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace {
// 테스트 때만 ERKILLCUT_CHZZK_API 로 바꿈
QString apiBase()
{
	return qEnvironmentVariable("ERKILLCUT_CHZZK_API", "https://api.chzzk.naver.com/service");
}

QNetworkRequest makeRequest(const QUrl &url, qint64 video)
{
	QNetworkRequest r(url);
	r.setHeader(QNetworkRequest::UserAgentHeader,
		    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36");
	r.setRawHeader("Accept", "application/json");
	r.setRawHeader("Origin", "https://chzzk.naver.com");
	r.setRawHeader("Referer", QString("https://chzzk.naver.com/video/%1").arg(video).toUtf8());
	r.setTransferTimeout(20000);
	return r;
}

// 이모티콘 표시 {:d_47:} 지우기
QString cleanText(QString t)
{
	static const QRegularExpression emo(R"(\{:[^{}:]*:\})");
	t.remove(emo);
	return t.simplified();
}
} // namespace

ChzzkChat::ChzzkChat(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

qint64 ChzzkChat::videoNumber(const QString &text)
{
	static const QRegularExpression url(R"(video/(\d+))");
	const auto m = url.match(text);
	if (m.hasMatch())
		return m.captured(1).toLongLong();
	const QString t = text.trimmed();
	bool ok = false;
	const qint64 v = t.toLongLong(&ok);
	return ok && v > 0 ? v : 0;
}

void ChzzkChat::start(qint64 videoNo)
{
	cancel();
	m_video = videoNo;
	m_cursor = 0;
	m_duration = 0;
	m_retries = 0;
	m_emptyPages = 0;
	m_out.clear();
	m_running = true;
	requestInfo();
}

void ChzzkChat::cancel()
{
	if (!m_running)
		return;
	m_running = false;
	if (m_reply) {
		m_reply->disconnect(this);
		m_reply->abort();
		m_reply->deleteLater();
	}
}

void ChzzkChat::fail(const QString &msg)
{
	m_running = false;
	emit finished(false, msg, m_out);
}

void ChzzkChat::requestInfo()
{
	m_reply = m_net->get(makeRequest(QUrl(QString("%1/v3/videos/%2").arg(apiBase()).arg(m_video)), m_video));
	connect(m_reply, &QNetworkReply::finished, this, [this, r = m_reply] {
		r->deleteLater();
		if (!m_running)
			return;
		const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (http == 404) {
			fail("그 번호의 다시보기를 찾지 못했어요. 주소를 확인해 주세요.");
			return;
		}
		if (r->error() == QNetworkReply::NoError) {
			const QJsonObject c = QJsonDocument::fromJson(r->readAll()).object()["content"].toObject();
			m_duration = c["duration"].toDouble();
			emit info(c["videoTitle"].toString(), m_duration);
		}
		// 정보를 못 받아도 채팅은 시도
		requestPage();
	});
}

QVector<ChatMessage> ChzzkChat::parsePage(const QByteArray &json, qint64 *next, QString *error)
{
	QVector<ChatMessage> out;
	*next = -1;
	QJsonParseError pe;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
	if (pe.error != QJsonParseError::NoError) {
		if (error)
			*error = "응답을 읽지 못했어요";
		return out;
	}
	const QJsonObject root = doc.object();
	if (root.contains("code") && root["code"].toInt(200) != 200) {
		if (error)
			*error = root["message"].toString("치지직이 요청을 거절했어요");
		return out;
	}
	const QJsonObject c = root["content"].toObject();
	for (const auto &v : c["videoChats"].toArray()) {
		const QJsonObject m = v.toObject();
		const QString text = cleanText(m["content"].toString());
		if (text.isEmpty())
			continue;
		out.push_back({m["playerMessageTime"].toDouble() / 1000.0, text});
	}
	const QJsonValue n = c["nextPlayerMessageTime"];
	if (!n.isNull() && !n.isUndefined())
		*next = qint64(n.toDouble());
	return out;
}

void ChzzkChat::requestPage()
{
	QUrl url(QString("%1/v1/videos/%2/chats").arg(apiBase()).arg(m_video));
	QUrlQuery q;
	q.addQueryItem("playerMessageTime", QString::number(m_cursor));
	q.addQueryItem("previousVideoChatSize", "50");
	url.setQuery(q);
	m_reply = m_net->get(makeRequest(url, m_video));
	connect(m_reply, &QNetworkReply::finished, this, [this, r = m_reply] {
		r->deleteLater();
		if (!m_running)
			return;
		const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (http == 429 || (r->error() != QNetworkReply::NoError && http != 401 && http != 403 && http != 404)) {
			if (++m_retries <= 5) { // 잠깐 쉬고 다시
				QTimer::singleShot(1500 * m_retries, this, [this] {
					if (m_running)
						requestPage();
				});
				return;
			}
			fail("채팅을 받는 중 연결이 끊겼어요: " + r->errorString() +
			     QString(" (%1개까지 받음 — 다시 누르면 처음부터)").arg(m_out.size()));
			return;
		}
		if (http == 401 || http == 403) {
			fail("치지직이 채팅을 내주지 않았어요 (연령 제한·구독자 전용 다시보기일 수 있어요).");
			return;
		}
		m_retries = 0;
		qint64 next = -1;
		QString err;
		const auto msgs = parsePage(r->readAll(), &next, &err);
		if (!err.isEmpty() && m_out.isEmpty()) {
			fail(err);
			return;
		}
		// 앞 장과 겹치는 메시지는 건너뜀
		const double last = m_out.isEmpty() ? -1 : m_out.back().time;
		for (const ChatMessage &m : msgs)
			if (m.time > last || (m.time == last && !m_out.isEmpty() && m.text != m_out.back().text))
				m_out.push_back(m);
		m_emptyPages = msgs.isEmpty() ? m_emptyPages + 1 : 0;
		emit progress(m_out.isEmpty() ? m_cursor / 1000.0 : m_out.back().time, m_duration, int(m_out.size()));
		if (next <= m_cursor || m_emptyPages >= 3) {
			m_running = false;
			emit finished(true, QString("채팅 %1개를 받았어요").arg(m_out.size()), m_out);
			return;
		}
		m_cursor = next;
		requestPage();
	});
}
