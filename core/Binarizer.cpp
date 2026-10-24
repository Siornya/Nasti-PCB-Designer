#include "Binarizer.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace binarizer {
std::string_view name(Method method) {
	switch (method) {
	case Method::Fixed: return "Fixed";
	case Method::Adaptive: return "Adaptive";
	case Method::Otsu: return "Otsu";
	case Method::Triangle: return "Triangle";
	case Method::Li: return "Li";
	case Method::Sauvola: return "Sauvola";
	case Method::Wolf: return "Wolf";
	case Method::Bernsen: return "Bernsen";
	}
	throw std::invalid_argument("Unknown threshold method");
}
static void validate(const Options& o) {
	if (o.threshold < 0 || o.threshold > 255 || o.blockSize < 3 || o.blockSize % 2 == 0
		|| !std::isfinite(o.gamma) || o.gamma <= 0 || o.maxDimension < 0
		|| !std::isfinite(o.adaptiveC) || std::abs(o.adaptiveC) > 100
		|| !std::isfinite(o.localK) || std::abs(o.localK) > 1
		|| std::abs(o.exposure) > 100 || std::abs(o.contrast) > 100
		|| o.denoiseMethod < 0 || o.denoiseMethod > 3
		|| o.edgeLowThreshold < 0 || o.edgeLowThreshold > 1000
		|| o.edgeHighThreshold < 0 || o.edgeHighThreshold > 1000
		|| o.edgeWidth < 1 || o.edgeWidth > 100
		|| !std::isfinite(o.edgeTolerance) || o.edgeTolerance < 0 || o.edgeTolerance > 100)
		throw std::invalid_argument("Invalid binarization parameters");
	for (int value : {o.denoiseStrength, o.smooth, o.sharpen, o.detail, o.edge, o.localContrast})
		if (value < 0 || value > 100) throw std::invalid_argument("Strength must be 0..100");
}
static int kernel(int strength) {
	return strength <= 0 ? 1 : std::min(21, 2 * std::max(1, static_cast<int>(std::nearbyint(strength / 10.0))) + 1);
}
cv::Mat preprocess(const cv::Mat& source, const Options& o) {
	validate(o);
	if (source.empty() || source.depth() != CV_8U)
		throw std::invalid_argument("Expected a nonempty 8-bit image");
	cv::Mat gray;
	if (source.channels() == 1) gray = source.clone();
	else if (source.channels() == 3) cv::cvtColor(source, gray, cv::COLOR_BGR2GRAY);
	else if (source.channels() == 4) {
		cv::Mat color(source.size(), CV_8UC3);
		for (int y = 0; y < source.rows; ++y) {
			const auto* input = source.ptr<cv::Vec4b>(y);
			auto* output = color.ptr<cv::Vec3b>(y);
			for (int x = 0; x < source.cols; ++x) {
				const float alpha = input[x][3] / 255.0f;
				for (int c = 0; c < 3; ++c)
					output[x][c] = static_cast<unsigned char>(input[x][c] * alpha + 255.0f * (1 - alpha));
			}
		}
		cv::cvtColor(color, gray, cv::COLOR_BGR2GRAY);
	} else throw std::invalid_argument("Expected gray, BGR or BGRA image");
	if (o.maxDimension > 0 && std::max(gray.cols, gray.rows) > o.maxDimension) {
		const double scale = double(o.maxDimension) / std::max(gray.cols, gray.rows);
		cv::resize(gray, gray, cv::Size(std::max(1, int(std::nearbyint(gray.cols * scale))),
			std::max(1, int(std::nearbyint(gray.rows * scale)))), 0, 0, cv::INTER_AREA);
	}
	cv::Mat lut(1, 256, CV_8U);
	for (int i = 0; i < 256; ++i) {
		double v = i * std::pow(2.0, o.exposure / 50.0);
		v = std::clamp((v - 127.5) * std::max(0.0, 1 + o.contrast / 50.0) + 127.5, 0.0, 255.0);
		if (o.gamma != 1) v = 255 * std::pow(v / 255, 1 / o.gamma);
		lut.at<unsigned char>(i) = static_cast<unsigned char>(std::clamp(v, 0.0, 255.0));
	}
	cv::LUT(gray, lut, gray);
	const int k = kernel(o.denoiseStrength);
	if (o.denoiseStrength) {
		switch (o.denoiseMethod) {
		case 0: cv::GaussianBlur(gray, gray, cv::Size(k, k), 0); break;
		case 1: cv::medianBlur(gray, gray, k); break;
		case 2: {
			cv::Mat filtered;
			cv::bilateralFilter(gray, filtered, k, std::max(10, o.denoiseStrength * 2), std::max(10, o.denoiseStrength * 2));
			gray = filtered;
			break;
		}
		case 3: cv::fastNlMeansDenoising(gray, gray, float(o.denoiseStrength), 7, 21); break;
		}
	}
	if (o.smooth) cv::GaussianBlur(gray, gray, cv::Size(kernel(o.smooth), kernel(o.smooth)), 0);
	if (o.clahe) cv::createCLAHE(2.0, cv::Size(8, 8))->apply(gray, gray);
	else if (o.equalize) cv::equalizeHist(gray, gray);
	const double amount = (o.sharpen + o.detail) / 100.0;
	if (amount) {
		cv::Mat blurred;
		cv::GaussianBlur(gray, blurred, cv::Size(), 1.2);
		cv::addWeighted(gray, 1 + amount, blurred, -amount, 0, gray);
	}
	if (o.edge) {
		cv::Mat laplacian;
		cv::Laplacian(gray, laplacian, CV_32F, 3);
		for (int y = 0; y < gray.rows; ++y)
			for (int x = 0; x < gray.cols; ++x)
				gray.at<unsigned char>(y, x) = static_cast<unsigned char>(std::clamp(
					gray.at<unsigned char>(y, x) - float(o.edge / 200.0) * laplacian.at<float>(y, x), 0.0f, 255.0f));
	}
	if (o.localContrast) {
		cv::Mat enhanced;
		cv::createCLAHE(1 + o.localContrast / 25.0, cv::Size(8, 8))->apply(gray, enhanced);
		cv::addWeighted(gray, 1 - o.localContrast / 100.0, enhanced, o.localContrast / 100.0, 0, gray);
	}
	return gray;
}
static double liThreshold(const cv::Mat& gray) {
	std::array<double, 256> histogram{};
	for (int y = 0; y < gray.rows; ++y) {
		const auto* line = gray.ptr<unsigned char>(y);
		for (int x = 0; x < gray.cols; ++x) ++histogram[line[x]];
	}
	int minimum = 0;
	while (minimum < 255 && histogram[minimum] == 0) ++minimum;
	int maximum = 255;
	while (maximum > 0 && histogram[maximum] == 0) --maximum;
	if (minimum == maximum) return minimum;
	double total = 0;
	double sum = 0;
	for (int level = minimum; level <= maximum; ++level) {
		total += histogram[level];
		sum += histogram[level] * (level - minimum);
	}
	double next = sum / total;
	for (int iteration = 0; iteration < 100; ++iteration) {
		const double current = next;
		double backgroundCount = 0;
		double backgroundSum = 0;
		double foregroundCount = 0;
		double foregroundSum = 0;
		for (int level = minimum; level <= maximum; ++level) {
			const double shifted = level - minimum;
			if (shifted <= current) {
				backgroundCount += histogram[level];
				backgroundSum += histogram[level] * shifted;
			} else {
				foregroundCount += histogram[level];
				foregroundSum += histogram[level] * shifted;
			}
		}
		if (backgroundCount == 0 || foregroundCount == 0) break;
		const double backgroundMean = backgroundSum / backgroundCount;
		const double foregroundMean = foregroundSum / foregroundCount;
		if (backgroundMean <= 0 || foregroundMean <= 0) break;
		const double denominator = std::log(backgroundMean) - std::log(foregroundMean);
		if (std::abs(denominator) < 1e-12) break;
		next = (backgroundMean - foregroundMean) / denominator;
		if (!std::isfinite(next) || std::abs(next - current) <= 0.5) break;
	}
	return next + minimum;
}
cv::Mat threshold(const cv::Mat& gray, const Options& o, double* thresholdValue) {
	validate(o);
	if (gray.empty() || gray.type() != CV_8UC1) throw std::invalid_argument("Expected grayscale image");
	cv::Mat binary;
	if (thresholdValue) *thresholdValue = std::numeric_limits<double>::quiet_NaN();
	if (o.method == Method::Fixed) {
		if (thresholdValue) *thresholdValue = o.threshold;
		cv::threshold(gray, binary, o.threshold, 255, cv::THRESH_BINARY);
	} else if (o.method == Method::Otsu) {
		const double value = cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
		if (thresholdValue) *thresholdValue = value;
	} else if (o.method == Method::Triangle) {
		const double value = cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_TRIANGLE);
		if (thresholdValue) *thresholdValue = value;
	} else if (o.method == Method::Li) {
		const double value = liThreshold(gray);
		if (thresholdValue) *thresholdValue = value;
		cv::threshold(gray, binary, value, 255, cv::THRESH_BINARY);
	} else if (o.method == Method::Adaptive)
		cv::adaptiveThreshold(gray, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C, cv::THRESH_BINARY, o.blockSize, o.adaptiveC);
	else {
		cv::Mat source, mean, squareMean, variance, deviation, thresholdMap;
		gray.convertTo(source, CV_32F);
		const cv::Size size(o.blockSize, o.blockSize);
		cv::boxFilter(source, mean, CV_32F, size);
		cv::boxFilter(source.mul(source), squareMean, CV_32F, size);
		cv::max(squareMean - mean.mul(mean), 0, variance);
		cv::sqrt(variance, deviation);
		switch (o.method) {
		case Method::Sauvola: thresholdMap = mean.mul(1 + o.localK * (deviation / 128 - 1)); break;
		case Method::Wolf: {
			double maximum, minimum;
			cv::minMaxLoc(deviation, nullptr, &maximum);
			cv::minMaxLoc(source, &minimum);
			thresholdMap = mean + o.localK * (deviation / std::max(maximum, 1e-6) - 1).mul(mean - minimum);
			break;
		}
		case Method::Bernsen: {
			cv::Mat minimum, maximum, lo, hi;
			const auto element = cv::getStructuringElement(cv::MORPH_RECT, size);
			cv::erode(gray, minimum, element);
			cv::dilate(gray, maximum, element);
			minimum.convertTo(lo, CV_32F);
			maximum.convertTo(hi, CV_32F);
			thresholdMap = (lo + hi) / 2;
			thresholdMap.setTo(o.threshold, hi - lo < 15);
			break;
		}
		default: throw std::invalid_argument("Unknown threshold method");
		}
		cv::compare(source, thresholdMap, binary, cv::CMP_GT);
	}
	if (o.invert) cv::bitwise_not(binary, binary);
	if (o.flipHorizontal) cv::flip(binary, binary, 1);
	if (o.flipVertical) cv::flip(binary, binary, 0);
	return binary;
}
cv::Mat process(const cv::Mat& source, const Options& options) {
	const cv::Mat gray = preprocess(source, options);
	return finish(threshold(gray, options), gray, source, options);
}

