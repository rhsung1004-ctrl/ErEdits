#pragma once

#include "Highlights.h"

#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

// 로컬 AI 설정 (QSettings 에 저장)
struct AiConfig {
	QString whisperExe;   // whisper.cpp 의 whisper-cli.exe
	QString whisperModel; // ggml-large-v3-turbo-q5_0.bin 등
	QString ollamaUrl = "http://localhost:11434";
	QString ollamaModel = "qwen3:8b";
	static AiConfig load();
	void save() const;
	bool whisperReady() const;
};

// 재밌는 장면 후보를 로컬 AI 로 확인
//  1단계: 후보마다 앞뒤를 넉넉히 잘라 whisper.cpp 로 방송인 말을 받아 적음 (GPU 를 번갈아 쓰지 않게 먼저 전부)
//  2단계: Ollama 에 받아 적은 말 + 그 순간 채팅을 주고 "재밌는 장면인지, 어디서 시작·끝나는지, 한 줄 설명"을 받음
class LocalAi : public QObject {
	Q_OBJECT
public:
	explicit LocalAi(QObject *parent = nullptr);
	~LocalAi() override;

	// indices: 확인할 후보 번호
	void start(const QString &videoPath, const AiConfig &cfg, const QVector<FunCandidate> &cands,
		   const QVector<int> &indices);
	void cancel();
	bool isRunning() const { return m_running; }

	// 연결 확인: Ollama 가 켜져 있고 모델이 있는지, whisper 파일이 있는지 → 결과 문장
	void check(const AiConfig &cfg);

	// 테스트용
	struct Line {
		double t0, t1; // 영상 기준 초
		QString text;
	};
	static QVector<Line> parseWhisperJson(const QByteArray &json, double offset);
	static QString buildPrompt(const FunCandidate &c, const QVector<Line> &lines);
	// AI 답(JSON) → 후보에 반영. 실패하면 false
	static bool applyAnswer(const QByteArray &answer, const QVector<Line> &lines, FunCandidate *c);

	static constexpr double kBefore = 45; // 반응 몇 초 전부터 받아 적을지
	static constexpr double kAfter = 12;

signals:
	void status(const QString &text);
	void candidateDone(int index, const FunCandidate &c);
	void finished(bool ok, const QString &message);
	void checked(bool ok, const QString &message);

private:
	void nextTranscribe();
	void onExtracted(int code, QProcess::ExitStatus st);
	void onWhisper(int code, QProcess::ExitStatus st);
	void nextJudge();
	void postOllama(const QByteArray &body, bool withThink);
	void finish(bool ok, const QString &msg);

	QNetworkAccessManager *m_net;
	QPointer<QNetworkReply> m_reply;
	QProcess m_proc;
	std::unique_ptr<QTemporaryDir> m_tmp;
	QString m_video;
	AiConfig m_cfg;
	QVector<FunCandidate> m_cands;
	QVector<int> m_todo;
	QVector<QVector<Line>> m_lines; // m_todo 순서
	int m_pos = 0;
	int m_judged = 0;
	bool m_running = false;
	bool m_cancelled = false;
	QString m_lastError;
};
