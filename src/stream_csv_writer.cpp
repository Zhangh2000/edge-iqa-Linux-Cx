#include "iqa/stream_csv_writer.hpp"

#include "iqa/csv_writer.hpp"

#include <exception>
#include <filesystem>
#include <iomanip>

namespace iqa {
namespace {

void ensureParentDirectory(const std::string& output_path) {
    const std::filesystem::path path(output_path);
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

}  // namespace

StreamCsvWriter::StreamCsvWriter(const std::string& output_path) {
    try {
        ensureParentDirectory(output_path);
        output_.open(output_path);
        if (!output_) {
            error_ = "failed to open stream output csv";
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

bool StreamCsvWriter::ok() const {
    return output_.is_open() && output_.good() && error_.empty();
}

const std::string& StreamCsvWriter::error() const {
    return error_;
}

void StreamCsvWriter::writeHeader() {
    output_ << "source,frame_index,source_time_ms,brightness_mean,"
            << "sharpness_laplacian_variance,processing_latency_ms,smoothed_fps,"
            << "status,reason\n";
    if (!output_) {
        error_ = "failed to write stream csv header";
    }
}

void StreamCsvWriter::writeRow(const StreamCsvRow& row) {
    output_ << escapeCsvField(row.source) << ','
            << row.frame_index << ','
            << std::fixed << std::setprecision(6)
            << row.source_time_ms << ','
            << row.brightness_mean << ','
            << row.sharpness_laplacian_variance << ','
            << row.processing_latency_ms << ','
            << row.smoothed_fps << ','
            << escapeCsvField(row.status) << ','
            << escapeCsvField(row.reason) << '\n';
    if (!output_) {
        error_ = "failed to write stream csv row";
    }
}

}  // namespace iqa
