#pragma once

#include <fstream>
#include <string>

namespace iqa {

struct StreamCsvRow {
    std::string source;
    long long frame_index = 0;
    double source_time_ms = 0.0;
    double brightness_mean = 0.0;
    double sharpness_laplacian_variance = 0.0;
    double processing_latency_ms = 0.0;
    double smoothed_fps = 0.0;
    std::string status;
    std::string reason;
};

class StreamCsvWriter {
public:
    explicit StreamCsvWriter(const std::string& output_path);

    bool ok() const;
    const std::string& error() const;
    void writeHeader();
    void writeRow(const StreamCsvRow& row);

private:
    std::ofstream output_;
    std::string error_;
};

}  // namespace iqa
