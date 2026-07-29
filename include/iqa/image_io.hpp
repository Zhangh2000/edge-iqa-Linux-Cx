#pragma once

#include <opencv2/core.hpp>

#include <string>

namespace iqa {

struct ImagePair {
    cv::Mat reference;
    cv::Mat distorted;
    int width = 0;
    int height = 0;
    int channels = 0;
};

struct ImageLoadResult {
    bool ok = false;
    std::string error;
    ImagePair pair;
};

ImageLoadResult loadImagePair(const std::string& reference_path,
                              const std::string& distorted_path);

}  // namespace iqa
