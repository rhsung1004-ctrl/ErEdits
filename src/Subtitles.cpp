#include "Subtitles.h"

#include "ComboColors.h"
#include "FontManager.h"

#include <QHash>
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
	combo = o.combo;
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

namespace {
// 자막을 그릴 모양: 글자 전체 윤곽(테두리·그림자용) + 색마다 채울 윤곽
struct SubtitleShape {
	QPainterPath all;
	QVector<QPair<QColor, QPainterPath>> fills;
	QRectF textRect;
	double k = 1;
};

SubtitleShape buildShape(const QRectF &canvas, const Subtitle &s)
{
	SubtitleShape sh;
	sh.k = canvas.height() / 1080.0; // 1080p 기준 → 화면 크기
	const QFont font = FontManager::qfont(s.font, std::max(1, int(std::lround(s.size * sh.k))));
	const QFontMetricsF fm(font);
	const QStringList lines = s.text.split('\n');
	const double lineH = fm.lineSpacing();
	const double blockH = lineH * lines.size() - fm.leading();
	const double bottom = canvas.top() + s.posY * canvas.height();
	const double top = bottom - blockH;

	ComboPreset preset;
	const bool combo = ComboColors::find(s.combo, &preset);
	QHash<QRgb, int> fillIndex; // 색 → fills 위치
	auto fillFor = [&](const QColor &c) -> QPainterPath & {
		const auto it = fillIndex.constFind(c.rgba());
		if (it != fillIndex.constEnd())
			return sh.fills[*it].second;
		fillIndex.insert(c.rgba(), int(sh.fills.size()));
		sh.fills.push_back({c, QPainterPath()});
		return sh.fills.back().second;
	};

	double maxW = 0;
	for (int i = 0; i < lines.size(); ++i) {
		const QString &line = lines[i];
		const double w = fm.horizontalAdvance(line);
		maxW = std::max(maxW, w);
		const double x0 = canvas.center().x() - w / 2, y = top + i * lineH + fm.ascent();
		if (!combo) {
			QPainterPath lp;
			lp.addText(QPointF(x0, y), font, line);
			sh.all.addPath(lp);
			fillFor(s.color).addPath(lp);
			continue;
		}
		// 같은 색끼리 묶어서 그 자리부터 그림 (앞부분 폭만큼 오른쪽으로)
		const QVector<QColor> colors = ComboColors::colorize(line, preset, s.color);
		int a = 0;
		while (a < line.size()) {
			int b = a + 1;
			while (b < line.size() && colors[b] == colors[a])
				++b;
			QPainterPath rp;
			rp.addText(QPointF(x0 + fm.horizontalAdvance(line.left(a)), y), font, line.mid(a, b - a));
			sh.all.addPath(rp);
			fillFor(colors[a]).addPath(rp);
			a = b;
		}
	}
	sh.textRect = QRectF(canvas.center().x() - maxW / 2, top, maxW, blockH);
	return sh;
}

QRectF drawShape(QPainter &p, const SubtitleShape &sh, const Subtitle &s, double alpha)
{
	const double k = sh.k;
	p.save();
	p.setRenderHint(QPainter::Antialiasing);
	p.setOpacity(std::clamp(alpha, 0.0, 1.0));
	if (s.box) {
		const double pad = s.size * k * 0.3;
		p.fillRect(sh.textRect.adjusted(-pad, -pad, pad, pad), QColor(0, 0, 0, 115));
	}
	if (s.shadow) {
		const double off = 3 * k;
		QPainterPath shadow = sh.all.translated(off, off);
		if (s.outlineWidth > 0) {
			QPainterPathStroker st;
			st.setWidth(2.0 * s.outlineWidth * k);
			st.setJoinStyle(Qt::RoundJoin);
			shadow = shadow.united(st.createStroke(shadow));
		}
		p.fillPath(shadow, QColor(0, 0, 0, 150));
	}
	if (s.outlineWidth > 0) {
		QPen pen(s.outline, 2.0 * s.outlineWidth * k);
		pen.setJoinStyle(Qt::RoundJoin);
		p.strokePath(sh.all, pen);
	}
	for (const auto &f : sh.fills)
		p.fillPath(f.second, f.first);
	p.restore();
	return sh.textRect;
}
} // namespace

QRectF paintSubtitle(QPainter &p, const QRectF &canvas, const Subtitle &s, double alpha)
{
	if (s.text.trimmed().isEmpty() || canvas.height() <= 0)
		return {};
	return drawShape(p, buildShape(canvas, s), s, alpha);
}

QImage renderSubtitleImage(const Subtitle &s, int W, int H, QPoint *topLeft)
{
	if (s.text.trimmed().isEmpty() || W <= 0 || H <= 0)
		return {};
	const QRectF canvas(0, 0, W, H);
	const SubtitleShape sh = buildShape(canvas, s);
	// 테두리·그림자·박스까지 들어가는 영역
	const double k = sh.k;
	double margin = 4 + s.outlineWidth * k + (s.shadow ? 3 * k : 0);
	if (s.box)
		margin = std::max(margin, s.size * k * 0.3 + 2);
	QRectF area = sh.all.boundingRect().united(sh.textRect).adjusted(-margin, -margin, margin, margin);
	const QRect r = area.toAlignedRect().intersected(QRect(0, 0, W, H));
	if (r.isEmpty())
		return {};
	QImage img(r.size(), QImage::Format_ARGB32_Premultiplied);
	img.fill(Qt::transparent);
	QPainter p(&img);
	p.translate(-r.topLeft());
	drawShape(p, sh, s, 1.0);
	p.end();
	if (topLeft)
		*topLeft = r.topLeft();
	return img;
}

QJsonObject subtitleStyleToJson(const Subtitle &s)
{
	return QJsonObject{{"font", s.font},       {"size", s.size},         {"color", s.color.name()},
			   {"outline", s.outline.name()}, {"ow", s.outlineWidth}, {"shadow", s.shadow},
			   {"box", s.box},          {"fade", s.fade},         {"y", s.posY},
			   {"combo", s.combo}};
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
	s->combo = o.contains("combo") ? o["combo"].toString() : ComboColors::defaultPresetName();
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
