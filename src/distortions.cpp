#include "iqa/distortions.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <stdexcept>
#include <vector>

namespace iqa {
namespace {

void requireImage(const cv::Mat& image) {
    if (image.empty()) {
        throw std::invalid_argument("input image is empty");
    }
    if (image.depth() != CV_8U) {
        throw std::invalid_argument("distortion input must use 8-bit pixels");
    }
}

}  // namespace

cv::Mat addGaussianNoise(const cv::Mat& image,
                         double sigma,
                         std::uint64_t seed) {
    requireImage(image);
    if (sigma <= 0.0) {
        throw std::invalid_argument("Gaussian noise sigma must be greater than zero");
    }

    const int float_type = CV_MAKETYPE(CV_32F, image.channels());
    cv::Mat image_float;
    image.convertTo(image_float, float_type);

    cv::Mat noise(image.size(), float_type);
    cv::RNG rng(seed);
    rng.fill(noise, cv::RNG::NORMAL, 0.0, sigma);

    cv::Mat noisy_float = image_float + noise;
    cv::Mat noisy;
    noisy_float.convertTo(noisy, image.type());
    return noisy;
}

cv::Mat applyGaussianBlur(const cv::Mat& image,
                          int kernel_size,
                          double sigma) {
    requireImage(image);
    if (kernel_size <= 0 || kernel_size % 2 == 0) {
        throw std::invalid_argument("Gaussian blur kernel size must be a positive odd number");
    }
    if (sigma <= 0.0) {
        throw std::invalid_argument("Gaussian blur sigma must be greater than zero");
    }

    cv::Mat blurred;
    cv::GaussianBlur(image,
                     blurred,
                     cv::Size(kernel_size, kernel_size),
                     sigma,
                     sigma,
                     cv::BORDER_REFLECT_101);
    return blurred;
}

std::vector<unsigned char> encodeJpeg(const cv::Mat& image, int quality) {
    requireImage(image);
    if (quality < 0 || quality > 100) {
        throw std::invalid_argument("JPEG quality must be in the range [0, 100]");
    }

    std::vector<unsigned char> encoded;
    const std::vector<int> parameters = {cv::IMWRITE_JPEG_QUALITY, quality};
    if (!cv::imencode(".jpg", image, encoded, parameters)) {
        throw std::runtime_error("failed to encode JPEG image");
    }
    return encoded;
}

}  // namespace iqa
