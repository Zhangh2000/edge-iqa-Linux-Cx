#pragma once

#include <fstream>
#include <string>

namespace iqa {

struct CsvResultRow {
    std::string case_id;
    std::string reference_path;
    std::string distorted_path;
    std::string distortion_type;
    int width = 0;
    int height = 0;
    int channels = 0;
    double mse_standard = 0.0;
    double psnr_db = 0.0;
    double ssim = 0.0;
    double elapsed_ms = 0.0;
    std::string status;
    std::string error;
};

class CsvWriter {
public:
    explicit CsvWriter(const std::string& output_path);

    bool ok() const;

    const std::string& error() const;

    void writeHeader();

    void writeRow(const CsvResultRow& row);

private:
    std::ofstream output_;
    std::string error_;
};

std::string escapeCsvField(const std::string& value);

}  // namespace iqa
