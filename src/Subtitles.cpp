#include "Subtitles.h"

#include "FontManager.h"

#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>

void Subtitle::copyStyleFrom(const Subtitle &o)
{
	font = o.font;
	size = o.size;
	color = o.color;
	outline = o.outline;
	outlineWidth = o.outlineWidth;
	shadow = o.shadow;
	box = o.box;
	fade = o.fade;
	posY = o.posY;
}

QVector<SubtitlePiece> mapSubtitle(const Subtitle &s, const QVector<Segment> &segs, double d)
{
	QVector<SubtitlePiece> out;
	double cum = 0;
	for (int i = 0; i < segs.size(); ++i) {
		const Segment &g = segs[i];
		const double outStart = cum - i * d;
		cum += g.length();
		const double a = std::max(s.start, g.start), b = std::min(s.end, g.end);
		if (b - a < 0.05)
			continue;
		SubtitlePiece p;
		p.outStart = outStart + (a - g.start);
		p.outEnd = outStart + (b - g.start);
		p.fadeIn = s.start >= g.start - 1e-6;
		p.fadeOut = s.end <= g.end + 1e-6;
		// 크로스페이드로 겹치는 앞 조각과는 하나로 (같은 자막이 두 번 겹쳐 그려지지 않게)
		if (!out.isEmpty() && p.outStart <= out.back().outEnd + 0.01) {
			out.back().outEnd = std::max(out.back().outEnd, p.outEnd);
			out.back().fadeOut = p.fadeOut;
			continue;
		}
		out.push_back(p);
	}
	return out;
}

double sourceToOutput(double t, const QVector<Segment> &segs, double d)
{
	double cum = 0;
	for (int i = 0; i < segs.size(); ++i) {
		const Segment &g = segs[i];
		const double outStart = cum - i * d;
		cum += g.length();
		if (t >= g.start && t <= g.end)
			return outStart + (t - g.start);
	}
	return -1;
}

double subtitleAlphaAt(const Subtitle &s, double t)
{
	if (t < s.start || t > s.end || s.text.trimmed().isEmpty())
		return 0;
	if (!s.fade)
		return 1;
	const double f = std::min(kSubtitleFade, s.length() / 2);
	if (f <= 0)
		return 1;
	return std::clamp(std::min((t - s.start) / f, (s.end - t) / f), 0.0, 1.0);
}

QRectF paintSubtitle(QPainter &p, const QRectF &canvas, const Subtitle &s, double alpha)
{
	if (s.text.trimmed().isEmpty() || canvas.height() <= 0)
		return {};
	const double k = canvas.height() / 1080.0; // 1080p 기준 → 화면 크기
	const QFont font = FontManager::qfont(s.font, std::max(1, int(std::lround(s.size * k))));
	const QFontMetricsF fm(font);
	const QStringList lines = s.text.split('\n');
	const double lineH = fm.lineSpacing();
	const double blockH = lineH * lines.size() - fm.leading();
	const double bottom = canvas.top() + s.posY * canvas.height();
	const double top = bottom - blockH;

	QPainterPath path;
	double maxW = 0;
	for (int i = 0; i < lines.size(); ++i) {
		const double w = fm.horizontalAdvance(lines[i]);
		maxW = std::max(maxW, w);
		path.addText(QPointF(canvas.center().x() - w / 2, top + i * lineH + fm.ascent()), font, lines[i]);
	}
	const QRectF textRect(canvas.center().x() - maxW / 2, top, maxW, blockH);

	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setOpacity(std::clamp(alpha, 0.0, 1.0));
	if (s.box) {
		const double pad = s.size * k * 0.3;
		p.fillRect(textRect.adjusted(-pad, -pad, pad, pad), QColor(0, 0, 0, 115));
	}
	if (s.shadow) {
		const double off = 3 * k;
		QPainterPath sh = path.translated(off, off);
		if (s.outlineWidth > 0) {
			QPainterPathStroker st;
			st.setWidth(2.0 * s.outlineWidth * k);
			st.setJoinStyle(Qt::RoundJoin);
			sh = sh.united(st.createStroke(sh));
		}
		p.fillPath(sh, QColor(0, 0, 0, 150));
	}
	if (s.outlineWidth > 0) {
		QPen pen(s.outline, 2.0 * s.outlineWidth * k);
		pen.setJoinStyle(Qt::RoundJoin);
		p.strokePath(path, pen);
	}
	p.fillPath(path, s.color);
	p.restore();
	return textRect;
}

QJsonObject subtitleStyleToJson(const Subtitle &s)
{
	return QJsonObject{{"font", s.font},       {"size", s.size},         {"color", s.color.name()},
			   {"outline", s.outline.name()}, {"ow", s.outlineWidth}, {"shadow", s.shadow},
			   {"box", s.box},          {"fade", s.fade},         {"y", s.posY}};
}

void subtitleStyleFromJson(const QJsonObject &o, Subtitle *s)
{
	if (o.isEmpty())
		return;
	s->font = o["font"].toString();
	s->size = o["size"].toInt(64);
	s->color = QColor(o["color"].toString("#ffffff"));
	s->outline = QColor(o["outline"].toString("#000000"));
	s->outlineWidth = o["ow"].toInt(4);
	s->shadow = o["shadow"].toBool(true);
	s->box = o["box"].toBool(false);
	s->fade = o["fade"].toBool(true);
	s->posY = o["y"].toDouble(0.9);
}

QJsonArray subtitlesToJson(const QVector<Subtitle> &subs)
{
	QJsonArray a;
	for (const Subtitle &s : subs) {
		QJsonObject o = subtitleStyleToJson(s);
		o["s"] = s.start;
		o["e"] = s.end;
		o["text"] = s.text;
		a.append(o);
	}
	return a;
}

QVector<Subtitle> subtitlesFromJson(const QJsonArray &a)
{
	QVector<Subtitle> out;
	for (const auto &v : a) {
		const QJsonObject o = v.toObject();
		Subtitle s;
		subtitleStyleFromJson(o, &s);
		s.start = o["s"].toDouble();
		s.end = o["e"].toDouble();
		s.text = o["text"].toString();
		if (s.end > s.start)
			out.push_back(s);
	}
	return out;
}
