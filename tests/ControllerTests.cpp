#include "Controller.hpp"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}
static void wait(Controller& controller) {
	QElapsedTimer timer;
	timer.start();
	while (controller.busy() && timer.elapsed() < 10000) {
		QGuiApplication::processEvents();
		QThread::msleep(2);
	}
	check(!controller.busy(), "Operation timed out");
}
int main(int argc, char** argv) {
	QGuiApplication app(argc, argv);
	QQuickStyle::setStyle(QStringLiteral("Fusion"));
	QQmlApplicationEngine engine;
	auto* images = new ImageStore;
	engine.addImageProvider("results", images);
	Controller controller(images);
	engine.rootContext()->setContextProperty("controller", &controller);
	QTemporaryDir directory;
	try {
		engine.load(QUrl::fromLocalFile(QStringLiteral(BINARIZER_SOURCE_DIR "/qml/Main.qml")));
		check(!engine.rootObjects().isEmpty(), "Application UI failed to load");
		QImage source(64, 64, QImage::Format_ARGB32);
		source.fill(Qt::transparent);
		for (int y = 16; y < 48; ++y)
			for (int x = 16; x < 48; ++x) source.setPixelColor(x, y, Qt::white);
		for (int y = 28; y < 36; ++y)
			for (int x = 28; x < 36; ++x) source.setPixelColor(x, y, QColor(180, 180, 180));
		const auto input = directory.filePath(QStringLiteral("input.png"));
		check(source.save(input), "Cannot create input image");
		const auto url = QUrl::fromLocalFile(input);
		controller.preview(url, {}, 0);
		wait(controller);
		const auto output = directory.filePath(QStringLiteral("output.png"));
		controller.save(QUrl::fromLocalFile(output));
		const QImage processed(output);
		check(!processed.isNull(), "Image processing did not produce a readable image");
		controller.setLayerType(controller.selectedLayerIndex(), QStringLiteral("silk"));
		controller.generatePcb(url, directory.path());
		wait(controller);
		QFile pcb(directory.filePath(QStringLiteral("input.epro2")));
		check(pcb.open(QIODevice::ReadOnly) && pcb.read(4) == QByteArray("PK\x03\x04", 4),
			"PCB export did not produce a project archive");
		std::cout << "PASS: application UI, image processing and PCB export\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		controller.cancel();
		wait(controller);
		return 1;
	}
}
