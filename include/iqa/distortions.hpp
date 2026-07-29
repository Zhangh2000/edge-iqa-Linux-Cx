#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace iqa {

cv::Mat addGaussianNoise(const cv::Mat& image,
                         double sigma,
                         std::uint64_t seed);

cv::Mat applyGaussianBlur(const cv::Mat& image,
                          int kernel_size,
                          double sigma);

std::vector<unsigned char> encodeJpeg(const cv::Mat& image, int quality);

}  // namespace iqa
