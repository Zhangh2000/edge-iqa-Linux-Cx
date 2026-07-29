#include "iqa/image_io.hpp"

#include <opencv2/imgcodecs.hpp>

namespace iqa {

ImageLoadResult loadImagePair(const std::string& reference_path,
                              const std::string& distorted_path) {
    ImageLoadResult result;

    cv::Mat reference = cv::imread(reference_path, cv::IMREAD_COLOR);
    if (reference.empty()) {
        result.error = "failed to read reference image";
        return result;
    }

    cv::Mat distorted = cv::imread(distorted_path, cv::IMREAD_COLOR);
    if (distorted.empty()) {
        result.error = "failed to read distorted image";
        return result;
    }

    if (reference.size() != distorted.size()) {
        result.error = "image sizes are different";
        return result;
    }

    if (reference.channels() != distorted.channels()) {
        result.error = "image channel counts are different";
        return result;
    }

    result.ok = true;
    result.pair.reference = reference;
    result.pair.distorted = distorted;
    result.pair.width = reference.cols;
    result.pair.height = reference.rows;
    result.pair.channels = reference.channels();
    return result;
}

}  // namespace iqa
