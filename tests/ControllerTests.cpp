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
static bool hasOutline(const QImage& image, const QRect& area) {
	const auto region = area.intersected(image.rect());
	for (int y = region.top(); y <= region.bottom(); ++y)
		for (int x = region.left(); x <= region.right(); ++x) {
			const auto pixel = image.pixel(x, y);
			if (qAlpha(pixel) > 0 && qGray(pixel) < 128) return true;
		}
	return false;
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
		controller.preview(url, QVariantMap{{"threshold", 0}, {"extractEdges", true},
			{"edgeLowThreshold", 50}, {"edgeHighThreshold", 150}, {"edgeWidth", 1}}, 0);
		wait(controller);
		const auto output = directory.filePath(QStringLiteral("output.png"));
		controller.save(QUrl::fromLocalFile(output));
		const QImage processed(output);
		check(!processed.isNull(), "Image processing did not produce a readable image");
		// Previously, exterior-only extraction filled the artwork and lost internal lines.
		check(hasOutline(processed, QRect(12, 22, 8, 6))
			&& hasOutline(processed, QRect(24, 29, 7, 4))
			&& processed.pixelColor(20, 20) == QColor(Qt::white),
			"Extracted edges lost the transparent boundary, internal line or unfilled interior");
		controller.preview(url, QVariantMap{{"threshold", 0}, {"extractEdges", true},
			{"edgeWidth", 1}, {"simplifyEdges", true}, {"edgeTolerance", 1.0}}, 0);
		wait(controller);
		controller.save(QUrl::fromLocalFile(output));
		const QImage simplified(output);
		check(!simplified.isNull() && simplified != processed
			&& hasOutline(simplified, QRect(12, 22, 8, 6))
			&& hasOutline(simplified, QRect(24, 29, 7, 4))
			&& simplified.pixelColor(20, 20) == QColor(Qt::white),
			"Contour simplification did not change the edges or lost internal, unfilled contours");
		controller.setLayerType(controller.selectedLayerIndex(), QStringLiteral("silk"));
		// The silkscreen preview previously bypassed the processed artwork.
		const auto thumbnail = controller.layerRows().front().toMap().value("thumbnail").toString();
		const QImage silk = images->requestImage(thumbnail.mid(QStringLiteral("image://results/").size()), nullptr, {});
		check(hasOutline(silk, QRect(12, 22, 8, 6)), "Silkscreen preview omitted the extracted outline");
		// A dedicated outline must stay unfilled even when thresholding fills the entire image.
		controller.preview(url, QVariantMap{{"threshold", 255}, {"extractEdges", false},
			{"edgeLowThreshold", 50}, {"edgeHighThreshold", 150}, {"edgeWidth", 1},
			{"simplifyEdges", true}, {"edgeTolerance", 1.0}}, 0);
		wait(controller);
		check(QMetaObject::invokeMethod(engine.rootObjects().front(), "setLayerMaterial",
			Q_ARG(QVariant, QVariant(controller.selectedLayerIndex())), Q_ARG(QVariant, QVariant(2))),
			"Cannot select the outline layer type from the UI");
		wait(controller);
		const QImage outline = images->requestImage("selected", nullptr, {});
		check(hasOutline(outline, QRect(12, 22, 8, 6)) && hasOutline(outline, QRect(24, 29, 7, 4))
			&& outline.pixelColor(20, 20) == QColor(Qt::white), "Outline layer retained the filled artwork");
		controller.setLayerType(controller.selectedLayerIndex(), QStringLiteral("enig"));
		check(images->requestImage("selected", nullptr, {}).pixelColor(20, 20) == QColor(Qt::black),
			"Switching back to ENIG lost the original threshold result");
		controller.setLayerType(controller.selectedLayerIndex(), QStringLiteral("outline"));
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
