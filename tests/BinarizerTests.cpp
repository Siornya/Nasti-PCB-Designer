#include "Binarizer.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
}

static int componentCount(const cv::Mat& mask, int connectivity) {
	cv::Mat labels;
	return cv::connectedComponents(mask, labels, connectivity, CV_32S) - 1;
}

static int rectangleCount(const cv::Mat& mask) {
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

static double maximumDistance(const cv::Mat& from, const cv::Mat& to) {
	cv::Mat inverse, distances;
	cv::bitwise_not(to, inverse);
	cv::distanceTransform(inverse, distances, cv::DIST_L2, cv::DIST_MASK_PRECISE);
	double maximum = 0;
	cv::minMaxLoc(distances, nullptr, &maximum, nullptr, nullptr, from);
	return maximum;
}

static void checkTopologyAndDistance(const cv::Mat& original, const cv::Mat& simplified,
	double tolerance) {
	check(componentCount(original, 8) == componentCount(simplified, 8),
		"Simplification changed the number of edge components");
	cv::Mat originalBackground, simplifiedBackground;
	cv::bitwise_not(original, originalBackground);
	cv::bitwise_not(simplified, simplifiedBackground);
	check(componentCount(originalBackground, 4) == componentCount(simplifiedBackground, 4),
		"Simplification opened or closed a bounded region");
	const double allowed = tolerance + std::sqrt(2.0) + 0.25;
	check(maximumDistance(original, simplified) <= allowed
		&& maximumDistance(simplified, original) <= allowed,
		"Simplification exceeded its geometric tolerance");
}

static void testOpenStroke() {
	cv::Mat original = cv::Mat::zeros(100, 180, CV_8UC1);
	std::vector<cv::Point> path;
	for (int x = 10; x < 170; ++x) path.emplace_back(x, 50 + ((x / 4) % 2));
	cv::polylines(original, path, false, cv::Scalar(255), 1, cv::LINE_8);
	const double tolerance = 2.0;
	binarizer::EdgeSimplificationStats stats;
	const cv::Mat simplified = binarizer::simplifyEdgeMask(original, tolerance, &stats);
	check(stats.status == binarizer::EdgeSimplificationStatus::Applied
		&& stats.resultRectangles < stats.originalRectangles,
		"Open-stroke simplification did not report its applied result");
	check(cv::countNonZero(original != simplified) > 0,
		"A noisy open stroke was not simplified");
	check(rectangleCount(simplified) < rectangleCount(original),
		"Open-stroke simplification did not reduce exported rectangles");
	check(simplified.at<uchar>(path.front()) != 0 && simplified.at<uchar>(path.back()) != 0,
		"Open-stroke endpoints moved or disappeared");
	checkTopologyAndDistance(original, simplified, tolerance);
}

static void testClosedLoop() {
	cv::Mat original = cv::Mat::zeros(160, 160, CV_8UC1);
	std::vector<cv::Point> loop;
	for (int x = 25; x <= 135; ++x) loop.emplace_back(x, 25 + ((x / 5) % 2));
	for (int y = 26; y <= 135; ++y) loop.emplace_back(135 - ((y / 5) % 2), y);
	for (int x = 134; x >= 25; --x) loop.emplace_back(x, 135 - ((x / 5) % 2));
	for (int y = 134; y >= 26; --y) loop.emplace_back(25 + ((y / 5) % 2), y);
	cv::polylines(original, loop, true, cv::Scalar(255), 1, cv::LINE_8);
	const double tolerance = 2.0;
	const cv::Mat simplified = binarizer::simplifyEdgeMask(original, tolerance);
	check(cv::countNonZero(original != simplified) > 0,
		"A noisy closed loop was not simplified");
	check(rectangleCount(simplified) < rectangleCount(original),
		"Closed-loop simplification did not reduce exported rectangles");
	checkTopologyAndDistance(original, simplified, tolerance);
}

static void testJunctions() {
	cv::Mat original = cv::Mat::zeros(120, 120, CV_8UC1);
	const cv::Point junction(60, 55);
	const std::vector<cv::Point> endpoints{{60, 105}, {15, 15}, {105, 15}};
	for (const cv::Point& endpoint : endpoints)
		cv::line(original, junction, endpoint, cv::Scalar(255), 1, cv::LINE_8);
	const cv::Mat simplified = binarizer::simplifyEdgeMask(original, 2.0);
	check(simplified.at<uchar>(junction) != 0, "Simplification removed a junction");
	for (const cv::Point& endpoint : endpoints)
		check(simplified.at<uchar>(endpoint) != 0, "Simplification removed a branch endpoint");
	checkTopologyAndDistance(original, simplified, 2.0);
}

static void testCannyIntegration() {
	cv::Mat source(64, 64, CV_8UC4, cv::Scalar(255, 255, 255, 0));
	cv::rectangle(source, {16, 16}, {47, 47}, cv::Scalar(255, 255, 255, 255), cv::FILLED);
	cv::rectangle(source, {28, 28}, {35, 35}, cv::Scalar(180, 180, 180, 255), cv::FILLED);
	binarizer::Options options;
	options.extractEdges = true;
	options.edgeWidth = 1;
	const cv::Mat gray = binarizer::preprocess(source, options);
	const cv::Mat original = binarizer::edgeMask(gray, source, options);
	options.simplifyEdges = true;
	options.edgeTolerance = 1.0;
	const cv::Mat simplified = binarizer::edgeMask(gray, source, options);
	check(cv::countNonZero(simplified(cv::Rect(12, 22, 8, 6))) > 0,
		"Simplified Canny mask lost the transparent outer boundary");
	check(cv::countNonZero(simplified(cv::Rect(24, 29, 7, 4))) > 0,
		"Simplified Canny mask lost an internal edge");
	check(simplified.at<uchar>(20, 20) == 0,
		"Simplified Canny mask filled the artwork interior");
	checkTopologyAndDistance(original, simplified, options.edgeTolerance);
}

static void testCannySimplificationEffect() {
	cv::Mat source(200, 200, CV_8UC1, cv::Scalar(0));
	std::vector<cv::Point> loop;
	for (int x = 25; x <= 175; ++x) loop.emplace_back(x, 25 + ((x / 5) % 2) * 2);
	for (int y = 26; y <= 175; ++y) loop.emplace_back(175 - ((y / 5) % 2) * 2, y);
	for (int x = 174; x >= 25; --x) loop.emplace_back(x, 175 - ((x / 5) % 2) * 2);
	for (int y = 174; y >= 26; --y) loop.emplace_back(25 + ((y / 5) % 2) * 2, y);
	cv::polylines(source, loop, true, cv::Scalar(255), 3, cv::LINE_8);
	binarizer::Options options;
	options.extractEdges = true;
	options.edgeWidth = 1;
	const cv::Mat gray = binarizer::preprocess(source, options);
	const cv::Mat original = binarizer::edgeMask(gray, source, options);
	options.simplifyEdges = true;
	options.edgeTolerance = 4.0;
	binarizer::EdgeSimplificationStats stats;
	const cv::Mat simplified = binarizer::edgeMask(gray, source, options, &stats);
	check(stats.status == binarizer::EdgeSimplificationStatus::Applied,
		"Canny integration did not report an applied simplification");
	check(cv::countNonZero(original != simplified) > 0,
		"Canny integration did not simplify a noisy closed edge");
	check(rectangleCount(simplified) < rectangleCount(original),
		"Canny integration did not reduce exported rectangles");
	checkTopologyAndDistance(original, simplified, options.edgeTolerance);

	options.simplifyEdges = false;
	options.edgeWidth = 3;
	const cv::Mat wideOriginal = binarizer::edgeMask(gray, source, options);
	options.simplifyEdges = true;
	const cv::Mat wideSimplified = binarizer::edgeMask(gray, source, options, &stats);
	check(stats.status != binarizer::EdgeSimplificationStatus::NotRequested,
		"Expanded Canny simplification did not report its decision");
	check(rectangleCount(wideSimplified) <= rectangleCount(wideOriginal),
		"Line-width expansion erased the primitive-count improvement guard");
	checkTopologyAndDistance(wideOriginal, wideSimplified, options.edgeTolerance);
}

int main() {
	try {
		testOpenStroke();
		testClosedLoop();
		testJunctions();
		testCannyIntegration();
		testCannySimplificationEffect();
		std::cout << "PASS: topology-safe edge simplification\n";
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
