#pragma once
#include <opencv2/core.hpp>
#include <array>
#include <string_view>

namespace binarizer {
enum class Method { Fixed, Adaptive, Otsu, Triangle, Li, Sauvola, Wolf, Bernsen };
inline constexpr std::array methods {Method::Fixed, Method::Adaptive, Method::Otsu,
	Method::Triangle, Method::Li, Method::Sauvola, Method::Wolf, Method::Bernsen};
std::string_view name(Method method);
struct Options {
	Method method = Method::Adaptive;
	int threshold = 127;
	int blockSize = 31;
	double adaptiveC = 5;
	double localK = 0.2;
	int denoiseMethod = 0;
	int denoiseStrength = 0;
	int exposure = 0;
	int contrast = 0;
	double gamma = 1;
	int smooth = 0;
	int sharpen = 0;
	bool equalize = false;
	bool clahe = false;
	int detail = 0;
	int edge = 0;
	int localContrast = 0;
	bool extractEdges = false;
	int edgeLowThreshold = 50;
	int edgeHighThreshold = 150;
	int edgeWidth = 2;
	bool simplifyEdges = false;
	double edgeTolerance = 1.0; // Source-image pixels, also scaled for comparison previews.
	bool invert = false;
	bool flipHorizontal = false;
	bool flipVertical = false;
	// Zero retains the original resolution. Previews pass a separate size limit.
	int maxDimension = 0;
};
// Core has no Qt, Python or UI dependency. Input is uint8 gray/BGR/BGRA.
cv::Mat preprocess(const cv::Mat& source, const Options& options);
cv::Mat threshold(const cv::Mat& gray, const Options& options, double* thresholdValue = nullptr);
cv::Mat edgeMask(const cv::Mat& gray, const cv::Mat& source, const Options& options);
// An empty mask leaves the binary image unchanged. A nonempty mask is black-overlaid.
cv::Mat overlayEdges(const cv::Mat& binary, const cv::Mat& edges);
cv::Mat finish(const cv::Mat& binary, const cv::Mat& gray, const cv::Mat& source, const Options& options);
cv::Mat process(const cv::Mat& source, const Options& options);
}
