#include "FontManager.h"
#include "MainWindow.h"
#include "SubtitlePanel.h"

#include <QApplication>
#include <QTabWidget>
#include <QPushButton>
#include <QTimer>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	QApplication::setOrganizationName("ERKillCut");
	QApplication::setApplicationName("ERKillCut");
	QApplication::setApplicationVersion("0.1.0");
	if (QStyleFactory::keys().contains("Fusion"))
		app.setStyle(QStyleFactory::create("Fusion"));

	FontManager::init(); // 추가해 둔 자막 글꼴 등록

	MainWindow w;
	w.show();
	const QStringList args = app.arguments();
	if (args.size() > 1)
		w.openFiles(args.mid(1)); // 영상·프로젝트 파일을 exe 위에 끌어 놓아도 열림 (여러 개면 묶기)
	else if (!qEnvironmentVariableIsSet("ERKILLCUT_NO_RESTORE"))
		QTimer::singleShot(0, &w, [&w] { w.restoreLastSession(); }); // 지난 작업 이어서
	// 개발용: 화면 모양 확인 (ERKILLCUT_SNAPSHOT=파일.png 이면 3초 뒤 창을 저장하고 종료)
	const QString snap = qEnvironmentVariable("ERKILLCUT_SNAPSHOT");
	// 개발용: ERKILLCUT_TEST_BATCH 이면 시작하자마자 [모두 킬 찾기]
	if (qEnvironmentVariableIsSet("ERKILLCUT_TEST_BATCH"))
		QTimer::singleShot(500, &w, [&w] {
			for (auto *b : w.findChildren<QPushButton *>())
				if (b->text().contains("모두 킬 찾기")) {
					b->click();
					break;
				}
		});
	if (!snap.isEmpty())
		QTimer::singleShot(qEnvironmentVariableIsSet("ERKILLCUT_SNAPSHOT_DELAY") ? qEnvironmentVariableIntValue("ERKILLCUT_SNAPSHOT_DELAY") : 3000, &w, [&w, snap] {
			if (qEnvironmentVariableIsSet("ERKILLCUT_SNAPSHOT_TAB"))
				for (auto *t : w.findChildren<QTabWidget *>())
					t->setCurrentIndex(qEnvironmentVariableIntValue("ERKILLCUT_SNAPSHOT_TAB"));
			if (qEnvironmentVariableIsSet("ERKILLCUT_SNAPSHOT_SUB"))
				if (auto *sp = w.findChild<SubtitlePanel *>())
					sp->select(qEnvironmentVariableIntValue("ERKILLCUT_SNAPSHOT_SUB"));
			QApplication::processEvents();
			w.grab().save(snap);
			QApplication::quit();
		});
	return app.exec();
}
