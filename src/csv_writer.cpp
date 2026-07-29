#include "iqa/csv_writer.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>

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

CsvWriter::CsvWriter(const std::string& output_path) {
    try {
        ensureParentDirectory(output_path);
        output_.open(output_path);
        if (!output_) {
            error_ = "failed to open output csv";
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

bool CsvWriter::ok() const {
    return output_.is_open() && output_.good() && error_.empty();
}

const std::string& CsvWriter::error() const {
    return error_;
}

void CsvWriter::writeHeader() {
    output_ << "case_id,reference_path,distorted_path,distortion_type,"
            << "width,height,channels,mse_standard,psnr_db,ssim,elapsed_ms,status,error\n";
}

void CsvWriter::writeRow(const CsvResultRow& row) {
    output_ << escapeCsvField(row.case_id) << ','
            << escapeCsvField(row.reference_path) << ','
            << escapeCsvField(row.distorted_path) << ','
            << escapeCsvField(row.distortion_type) << ','
            << row.width << ','
            << row.height << ','
            << row.channels << ','
            << std::fixed << std::setprecision(6)
            << row.mse_standard << ','
            << row.psnr_db << ','
            << row.ssim << ','
            << row.elapsed_ms << ','
            << escapeCsvField(row.status) << ','
            << escapeCsvField(row.error) << '\n';
}

std::string escapeCsvField(const std::string& value) {
    const bool needs_quotes = value.find_first_of(",\"\n\r") != std::string::npos;
    if (!needs_quotes) {
        return value;
    }

    std::ostringstream escaped;
    escaped << '"';
    for (const char ch : value) {
        if (ch == '"') {
            escaped << "\"\"";
        } else {
            escaped << ch;
        }
    }
    escaped << '"';
    return escaped.str();
}

}  // namespace iqa
