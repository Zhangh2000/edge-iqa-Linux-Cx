#pragma once

#include <opencv2/core/mat.hpp>

namespace iqa {

struct FrameMetrics {
    double brightness_mean = 0.0;
    double sharpness_laplacian_variance = 0.0;
};

FrameMetrics computeFrameMetrics(const cv::Mat& frame);

}  // namespace iqa
