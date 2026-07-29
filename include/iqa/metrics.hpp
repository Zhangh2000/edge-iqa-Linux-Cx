#pragma once

#include <opencv2/core.hpp>

namespace iqa {

struct MetricResult {
    double mse_standard = 0.0;
    double psnr_db = 0.0;
    double ssim = 0.0;
};

double computeMSEStandard(const cv::Mat& reference, const cv::Mat& distorted);

double computePSNR(double mse, double max_pixel_value = 255.0);

double computeSSIMGrayscale(const cv::Mat& reference, const cv::Mat& distorted);

MetricResult computeFullReferenceMetrics(const cv::Mat& reference, const cv::Mat& distorted);

}  // namespace iqa
