#include "iqa/metrics.hpp"

#include <opencv2/imgproc.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace iqa {
namespace {

void validateComparableImages(const cv::Mat& reference, const cv::Mat& distorted) {
    if (reference.empty() || distorted.empty()) {
        throw std::invalid_argument("input image is empty");
    }
    if (reference.size() != distorted.size()) {
        throw std::invalid_argument("image sizes are different");
    }
    if (reference.channels() != distorted.channels()) {
        throw std::invalid_argument("image channel counts are different");
    }
}

double sumSquaredError(const cv::Mat& reference, const cv::Mat& distorted) {
    cv::Mat reference64;
    cv::Mat distorted64;
    reference.convertTo(reference64, CV_64F);
    distorted.convertTo(distorted64, CV_64F);

    cv::Mat diff = reference64 - distorted64;
    cv::Mat squared = diff.mul(diff);
    const cv::Scalar channel_sums = cv::sum(squared);

    double total = 0.0;
    for (int i = 0; i < reference.channels(); ++i) {
        total += channel_sums[i];
    }
    return total;
}

cv::Mat toGray64(const cv::Mat& image) {
    cv::Mat gray;
    if (image.channels() == 1) {
        gray = image;
    } else if (image.channels() == 3) {
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    } else if (image.channels() == 4) {
        cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
    } else {
        throw std::invalid_argument("unsupported channel count for SSIM");
    }

    cv::Mat gray64;
    gray.convertTo(gray64, CV_64F);
    return gray64;
}

}  // namespace

double computeMSEStandard(const cv::Mat& reference, const cv::Mat& distorted) {
    validateComparableImages(reference, distorted);
    const double denominator =
        static_cast<double>(reference.total()) * static_cast<double>(reference.channels());
    return sumSquaredError(reference, distorted) / denominator;
}

double computePSNR(double mse, double max_pixel_value) {
    if (mse <= std::numeric_limits<double>::epsilon()) {
        return std::numeric_limits<double>::infinity();
    }
    return 10.0 * std::log10((max_pixel_value * max_pixel_value) / mse);
}

double computeSSIMGrayscale(const cv::Mat& reference, const cv::Mat& distorted) {
    validateComparableImages(reference, distorted);

    const cv::Mat reference64 = toGray64(reference);
    const cv::Mat distorted64 = toGray64(distorted);

    const double c1 = std::pow(0.01 * 255.0, 2.0);
    const double c2 = std::pow(0.03 * 255.0, 2.0);
    const cv::Size window_size(11, 11);
    const double sigma = 1.5;

    cv::Mat mu1;
    cv::Mat mu2;
    cv::GaussianBlur(reference64, mu1, window_size, sigma);
    cv::GaussianBlur(distorted64, mu2, window_size, sigma);

    const cv::Mat mu1_sq = mu1.mul(mu1);
    const cv::Mat mu2_sq = mu2.mul(mu2);
    const cv::Mat mu1_mu2 = mu1.mul(mu2);

    cv::Mat reference_sq;
    cv::Mat distorted_sq;
    cv::Mat reference_distorted;
    cv::GaussianBlur(reference64.mul(reference64), reference_sq, window_size, sigma);
    cv::GaussianBlur(distorted64.mul(distorted64), distorted_sq, window_size, sigma);
    cv::GaussianBlur(reference64.mul(distorted64), reference_distorted, window_size, sigma);

    const cv::Mat sigma1_sq = reference_sq - mu1_sq;
    const cv::Mat sigma2_sq = distorted_sq - mu2_sq;
    const cv::Mat sigma12 = reference_distorted - mu1_mu2;

    const cv::Mat luminance = 2.0 * mu1_mu2 + c1;
    const cv::Mat contrast_structure = 2.0 * sigma12 + c2;
    const cv::Mat numerator = luminance.mul(contrast_structure);

    const cv::Mat luminance_denominator = mu1_sq + mu2_sq + c1;
    const cv::Mat contrast_structure_denominator = sigma1_sq + sigma2_sq + c2;
    const cv::Mat denominator = luminance_denominator.mul(contrast_structure_denominator);

    cv::Mat ssim_map;
    cv::divide(numerator, denominator, ssim_map);
    return cv::mean(ssim_map)[0];
}

MetricResult computeFullReferenceMetrics(const cv::Mat& reference, const cv::Mat& distorted) {
    MetricResult result;
    result.mse_standard = computeMSEStandard(reference, distorted);
    result.psnr_db = computePSNR(result.mse_standard);
    result.ssim = computeSSIMGrayscale(reference, distorted);
    return result;
}

}  // namespace iqa
