#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVector>

// 스킬 콤보 자막 색 (캐릭터별 프리셋)
//  자막 안에서 "Q-F-W-E-R-E-D" 처럼 프리셋의 키가 '-' 로 2개 이상 이어진 부분을 찾아
//  키마다 정한 색, '-' 는 구분자 색으로 칠함. 나머지 글자는 자막의 글자색 그대로.
//  - 영문 키는 대소문자 구분 없음, '-' 앞뒤 띄어쓰기 허용 ("Q - W - E")
//  - 키 하나만 있는 글자("E" 등)는 칠하지 않음 (일반 문장과 헷갈리지 않게)
//  - 키는 여러 글자도 가능 (예: "평", "AA", "Space")
struct ComboKey {
	QString key;
	QColor color;
};

struct ComboPreset {
	QString name;
	QColor dash = Qt::white; // '-' 색
	QVector<ComboKey> keys;
};

namespace ComboColors {

QString defaultPresetName(); // "기본"
ComboPreset builtinDefault(); // QWERD 연한 주황, F 하늘색, '-' 흰색

QVector<ComboPreset> presets();               // 저장된 프리셋 (없으면 기본 하나)
void setPresets(const QVector<ComboPreset> &); // 저장 + changed 알림
bool find(const QString &name, ComboPreset *out);
// 편집 중 미리보기용: 저장하지 않고 이 프리셋을 잠깐 우선 사용
void setOverride(const ComboPreset &p);
void clearOverride();

// line 의 글자마다 색 (콤보가 아닌 글자는 base)
QVector<QColor> colorize(const QString &line, const ComboPreset &p, const QColor &base);
// line 안에 칠할 콤보가 있는지
bool hasCombo(const QString &line, const ComboPreset &p);

// 프리셋이 바뀌면 알림 (미리보기 다시 그리기용)
class Notifier : public QObject {
	Q_OBJECT
signals:
	void changed();
};
Notifier *notifier();

} // namespace ComboColors