namespace {
const std::array<cv::Point, 8> neighborOffsets{{
	{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
}};

bool activePixel(const cv::Mat& edges, const cv::Point& point) {
	return point.x >= 0 && point.x < edges.cols && point.y >= 0 && point.y < edges.rows
		&& edges.at<uchar>(point) != 0;
}

bool connectedNeighbor(const cv::Mat& edges, const cv::Point& point, int direction) {
	const cv::Point offset = neighborOffsets[direction];
	if (!activePixel(edges, point + offset)) return false;
	if (offset.x != 0 && offset.y != 0
		&& (activePixel(edges, point + cv::Point(offset.x, 0))
			|| activePixel(edges, point + cv::Point(0, offset.y)))) return false;
	return true;
}

cv::Mat buildAdjacency(const cv::Mat& edges) {
	cv::Mat adjacency = cv::Mat::zeros(edges.size(), CV_8UC1);
	for (int y = 0; y < edges.rows; ++y)
		for (int x = 0; x < edges.cols; ++x) {
			const cv::Point point(x, y);
			if (!activePixel(edges, point)) continue;
			uchar mask = 0;
			for (int direction = 0; direction < int(neighborOffsets.size()); ++direction)
				if (connectedNeighbor(edges, point, direction)) mask |= static_cast<uchar>(1u << direction);
			adjacency.at<uchar>(point) = mask;
		}
	return adjacency;
}

bool visitedEdge(const cv::Mat& visited, const cv::Point& point, int direction) {
	return (visited.at<uchar>(point) & (1u << direction)) != 0;
}

void markVisitedEdge(cv::Mat& visited, const cv::Point& point, int direction) {
	visited.at<uchar>(point) |= static_cast<uchar>(1u << direction);
	const cv::Point neighbor = point + neighborOffsets[direction];
	visited.at<uchar>(neighbor) |= static_cast<uchar>(1u << ((direction + 4) % 8));
}

struct EdgePath {
	std::vector<cv::Point> points;
	bool closed = false;
};

EdgePath traceEdgePath(const cv::Mat& adjacency, cv::Mat& visited,
	const cv::Point& start, int firstDirection) {
	EdgePath path;
	path.points.push_back(start);
	cv::Point current = start;
	int direction = firstDirection;
	while (direction >= 0) {
		const cv::Point next = current + neighborOffsets[direction];
		markVisitedEdge(visited, current, direction);
		if (next == start) {
			path.closed = true;
			break;
		}
		path.points.push_back(next);
		if (std::popcount(adjacency.at<uchar>(next)) != 2) break;
		current = next;
		direction = -1;
		for (int candidate = 0; candidate < int(neighborOffsets.size()); ++candidate)
			if ((adjacency.at<uchar>(current) & (1u << candidate)) != 0
				&& !visitedEdge(visited, current, candidate)) {
				direction = candidate;
				break;
			}
	}
	return path;
}

int componentCount(const cv::Mat& mask, int connectivity) {
	cv::Mat labels;
	return cv::connectedComponents(mask, labels, connectivity, CV_32S) - 1;
}

double maximumMaskDistance(const cv::Mat& from, const cv::Mat& to) {
	if (cv::countNonZero(from) == 0) return 0;
	cv::Mat inverse, distances;
	cv::bitwise_not(to, inverse);
	cv::distanceTransform(inverse, distances, cv::DIST_L2, cv::DIST_MASK_PRECISE);
	double maximum = 0;
	cv::minMaxLoc(distances, nullptr, &maximum, nullptr, nullptr, from);
	return maximum;
}

int rectangleCount(const cv::Mat& mask) {
	std::vector<std::pair<int, int>> previous;
	int count = 0;
	for (int y = 0; y < mask.rows; ++y) {
		std::vector<std::pair<int, int>> current;
		const uchar* line = mask.ptr<uchar>(y);
		for (int x = 0; x < mask.cols;) {
			while (x < mask.cols && line[x] == 0) ++x;
			const int start = x;
			while (x < mask.cols && line[x] != 0) ++x;
			if (start < x) current.emplace_back(start, x);
		}
		size_t previousIndex = 0;
		for (const auto& run : current) {
			while (previousIndex < previous.size() && previous[previousIndex] < run) ++previousIndex;
			if (previousIndex == previous.size() || previous[previousIndex] != run) ++count;
		}
		previous = std::move(current);
	}
	return count;
}

EdgeSimplificationStatus assessSimplification(const cv::Mat& original,
	const cv::Mat& simplified, double tolerance, EdgeSimplificationStats* stats) {
	const int originalRectangles = rectangleCount(original);
	const int candidateRectangles = rectangleCount(simplified);
	auto finish = [&](EdgeSimplificationStatus status) {
		if (stats) {
			stats->status = status;
			stats->originalRectangles = originalRectangles;
			stats->candidateRectangles = candidateRectangles;
			stats->resultRectangles = status == EdgeSimplificationStatus::Applied
				? candidateRectangles : originalRectangles;
		}
		return status;
	};
	if (cv::countNonZero(original != simplified) == 0)
		return finish(EdgeSimplificationStatus::Unchanged);
	if (candidateRectangles > originalRectangles)
		return finish(EdgeSimplificationStatus::PrimitiveCountIncreased);
	cv::Mat originalBackground, simplifiedBackground;
	cv::bitwise_not(original, originalBackground);
	cv::bitwise_not(simplified, simplifiedBackground);
	if (componentCount(original, 8) != componentCount(simplified, 8)
		|| componentCount(originalBackground, 4) != componentCount(simplifiedBackground, 4))
		return finish(EdgeSimplificationStatus::TopologyChanged);
	const double allowedDistance = tolerance + std::sqrt(2.0) + 0.25;
	if (maximumMaskDistance(original, simplified) > allowedDistance
		|| maximumMaskDistance(simplified, original) > allowedDistance)
		return finish(EdgeSimplificationStatus::DeviationExceeded);
	return finish(EdgeSimplificationStatus::Applied);
}
}

cv::Mat simplifyEdgeMask(const cv::Mat& edges, double tolerance,
	EdgeSimplificationStats* stats) {
	if (edges.empty() || edges.type() != CV_8UC1)
		throw std::invalid_argument("Expected an 8-bit edge mask");
	if (!std::isfinite(tolerance) || tolerance < 0)
		throw std::invalid_argument("Invalid edge simplification tolerance");
	cv::Mat binary;
	cv::compare(edges, 0, binary, cv::CMP_GT);
	if (tolerance == 0 || cv::countNonZero(binary) < 2) {
		assessSimplification(binary, binary, tolerance, stats);
		return binary;
	}

	cv::Mat simplified = cv::Mat::zeros(binary.size(), CV_8UC1);
	const cv::Mat adjacency = buildAdjacency(binary);
	cv::Mat visited = cv::Mat::zeros(binary.size(), CV_8UC1);
	auto drawPath = [&](const EdgePath& path) {
		if (path.points.empty()) return;
		if (path.points.size() == 1) {
			simplified.at<uchar>(path.points.front()) = 255;
			return;
		}
		std::vector<cv::Point> points;
		cv::approxPolyDP(path.points, points, tolerance, path.closed);
		if (points.size() < 2) points = path.points;
		if (!path.closed) {
			points.front() = path.points.front();
			points.back() = path.points.back();
		}
		cv::polylines(simplified, points, path.closed, cv::Scalar(255), 1, cv::LINE_8);
	};

	for (int y = 0; y < binary.rows; ++y)
		for (int x = 0; x < binary.cols; ++x) {
			const cv::Point point(x, y);
			if (!activePixel(binary, point)) continue;
			const int degree = std::popcount(adjacency.at<uchar>(point));
			if (degree == 0) {
				simplified.at<uchar>(point) = 255;
				continue;
			}
			if (degree == 2) continue;
			for (int direction = 0; direction < int(neighborOffsets.size()); ++direction)
				if ((adjacency.at<uchar>(point) & (1u << direction)) != 0
					&& !visitedEdge(visited, point, direction))
					drawPath(traceEdgePath(adjacency, visited, point, direction));
		}

	for (int y = 0; y < binary.rows; ++y)
		for (int x = 0; x < binary.cols; ++x) {
			const cv::Point point(x, y);
			if (!activePixel(binary, point)) continue;
			for (int direction = 0; direction < int(neighborOffsets.size()); ++direction)
				if ((adjacency.at<uchar>(point) & (1u << direction)) != 0
					&& !visitedEdge(visited, point, direction)) {
					drawPath(traceEdgePath(adjacency, visited, point, direction));
					break;
				}
		}

	return assessSimplification(binary, simplified, tolerance, stats)
		== EdgeSimplificationStatus::Applied ? simplified : binary;
}

cv::Mat edgeMask(const cv::Mat& gray, const cv::Mat& source, const Options& options,
	EdgeSimplificationStats* stats) {
	validate(options);
	if (stats) *stats = {};
	if (gray.empty() || gray.type() != CV_8UC1)
		throw std::invalid_argument("Expected grayscale image");
	if (source.empty() || source.depth() != CV_8U || (source.channels() != 1
		&& source.channels() != 3 && source.channels() != 4))
		throw std::invalid_argument("Expected gray, BGR or BGRA source image");
	if (!options.extractEdges) return cv::Mat::zeros(gray.size(), CV_8UC1);
	cv::Mat edges;
	const int low = std::min(options.edgeLowThreshold, options.edgeHighThreshold);
	const int high = std::max(options.edgeLowThreshold, options.edgeHighThreshold);
	cv::Canny(gray, edges, low, high);
	if (source.channels() == 4) {
		cv::Mat alpha;
		cv::extractChannel(source, alpha, 3);
		double minimum;
		cv::minMaxLoc(alpha, &minimum);
		if (minimum < 255) {
			// Alpha retains the boundary of light-colored transparent artwork.
			if (alpha.size() != gray.size())
				cv::resize(alpha, alpha, gray.size(), 0, 0, cv::INTER_AREA);
			cv::Mat alphaEdges;
			cv::Canny(alpha, alphaEdges, low, high);
			cv::bitwise_or(edges, alphaEdges, edges);
		}
	}
	const double scale = double(gray.cols) / source.cols;
	const double tolerance = options.edgeTolerance * scale;
	cv::Mat originalEdges;
	bool simplified = false;
	EdgeSimplificationStats simplificationStats;
	if (options.simplifyEdges && options.edgeTolerance > 0) {
		originalEdges = edges.clone();
		edges = simplifyEdgeMask(edges, tolerance, &simplificationStats);
		simplified = cv::countNonZero(originalEdges != edges) > 0;
	}
	const int width = std::max(1, int(std::lround(options.edgeWidth * scale)));
	if (width > 1) {
		const cv::Mat element = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(width, width));
		cv::dilate(edges, edges, element);
		if (simplified) {
			cv::dilate(originalEdges, originalEdges, element);
			EdgeSimplificationStats expandedStats;
			if (assessSimplification(originalEdges, edges, tolerance, &expandedStats)
				!= EdgeSimplificationStatus::Applied) edges = originalEdges;
			simplificationStats = expandedStats;
		}
	}
	if (stats && options.simplifyEdges) *stats = simplificationStats;
	if (options.flipHorizontal) cv::flip(edges, edges, 1);
	if (options.flipVertical) cv::flip(edges, edges, 0);
	return edges;
}
cv::Mat overlayEdges(const cv::Mat& binary, const cv::Mat& edges) {
	if (binary.empty() || binary.type() != CV_8UC1)
		throw std::invalid_argument("Expected binary grayscale image");
	if (edges.empty()) return binary;
	if (edges.type() != CV_8UC1 || edges.size() != binary.size())
		throw std::invalid_argument("Expected an equally sized edge mask");
	cv::Mat result = binary.clone();
	result.setTo(0, edges);
	return result;
}
cv::Mat finish(const cv::Mat& binary, const cv::Mat& gray, const cv::Mat& source, const Options& options) {
	validate(options);
	if (binary.empty() || binary.type() != CV_8UC1 || gray.type() != CV_8UC1
		|| binary.size() != gray.size())
		throw std::invalid_argument("Expected equally sized grayscale images");
	if (!options.extractEdges) return binary;
	return overlayEdges(binary, edgeMask(gray, source, options));
}
}
