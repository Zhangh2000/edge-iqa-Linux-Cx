#include "iqa/video_source.hpp"

#include <cmath>
#include <sstream>

namespace iqa {
namespace {

int positivePropertyAsInt(cv::VideoCapture& capture, int property) {
    const double value = capture.get(property);
    if (!std::isfinite(value) || value <= 0.0) {
        return 0;
    }
    return static_cast<int>(std::lround(value));
}

long long positivePropertyAsLongLong(cv::VideoCapture& capture, int property) {
    const double value = capture.get(property);
    if (!std::isfinite(value) || value <= 0.0) {
        return 0;
    }
    return static_cast<long long>(std::llround(value));
}

}  // namespace

bool VideoSource::open(const VideoSourceConfig& config) {
    capture_.release();
    info_ = {};
    error_.clear();

    if (config.kind == VideoSourceKind::File) {
        if (config.file_path.empty()) {
            error_ = "video file path is empty";
            return false;
        }
        info_.label = config.file_path;
        if (!capture_.open(config.file_path, cv::CAP_ANY)) {
            error_ = "failed to open video file: " + config.file_path;
            return false;
        }
    } else {
        if (config.camera_index < 0) {
            error_ = "camera index must be non-negative";
            return false;
        }
        std::ostringstream label;
        label << "camera:" << config.camera_index;
        info_.label = label.str();
        if (!capture_.open(config.camera_index, cv::CAP_ANY)) {
            error_ = "failed to open camera index: " + std::to_string(config.camera_index);
            return false;
        }
    }

    info_.width = positivePropertyAsInt(capture_, cv::CAP_PROP_FRAME_WIDTH);
    info_.height = positivePropertyAsInt(capture_, cv::CAP_PROP_FRAME_HEIGHT);
    info_.backend_name = capture_.getBackendName();

    const double fps = capture_.get(cv::CAP_PROP_FPS);
    info_.reported_fps = std::isfinite(fps) && fps > 0.0 ? fps : 0.0;
    info_.reported_frame_count =
        positivePropertyAsLongLong(capture_, cv::CAP_PROP_FRAME_COUNT);
    return true;
}

bool VideoSource::read(cv::Mat& frame) {
    if (!capture_.isOpened()) {
        error_ = "video source is not open";
        return false;
    }

    if (!capture_.read(frame) || frame.empty()) {
        return false;
    }
    return true;
}

bool VideoSource::isOpened() const {
    return capture_.isOpened();
}

double VideoSource::positionMilliseconds() const {
    if (!capture_.isOpened()) {
        return 0.0;
    }
    const double position = capture_.get(cv::CAP_PROP_POS_MSEC);
    return std::isfinite(position) && position >= 0.0 ? position : 0.0;
}

const VideoSourceInfo& VideoSource::info() const {
    return info_;
}

const std::string& VideoSource::error() const {
    return error_;
}

}  // namespace iqa
