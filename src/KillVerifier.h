#pragma once

#include "Segments.h"

#include <QObject>
#include <QRectF>

class FrameGrabber;

// 킬 확인: 킬 숫자 모양이 바뀐 시점마다 그 전·후 장면의 TK·K·A 숫자를 실제로 읽어 봄
//  - K 가 올라감 → 킬 (올라간 만큼 킬 수), A 만 올라감 → 어시스트
//  - 킬 시각이 몇 초 늦게 잡혔을 수 있어서 (빠르게 방식·키프레임 간격) 앞은 0.6초 → 2.5 → 5 → 9초 전까지,
//    단 앞 킬 바로 뒤보다 앞으로는 가지 않음. 바뀐 곳을 찾으면 킬 시각도 그 사이로 바로잡음
//  - K·A 가 여러 장면에서 모두 그대로 → 잘못 잡힌 킬 (이펙트·커서 등으로 모양만 바뀜)
//    "빼기"를 끄면 빼지 않고 "확인 필요"로만 표시
//  - 숫자를 못 읽으면 원래 판단(모양 변화)을 그대로 씀
// 장면은 FrameGrabber 로 10장씩 묶어 꺼냄: 1차(바로 앞·뒤) → 그대로였던 킬만 2차(더 앞·더 뒤)
// 읽은 장면은 확인 기록(그림 + 읽은 값)으로 저장할 수 있음
class KillVerifier : public QObject {
	Q_OBJECT
public:
	explicit KillVerifier(QObject *parent = nullptr);
	~KillVerifier() override;

	// roi: 킬 숫자 영역 (영상 크기 대비 0~1 비율)
	void start(const QString &path, const QRectF &roi, const QVector<KillEvent> &events);
	void cancel();
	bool isRunning() const { return m_running; }
	// 숫자가 그대로인 킬을 뺄지 (끄면 Doubtful 로 남김)
	void setDropUnchanged(bool on) { m_drop = on; }

	// 마지막 확인 기록을 dir 에 저장 (index.html + 장면 그림). 저장한 html 경로
	QString saveRecord(const QString &dir) const;
	bool hasRecord() const { return !m_record.isEmpty(); }

signals:
	void progress(int done, int total);
	void finished(const QVector<KillEvent> &events);

private:
	struct Read {
		bool ok = false;
		bool weak = true;
		int v[3] = {0, 0, 0};
	};
	struct Shot {
		double time = 0;
		bool before = true; // 킬 전 장면인지
		Read read;
		QByteArray jpg; // 기록용 그림
	};
	struct Record {
		KillEvent original;
		KillEvent result;
		QString reason;
		QVector<Shot> shots;
	};
	void runRound(int round);
	QVector<double> beforeTimes(int i, int round) const;
	QVector<double> afterTimes(int i, int round) const;
	void decide(int index);

	FrameGrabber *m_grab;
	QString m_path;
	QRectF m_roi;
	bool m_drop = true;
	QVector<KillEvent> m_events; // 시간순
	QVector<KillEvent> m_original;
	QVector<QVector<Shot>> m_shots;
	QVector<QPair<int, int>> m_asked; // 이번 차례 장면 번호 → (킬 번호, m_shots 안 번호)
	QVector<Record> m_record;
	QVector<QString> m_reason;
	int m_round = 0;
	int m_done = 0, m_total = 0;
	bool m_running = false;
};
