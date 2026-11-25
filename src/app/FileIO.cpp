#include "Controller.hpp"
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <opencv2/imgproc.hpp>

QString Controller::localPath(const QUrl& url) const { return url.toLocalFile(); }
QUrl Controller::fileUrl(const QString& path) const { return QUrl::fromLocalFile(path); }
QString Controller::sourceDirectory(const QUrl& source) const { return QFileInfo(source.toLocalFile()).absolutePath(); }

static cv::Mat sourcePixels(QImage& image) {
	image = image.convertToFormat(QImage::Format_RGBA8888);
	const cv::Mat rgba(image.height(), image.width(), CV_8UC4, image.bits(), image.bytesPerLine());
	cv::Mat source;
	cv::cvtColor(rgba, source, cv::COLOR_RGBA2BGRA);
	return source;
}

QImage Controller::readImage(const QString& path, const QImage& embedded, cv::Mat& source, bool& hasPhysicalResolution) {
	QImageReader reader(path);
	QImage image = embedded.isNull() ? reader.read() : embedded;
	if (image.isNull()) throw std::runtime_error(reader.errorString().toStdString());
	const int dotsPerMeterX = image.dotsPerMeterX();
	const int dotsPerMeterY = image.dotsPerMeterY();
	const QImage defaultResolution(1, 1, QImage::Format_Grayscale8);
	const double dpiX = dotsPerMeterX * 0.0254;
	const double dpiY = dotsPerMeterY * 0.0254;
	const bool isQtDefault = dotsPerMeterX == defaultResolution.dotsPerMeterX()
		&& dotsPerMeterY == defaultResolution.dotsPerMeterY();
	hasPhysicalResolution = !isQtDefault
		&& dpiX >= 10.0 && dpiX <= 2400.0
		&& dpiY >= 10.0 && dpiY <= 2400.0;
	source = sourcePixels(image);
	return image;
}

bool Controller::hasExportableLayers() const {
	return std::any_of(artworkLayers.cbegin(), artworkLayers.cend(), [this](const ArtworkLayer& layer) {
		return layer.visible && !artworkImage(layer).isNull();
	});
}

QUrl Controller::outputFile(const QUrl& source, const QString& directory, bool grayscale) const {
	const QFileInfo file(source.toLocalFile());
	return QUrl::fromLocalFile(QDir(directory.isEmpty() ? file.absolutePath() : directory)
		.filePath(file.completeBaseName() + (grayscale ? ".gray.png" : ".binary.png")));
}

static cv::Mat adjustedGray(const cv::Mat& source, const binarizer::Options& settings) {
	cv::Mat gray = binarizer::preprocess(source, settings);
	if (settings.invert) cv::bitwise_not(gray, gray);
	if (settings.flipHorizontal || settings.flipVertical) {
		const int axis = settings.flipHorizontal && settings.flipVertical ? -1 : int(settings.flipHorizontal);
		cv::flip(gray, gray, axis);
	}
	return gray;
}

void Controller::save(const QUrl& destination, bool grayscale) {
	if (active || output.isNull()) return;
	if (!destination.isLocalFile()) return;
	QString path = destination.toLocalFile();
	if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
	QImage image = output;
	if (grayscale) {
		try {
			image = grayscaleImage(adjustedGray(sourceImage, settings));
			const auto& layer = artworkLayers[selectedLayer];
			image.setDotsPerMeterX(layer.dotsPerMeterX);
			image.setDotsPerMeterY(layer.dotsPerMeterY);
		} catch (const std::exception& error) {
			message = QStringLiteral("灰度导出失败：%1").arg(QString::fromUtf8(error.what()));
			emit changed();
			return;
		}
	}
	message = image.save(path, "PNG") ? QStringLiteral("已保存：%1").arg(path) : QStringLiteral("保存失败，请检查路径和权限");
	emit changed();
}
static bool validRelief(double width, double thin, double thick) {
	return width >= 0.1 && width <= 10000 && thin >= 0.1 && thin < thick && thick <= 1000;
}

