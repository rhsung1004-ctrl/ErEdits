#pragma once

#include "Segments.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QRectF>

// 킬 확인: 킬 숫자 모양이 바뀐 시점마다 그 전·후 장면의 TK·K·A 숫자를 실제로 읽어 봄
//  - K 가 올라감 → 킬 (올라간 만큼 킬 수), A 만 올라감 → 어시스트
//  - K·A 둘 다 그대로 → 잘못 잡힌 킬 (이펙트·커서 등으로 모양만 바뀜). 한 번 더 다른 장면으로 확인한 뒤에만 뺌
//  - 숫자를 못 읽으면 원래 판단(모양 변화)을 그대로 씀
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
	void next();
	void grab(double t);
	void onFinished(int code, QProcess::ExitStatus st);
	double slotTime(int index, int slot) const; // slot: 0 앞, 1 뒤, 2 앞(다시), 3 뒤(다시)
	void decide();

	QProcess m_proc;
	QString m_path;
	QRectF m_roi;
	QVector<KillEvent> m_events; // 시간순
	QByteArray m_buf;
	int m_index = 0;
	int m_slot = 0;
	Read m_reads[4];
	bool m_running = false;
	bool m_cancelled = false;
};
