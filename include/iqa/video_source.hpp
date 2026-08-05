#pragma once

#include <opencv2/core/mat.hpp>
#include <opencv2/videoio.hpp>

#include <string>

namespace iqa {

enum class VideoSourceKind {
    File,
    Camera,
};

struct VideoSourceConfig {
    VideoSourceKind kind = VideoSourceKind::File;
    std::string file_path;
    int camera_index = 0;
};

struct VideoSourceInfo {
    std::string label;
    std::string backend_name;
    int width = 0;
    int height = 0;
    double reported_fps = 0.0;
    long long reported_frame_count = 0;
};

class VideoSource {
public:
    bool open(const VideoSourceConfig& config);
    bool read(cv::Mat& frame);
    bool isOpened() const;
    double positionMilliseconds() const;

    const VideoSourceInfo& info() const;
    const std::string& error() const;

private:
    cv::VideoCapture capture_;
    VideoSourceInfo info_;
    std::string error_;
};

}  // namespace iqa