static QString writeLithophane(const QString& path, cv::Mat gray, double width, double thin,
	double thick, const std::atomic_bool& stop) {
	const double height = width * gray.rows / gray.cols;
	// Bound binary STL size below 100 MB, independent of the source resolution.
	const double scale = std::min(1.0, 900.0 / std::max(gray.rows, gray.cols));
	const int cols = std::max(2, qRound(gray.cols * scale));
	const int rows = std::max(2, qRound(gray.rows * scale));
	cv::resize(gray, gray, {cols, rows}, 0, 0, cv::INTER_AREA);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) return file.errorString();
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	out.setFloatingPointPrecision(QDataStream::SinglePrecision);
	const QByteArray header(80, '\0');
	out.writeRawData(header.constData(), header.size());
	const int perimeter = 2 * (rows + cols - 2);
	out << quint32(2 * (cols - 1) * (rows - 1) + 3 * perimeter);
	using Vertex = cv::Vec3f;
	auto vertex = [&](int x, int y) {
		const int intensity = gray.at<uchar>(rows - 1 - y, x);
		return Vertex(width * x / (cols - 1), height * y / (rows - 1),
			thin + (thick - thin) * (255 - intensity) / 255);
	};
	auto triangle = [&](const Vertex& a, const Vertex& b, const Vertex& c) {
		const Vertex normal = cv::normalize((b - a).cross(c - a));
		for (const Vertex* v : {&normal, &a, &b, &c}) out << (*v)[0] << (*v)[1] << (*v)[2];
		out << quint16(0);
	};
	const Vertex center(width / 2, height / 2, 0);
	auto closeEdge = [&](const Vertex& a, const Vertex& b) {
		const Vertex baseA(a[0], a[1], 0), baseB(b[0], b[1], 0);
		triangle(baseA, baseB, b);
		triangle(baseA, b, a);
		triangle(center, baseB, baseA);
	};
	for (int y = 0; y < rows - 1; ++y) {
		if (stop.load()) return QStringLiteral("已取消");
		for (int x = 0; x < cols - 1; ++x) {
			const Vertex a = vertex(x, y), b = vertex(x + 1, y);
			const Vertex c = vertex(x + 1, y + 1), d = vertex(x, y + 1);
			triangle(a, b, c);
			triangle(a, c, d);
			if (y == 0) closeEdge(a, b);
			if (x == cols - 2) closeEdge(b, c);
			if (y == rows - 2) closeEdge(c, d);
			if (x == 0) closeEdge(d, a);
		}
		if (out.status() != QDataStream::Ok) return file.errorString();
	}
	if (stop.load()) return QStringLiteral("已取消");
	if (!file.commit()) return file.errorString();
	return {};
}

void Controller::exportLithophane(const QUrl& destination, double width, double thin, double thick) {
	if (active || selectedLayer < 0 || !destination.isLocalFile()) return;
	if (!validRelief(width, thin, thick)) {
		message = QStringLiteral("请输入有效尺寸，最厚处必须大于最薄处");
		emit changed();
		return;
	}
	auto& layer = artworkLayers[selectedLayer];
	if (layer.reliefWidth != width || layer.reliefThin != thin || layer.reliefThick != thick) {
		recordEdit();
		layer.reliefWidth = width;
		layer.reliefThin = thin;
		layer.reliefThick = thick;
		refreshLayerPresentation();
	}
	QString path = destination.toLocalFile();
	if (!path.endsWith(".stl", Qt::CaseInsensitive)) path += ".stl";
	active = true;
	message = QStringLiteral("正在导出透光浮雕…");
	cancelled = std::make_shared<std::atomic_bool>(false);
	auto* watcher = new QFutureWatcher<QString>(this);
	connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
		const QString error = watcher->result();
		watcher->deleteLater();
		active = false;
		message = error.isEmpty() ? QStringLiteral("已导出透光浮雕：%1").arg(path) : error;
		emit changed();
	});
	watcher->setFuture(QtConcurrent::run([source = layer.sourceImage, options = layer.settings,
		stop = cancelled, path, width, thin, thick]() -> QString {
		try {
			return writeLithophane(path, adjustedGray(source, options), width, thin, thick, *stop);
		} catch (const std::exception& error) {
			return QString::fromUtf8(error.what());
		}
	}));
	emit changed();
}

