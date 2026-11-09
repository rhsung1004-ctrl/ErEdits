#pragma once

#include "Segments.h"

#include <QObject>
#include <QRectF>

#include <array>

class FrameGrabber;

// 킬 확인: 킬 숫자 모양이 바뀐 시점마다 그 전·후 장면의 TK·K·A 숫자를 실제로 읽어 봄
//  - K 가 올라감 → 킬 (올라간 만큼 킬 수), A 만 올라감 → 어시스트
//  - K·A 둘 다 그대로 → 잘못 잡힌 킬 (이펙트·커서 등으로 모양만 바뀜). 다른 장면으로 한 번 더 읽어 같을 때만 뺌
//  - 숫자를 못 읽으면 원래 판단(모양 변화)을 그대로 씀
// 장면은 FrameGrabber 로 10장씩 묶어 꺼냄: 1차(전·후) → 필요한 킬만 2차(조금 더 앞·뒤)
class KillVerifier : public QObject {
	Q_OBJECT
public:
	explicit KillVerifier(QObject *parent = nullptr);
	~KillVerifier() override;

	// roi: 킬 숫자 영역 (영상 크기 대비 0~1 비율)
	void start(const QString &path, const QRectF &roi, const QVector<KillEvent> &events);
	void cancel();
	bool isRunning() const { return m_running; }

signals:
	void progress(int done, int total);
	void finished(const QVector<KillEvent> &events);

private:
	struct Read {
		bool ok = false;
		bool weak = true;
		int v[3] = {0, 0, 0};
		bool same(const Read &o) const { return ok && o.ok && v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2]; }
	};
	void runRound(int round);
	double slotTime(int index, int slot) const; // slot: 0 앞, 1 뒤, 2 앞(다시), 3 뒤(다시)
	void decide(int index);

	FrameGrabber *m_grab;
	QString m_path;
	QRectF m_roi;
	QVector<KillEvent> m_events; // 시간순
	QVector<std::array<Read, 4>> m_reads;
	QVector<QPair<int, int>> m_asked; // 이번 차례 장면 번호 → (킬 번호, slot)
	int m_round = 0;
	int m_done = 0, m_total = 0;
	bool m_running = false;
};
