#include "Controller.hpp"
#include <QDataStream>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <opencv2/imgproc.hpp>

bool Controller::saveProject(const QUrl& destination) {
	if (active || !destination.isLocalFile()) return false;
	QSaveFile file(destination.toLocalFile());
	bool ok = file.open(QIODevice::WriteOnly);
	if (ok) {
		QDataStream out(&file);
		out.setVersion(QDataStream::Qt_6_0);
		out << quint32(0x4e504342) << quint32(1) << minimumCanvasWidth << minimumCanvasHeight
			<< maskColor << activeSideValue << frontSelectedLayerId << backSelectedLayerId
			<< quint32(artworkLayers.size());
		for (const auto& layer : artworkLayers) {
			out << layer.id << layer.name << layer.sourcePath << layer.type << layer.side << layer.visible
				<< layer.x << layer.y << parametersFor(layer.settings) << layer.dotsPerMeterX
				<< layer.dotsPerMeterY << layer.hasPhysicalResolution << layer.originalImage
				<< layer.outputImage << layer.silkImage << layer.outlineImage;
		}
		ok = out.status() == QDataStream::Ok && file.commit();
	}
	message = ok ? QStringLiteral("工程已保存：%1").arg(file.fileName()) : QStringLiteral("保存工程失败：%1").arg(file.errorString());
	emit changed();
	return ok;
}

bool Controller::openProject(const QUrl& source) {
	if (active || !source.isLocalFile()) return false;
	auto fail = [this](const QString& reason) {
		message = QStringLiteral("打开工程失败：%1").arg(reason);
		emit changed();
		return false;
	};
	QFile file(source.toLocalFile());
	if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
	QDataStream in(&file);
	in.setVersion(QDataStream::Qt_6_0);
	quint32 magic = 0, version = 0, count = 0;
	in >> magic >> version;
	if (magic != 0x4e504342 || version != 1) return fail(QStringLiteral("无法识别工程格式或版本"));
	Snapshot state{};
	in >> state.width >> state.height >> state.color >> state.side >> state.frontSelection >> state.backSelection >> count;
	const auto validSide = [](const QString& side) { return side == "front" || side == "back"; };
	if (state.width < 0 || state.height < 0 || !validSide(state.side)) return fail(QStringLiteral("画布数据无效"));
	int nextId = 1;
	QSet<int> ids;
	for (quint32 i = 0; i < count; ++i) {
		ArtworkLayer layer;
		QVariantMap params;
		in >> layer.id >> layer.name >> layer.sourcePath >> layer.type >> layer.side >> layer.visible
			>> layer.x >> layer.y >> params >> layer.dotsPerMeterX >> layer.dotsPerMeterY
			>> layer.hasPhysicalResolution >> layer.originalImage >> layer.outputImage >> layer.silkImage >> layer.outlineImage;
		const int method = params.value("method", -1).toInt();
		if (in.status() != QDataStream::Ok || layer.originalImage.isNull() || layer.sourcePath.isEmpty()
			|| method < 0 || method >= int(binarizer::methods.size()) || !validSide(layer.side)
			|| (layer.type != "enig" && layer.type != "silk" && layer.type != "outline")
			|| layer.id < 1 || layer.id == std::numeric_limits<int>::max() || ids.contains(layer.id)
			|| !std::isfinite(layer.x) || !std::isfinite(layer.y) || layer.x < 0 || layer.y < 0
			|| layer.x + layer.originalImage.width() > std::numeric_limits<int>::max()
			|| layer.y + layer.originalImage.height() > std::numeric_limits<int>::max())
			return fail(QStringLiteral("图层数据无效或不完整"));
		for (const QImage* image : {&layer.outputImage, &layer.silkImage, &layer.outlineImage})
			if (!image->isNull() && image->size() != layer.originalImage.size()) return fail(QStringLiteral("图层尺寸不一致"));
		layer.settings = optionsFrom(params, method);
		layer.embedded = true;
		layer.originalImage = layer.originalImage.convertToFormat(QImage::Format_RGBA8888);
		cv::Mat rgba(layer.originalImage.height(), layer.originalImage.width(), CV_8UC4,
			layer.originalImage.bits(), layer.originalImage.bytesPerLine());
		cv::cvtColor(rgba, layer.sourceImage, cv::COLOR_RGBA2BGRA);
		ids.insert(layer.id);
		nextId = std::max(nextId, layer.id + 1);
		state.layers.push_back(std::move(layer));
	}
	if (in.status() != QDataStream::Ok || !in.atEnd()) return fail(QStringLiteral("工程数据不完整或格式异常"));
	nextLayerId = nextId;
	past.clear();
	future.clear();
	images->clear();
	message = QStringLiteral("工程已打开：%1").arg(file.fileName());
	restore(std::move(state));
	return true;
}