void Controller::generatePcb(const QUrl& source, const QString& directory) {
	if (active) return;
	if (!hasExportableLayers()) {
		message = QStringLiteral("没有可用于生成 PCB 文件的可见图层");
		emit changed();
		return;
	}
	QString projectSource = source.toLocalFile();
	if (!QFileInfo::exists(projectSource) && !artworkLayers.empty())
		projectSource = artworkLayers.front().sourcePath;
	const QFileInfo sourceFile(projectSource);
	QString root = QStringLiteral(BINARIZER_SOURCE_DIR);
	QString exporter;
	const QDir applicationDirectory(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
	const QString exporterName = QStringLiteral("pcb-exporter.exe");
#else
	const QString exporterName = QStringLiteral("pcb-exporter");
#endif
	const QStringList exporterDirectories{
		QStringLiteral("../exporter"), QStringLiteral("../Resources/exporter")};
	for (const QString& directory : exporterDirectories) {
		const QString candidate = applicationDirectory.filePath(directory + QLatin1Char('/') + exporterName);
		if (!QFileInfo(candidate).isExecutable()) continue;
		exporter = QDir::cleanPath(candidate);
		root = QFileInfo(exporter).absolutePath();
		break;
	}
	const QString script = QDir(root).filePath(QStringLiteral("img2pcb.py"));
	if (exporter.isEmpty() && !QFileInfo::exists(script)) {
		message = QStringLiteral("找不到 Python PCB 导出器：%1").arg(script);
		emit changed();
		return;
	}
	QString python = qEnvironmentVariable("IMAGE_BINARIZER_PYTHON");
	if (exporter.isEmpty() && python.isEmpty()) {
		const QString unixEnvironment = QDir(root).filePath(QStringLiteral(".venv/bin/python"));
		const QString windowsEnvironment = QDir(root).filePath(QStringLiteral(".venv/Scripts/python.exe"));
		if (QFileInfo(unixEnvironment).isExecutable()) python = unixEnvironment;
		else if (QFileInfo(windowsEnvironment).isExecutable()) python = windowsEnvironment;
		else python = QStandardPaths::findExecutable(QStringLiteral("python3"));
		if (python.isEmpty()) python = QStandardPaths::findExecutable(QStringLiteral("python"));
	}
	if (exporter.isEmpty() && (python.isEmpty() || !QFileInfo(python).isExecutable())) {
		message = QStringLiteral("找不到可用的 Python 运行环境");
		emit changed();
		return;
	}
	QDir outputDirectory(directory.isEmpty() ? sourceFile.absolutePath() : directory);
	if (!outputDirectory.exists() && !QDir().mkpath(outputDirectory.absolutePath())) {
		message = QStringLiteral("无法创建输出目录：%1").arg(outputDirectory.absolutePath());
		emit changed();
		return;
	}
	pcbTemporaryDirectory = std::make_unique<QTemporaryDir>(
		QDir::temp().filePath(QStringLiteral("image-binarizer-pcb-XXXXXX")));
	if (!pcbTemporaryDirectory->isValid()) {
		message = QStringLiteral("无法创建 PCB 导出临时目录");
		pcbTemporaryDirectory.reset();
		emit changed();
		return;
	}
	pcbDestination = outputDirectory.filePath(sourceFile.completeBaseName() + QStringLiteral(".epro2"));
	pcbCancelled = false;
	const QSize canvas = canvasSize();
	QJsonArray exportedLayers;
	for (int index = 0; index < int(artworkLayers.size()); ++index) {
		const auto& layer = artworkLayers[index];
		if (!layer.visible) continue;
		const QImage& image = artworkImage(layer);
		if (image.isNull()) continue;
		const QString path = QDir(pcbTemporaryDirectory->path())
			.filePath(QStringLiteral("layer-%1.png").arg(index));
		if (!image.save(path, "PNG")) {
			message = QStringLiteral("无法写入 PCB 导出临时图层");
			pcbTemporaryDirectory.reset();
			emit changed();
			return;
		}
		exportedLayers.append(QJsonObject{{QStringLiteral("source"), path},
			{QStringLiteral("type"), layer.type == QStringLiteral("outline") ? QStringLiteral("enig") : layer.type},
			{QStringLiteral("x"), layer.x},
			{QStringLiteral("y"), layer.y}, {QStringLiteral("side"), layer.side}});
	}
	const ArtworkLayer* resolutionLayer = nullptr;
	if (selectedLayer >= 0 && selectedLayer < int(artworkLayers.size())
		&& artworkLayers[selectedLayer].visible && artworkLayers[selectedLayer].hasPhysicalResolution)
		resolutionLayer = &artworkLayers[selectedLayer];
	if (!resolutionLayer) {
		for (const auto& layer : artworkLayers)
			if (layer.visible && !artworkImage(layer).isNull() && layer.hasPhysicalResolution) {
				resolutionLayer = &layer;
				break;
			}
	}
	QJsonObject canvasSettings{{QStringLiteral("width"), canvas.width()},
		{QStringLiteral("height"), canvas.height()}};
	double widthMm;
	double heightMm;
	if (resolutionLayer) {
		widthMm = canvas.width() * 1000.0 / resolutionLayer->dotsPerMeterX;
		heightMm = canvas.height() * 1000.0 / resolutionLayer->dotsPerMeterY;
		pcbDimensionMessage = QStringLiteral("按图片 DPI：图案 %1 × %2 mm，板框 %3 × %4 mm")
			.arg(widthMm, 0, 'f', 2).arg(heightMm, 0, 'f', 2)
			.arg(widthMm + 2.0, 0, 'f', 2).arg(heightMm + 2.0, 0, 'f', 2);
	} else {
		constexpr double defaultDpi = 300.0;
		widthMm = canvas.width() * 25.4 / defaultDpi;
		heightMm = canvas.height() * 25.4 / defaultDpi;
		pcbDimensionMessage = QStringLiteral("未检测到可靠 DPI，按默认 300 DPI：图案 %1 × %2 mm，板框 %3 × %4 mm")
			.arg(widthMm, 0, 'f', 2).arg(heightMm, 0, 'f', 2)
			.arg(widthMm + 2.0, 0, 'f', 2).arg(heightMm + 2.0, 0, 'f', 2);
	}
	canvasSettings.insert(QStringLiteral("widthMm"), widthMm);
	canvasSettings.insert(QStringLiteral("heightMm"), heightMm);
	const QJsonObject manifestObject{
		{QStringLiteral("canvas"), canvasSettings},
		{QStringLiteral("projectName"), sourceFile.completeBaseName()},
		{QStringLiteral("solderMaskColor"), solderMaskPreviewColor(maskColor).name(QColor::HexRgb).toUpper()},
		{QStringLiteral("layers"), exportedLayers}
	};
	const QString manifestPath = QDir(pcbTemporaryDirectory->path()).filePath(QStringLiteral("layers.json"));
	QFile manifest(manifestPath);
	if (!manifest.open(QIODevice::WriteOnly)
		|| manifest.write(QJsonDocument(manifestObject).toJson(QJsonDocument::Compact)) < 0) {
		message = QStringLiteral("无法写入 PCB 导出图层清单");
		pcbTemporaryDirectory.reset();
		emit changed();
		return;
	}
	manifest.close();
	QStringList arguments{QStringLiteral("--manifest"), manifestPath,
		QStringLiteral("-o"), pcbDestination};
	if (exporter.isEmpty()) arguments.prepend(script);
	auto* process = new QProcess(this);
	pcbProcess = process;
	process->setProgram(exporter.isEmpty() ? python : exporter);
	process->setArguments(arguments);
	process->setWorkingDirectory(root);
	connect(process, &QProcess::finished, this,
		[this, process](int exitCode, QProcess::ExitStatus status) {
			finishPcb(process, exitCode, status != QProcess::NormalExit);
		});
	connect(process, &QProcess::errorOccurred, this,
		[this, process](QProcess::ProcessError error) {
			if (error == QProcess::FailedToStart) finishPcb(process, -1, true);
		});
	active = true;
	message = QStringLiteral("正在生成 PCB 文件…（%1）").arg(pcbDimensionMessage);
	process->start();
	emit changed();
}
void Controller::finishPcb(QProcess* process, int exitCode, bool processFailed) {
	if (!pcbProcess || pcbProcess != process) return;
	const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
	active = false;
	if (pcbCancelled) message = QStringLiteral("已取消 PCB 文件生成");
	else if (processFailed || exitCode != 0 || !QFileInfo::exists(pcbDestination))
		message = error.isEmpty() ? QStringLiteral("PCB 文件生成失败") : error;
	else message = QStringLiteral("已生成 PCB 文件：%1（%2）").arg(pcbDestination, pcbDimensionMessage);
	pcbProcess.clear();
	process->deleteLater();
	pcbTemporaryDirectory.reset();
	pcbDestination.clear();
	pcbDimensionMessage.clear();
	pcbCancelled = false;
	emit changed();
}

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
			auto params = parametersFor(layer.settings);
			params.insert("reliefWidth", layer.reliefWidth);
			params.insert("reliefThin", layer.reliefThin);
			params.insert("reliefThick", layer.reliefThick);
			out << layer.id << layer.name << layer.sourcePath << layer.type << layer.side << layer.visible
				<< layer.x << layer.y << params << layer.dotsPerMeterX
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
		layer.reliefWidth = params.value("reliefWidth", layer.reliefWidth).toDouble();
		layer.reliefThin = params.value("reliefThin", layer.reliefThin).toDouble();
		layer.reliefThick = params.value("reliefThick", layer.reliefThick).toDouble();
		if (!validRelief(layer.reliefWidth, layer.reliefThin, layer.reliefThick)) return fail(QStringLiteral("浮雕尺寸无效"));
		layer.settings = optionsFrom(params, method);
		layer.embedded = true;
		layer.sourceImage = sourcePixels(layer.originalImage);
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
