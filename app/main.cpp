#include "Controller.hpp"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QThreadPool>
#include <QTimer>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QTemporaryDir>

int main(int argc, char* argv[]) {
	QGuiApplication app(argc, argv);
	QGuiApplication::setApplicationName("Nasti PCB Designer");
	QGuiApplication::setApplicationDisplayName("Nasti PCB Designer");
	QQuickStyle::setStyle("Fusion");
	auto* images = new ImageStore;
	Controller controller(images);
	QQmlApplicationEngine engine;
	engine.addImageProvider("results", images);
	engine.rootContext()->setContextProperty("controller", &controller);
	engine.loadFromModule("Binarizer", "Main");
	if (engine.rootObjects().isEmpty()) return 1;
	const auto arguments = app.arguments();
	// Test the actual packaged QML scene as well as the C++ controller.
	QTemporaryDir smokeDirectory;
	const int smoke = arguments.indexOf("--smoke-screenshot");
	if (smoke >= 0 && smoke + 1 < arguments.size()) {
		QImage fixture(64, 64, QImage::Format_Grayscale8);
		fixture.fill(Qt::white);
		for (int y = 16; y < 48; ++y)
			for (int x = 16; x < 48; ++x) fixture.setPixelColor(x, y, Qt::black);
		const auto input = smokeDirectory.filePath("fixture.png");
		if (!fixture.save(input)) return 1;
		const auto screenshot = arguments[smoke + 1];
		QTimer::singleShot(15000, &app, [&app] { app.exit(2); });
		QObject::connect(&controller, &Controller::changed, &app, [&, screenshot, capturing = false]() mutable {
			if (controller.busy() || controller.resultUrl().isEmpty() || capturing) return;
			capturing = true;
			QTimer::singleShot(250, &app, [&, screenshot] {
				auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
				const bool saved = window && window->grabWindow().save(screenshot);
				app.exit(saved ? 0 : 3);
			});
		});
		QTimer::singleShot(0, &engine, [&, input] {
			engine.rootObjects().first()->setProperty("sourceFile", QUrl::fromLocalFile(input));
		});
	}
	const int code = app.exec();
	controller.cancel();
	QThreadPool::globalInstance()->waitForDone();
	return code;
}
