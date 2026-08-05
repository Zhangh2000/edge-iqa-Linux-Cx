#include "iqa/frame_metrics.hpp"

#include <opencv2/imgproc.hpp>

#include <stdexcept>

namespace iqa {

FrameMetrics computeFrameMetrics(const cv::Mat& frame) {
    if (frame.empty()) {
        throw std::invalid_argument("cannot compute metrics for an empty frame");
    }
    if (frame.depth() != CV_8U) {
        throw std::invalid_argument("frame metrics require 8-bit image data");
    }

    cv::Mat gray;
    if (frame.channels() == 1) {
        gray = frame;
    } else if (frame.channels() == 3) {
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    } else if (frame.channels() == 4) {
        cv::cvtColor(frame, gray, cv::COLOR_BGRA2GRAY);
    } else {
        throw std::invalid_argument("frame metrics support 1, 3, or 4 channels");
    }

    FrameMetrics result;
    result.brightness_mean = cv::mean(gray)[0];

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F, 3);
    cv::Scalar laplacian_mean;
    cv::Scalar laplacian_stddev;
    cv::meanStdDev(laplacian, laplacian_mean, laplacian_stddev);
    result.sharpness_laplacian_variance = laplacian_stddev[0] * laplacian_stddev[0];
    return result;
}

}  // namespace iqa
