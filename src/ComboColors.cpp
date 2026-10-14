#include "ComboColors.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>

namespace {
QVector<ComboPreset> g_presets;
bool g_loaded = false;
bool g_hasOverride = false;
ComboPreset g_override;

QJsonArray toJson(const QVector<ComboPreset> &list)
{
	QJsonArray a;
	for (const ComboPreset &p : list) {
		QJsonArray keys;
		for (const ComboKey &k : p.keys)
			keys.append(QJsonObject{{"key", k.key}, {"color", k.color.name()}});
		a.append(QJsonObject{{"name", p.name}, {"dash", p.dash.name()}, {"keys", keys}});
	}
	return a;
}

QVector<ComboPreset> fromJson(const QJsonArray &a)
{
	QVector<ComboPreset> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		ComboPreset p;
		p.name = o["name"].toString().trimmed();
		p.dash = QColor(o["dash"].toString("#ffffff"));
		for (const auto &kv : o["keys"].toArray()) {
			const QJsonObject ko = kv.toObject();
			ComboKey k;
			k.key = ko["key"].toString().trimmed();
			k.color = QColor(ko["color"].toString("#ffffff"));
			if (!k.key.isEmpty())
				p.keys.push_back(k);
		}
		if (!p.name.isEmpty())
			out.push_back(p);
	}
	return out;
}

void load()
{
	if (g_loaded)
		return;
	g_loaded = true;
	const QByteArray raw = QSettings().value("comboPresets").toByteArray();
	g_presets = fromJson(QJsonDocument::fromJson(raw).array());
	if (g_presets.isEmpty())
		g_presets.push_back(ComboColors::builtinDefault());
}

// 키 문자열들을 정규식으로 (긴 키부터: "AA" 가 "A" 보다 먼저)
QString keyAlternation(const ComboPreset &p)
{
	QStringList keys;
	for (const ComboKey &k : p.keys)
		if (!k.key.isEmpty() && !k.key.contains('-'))
			keys << QRegularExpression::escape(k.key);
	std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) { return a.size() > b.size(); });
	keys.removeDuplicates();
	return keys.join('|');
}
} // namespace

namespace ComboColors {

QString defaultPresetName()
{
	return QStringLiteral("기본");
}

ComboPreset builtinDefault()
{
	ComboPreset p;
	p.name = defaultPresetName();
	p.dash = Qt::white;
	const QColor orange("#ffb35c"); // 연한 주황
	const QColor sky("#6fd0ff");    // 하늘색
	for (const QString k : {"Q", "W", "E", "R", "D"})
		p.keys.push_back({k, orange});
	p.keys.push_back({"F", sky});
	return p;
}

QVector<ComboPreset> presets()
{
	load();
	return g_presets;
}

void setPresets(const QVector<ComboPreset> &list)
{
	load();
	g_presets = list;
	QSettings().setValue("comboPresets", QJsonDocument(toJson(list)).toJson(QJsonDocument::Compact));
	emit notifier()->changed();
}

bool find(const QString &name, ComboPreset *out)
{
	if (name.isEmpty())
		return false;
	if (g_hasOverride && g_override.name == name) {
		*out = g_override;
		return true;
	}
	load();
	for (const ComboPreset &p : g_presets)
		if (p.name == name) {
			*out = p;
			return true;
		}
	return false;
}

void setOverride(const ComboPreset &p)
{
	g_override = p;
	g_hasOverride = true;
}

void clearOverride()
{
	g_hasOverride = false;
}

QVector<QColor> colorize(const QString &line, const ComboPreset &p, const QColor &base)
{
	QVector<QColor> colors(line.size(), base);
	const QString alt = keyAlternation(p);
	if (alt.isEmpty())
		return colors;
	// 키 - 키 (- 키 …) 가 이어진 부분. 앞뒤가 글자·숫자면 단어의 일부이므로 제외
	const QRegularExpression run(QString(R"((?<![\p{L}\p{N}])(?:%1)(?:\s*-\s*(?:%1))+(?![\p{L}\p{N}]))").arg(alt),
				     QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpression key(QString("(?:%1)").arg(alt), QRegularExpression::CaseInsensitiveOption);
	auto it = run.globalMatch(line);
	while (it.hasNext()) {
		const auto m = it.next();
		const int end = int(m.capturedEnd());
		int pos = int(m.capturedStart());
		while (pos < end) {
			const QChar c = line[pos];
			if (c == '-') {
				colors[pos++] = p.dash;
				continue;
			}
			if (c.isSpace()) {
				++pos;
				continue;
			}
			const auto km = key.match(line, pos, QRegularExpression::NormalMatch,
						  QRegularExpression::AnchorAtOffsetMatchOption);
			if (!km.hasMatch() || km.capturedLength() == 0) {
				++pos;
				continue;
			}
			const QString found = km.captured(0);
			QColor col = base;
			// 같은 길이 중 대소문자 무시로 같은 키의 색 (먼저 적힌 것)
			for (const ComboKey &k : p.keys)
				if (k.key.compare(found, Qt::CaseInsensitive) == 0) {
					col = k.color;
					break;
				}
			for (int i = 0; i < km.capturedLength(); ++i)
				colors[pos + i] = col;
			pos += int(km.capturedLength());
		}
	}
	return colors;
}

bool hasCombo(const QString &line, const ComboPreset &p)
{
	const QString alt = keyAlternation(p);
	if (alt.isEmpty())
		return false;
	const QRegularExpression run(QString(R"((?<![\p{L}\p{N}])(?:%1)(?:\s*-\s*(?:%1))+(?![\p{L}\p{N}]))").arg(alt),
				     QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
	return run.match(line).hasMatch();
}

Notifier *notifier()
{
	static Notifier n;
	return &n;
}

} // namespace ComboColors
