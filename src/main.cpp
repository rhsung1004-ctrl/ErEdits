#include "MainWindow.h"

#include <QApplication>
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

	MainWindow w;
	w.show();
	const QStringList args = app.arguments();
	if (args.size() > 1)
		w.openVideo(args.at(1)); // 영상 파일을 exe 위에 끌어 놓아도 열림
	// 개발용: 화면 모양 확인 (ERKILLCUT_SNAPSHOT=파일.png 이면 3초 뒤 창을 저장하고 종료)
	const QString snap = qEnvironmentVariable("ERKILLCUT_SNAPSHOT");
	if (!snap.isEmpty())
		QTimer::singleShot(3000, &w, [&w, snap] {
			w.grab().save(snap);
			QApplication::quit();
		});
	return app.exec();
}
