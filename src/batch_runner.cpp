#include "iqa/batch_runner.hpp"

#include "iqa/csv_writer.hpp"
#include "iqa/image_io.hpp"
#include "iqa/metrics.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace iqa {
namespace {

using Clock = std::chrono::steady_clock;

std::string trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return "";
    }
    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool in_quotes = false;

    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (ch == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current.push_back('"');
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (ch == ',' && !in_quotes) {
            fields.push_back(trim(current));
            current.clear();
        } else {
            current.push_back(ch);
        }
    }

    fields.push_back(trim(current));
    return fields;
}

std::unordered_map<std::string, std::size_t> buildHeaderIndex(
    const std::vector<std::string>& header) {
    std::unordered_map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < header.size(); ++i) {
        index[header[i]] = i;
    }
    return index;
}

std::string getField(const std::vector<std::string>& row,
                     const std::unordered_map<std::string, std::size_t>& header,
                     const std::string& key) {
    const auto it = header.find(key);
    if (it == header.end() || it->second >= row.size()) {
        return "";
    }
    return row[it->second];
}

std::string resolvePath(const std::filesystem::path& manifest_dir,
                        const std::string& path_text) {
    const std::filesystem::path path(path_text);
    if (path.is_absolute()) {
        return path.string();
    }
    return (manifest_dir / path).lexically_normal().string();
}

double elapsedMilliseconds(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void requireColumns(const std::unordered_map<std::string, std::size_t>& header) {
    if (header.find("case_id") == header.end() ||
        header.find("ref_path") == header.end() ||
        header.find("dist_path") == header.end()) {
        throw std::runtime_error("manifest must contain case_id,ref_path,dist_path columns");
    }
}

}  // namespace

BatchSummary runBatch(const BatchOptions& options) {
    std::ifstream manifest(options.manifest_path);
    if (!manifest) {
        throw std::runtime_error("failed to open manifest csv: " + options.manifest_path);
    }

    CsvWriter writer(options.output_path);
    if (!writer.ok()) {
        throw std::runtime_error(writer.error());
    }
    writer.writeHeader();

    std::string line;
    std::vector<std::string> header;
    while (std::getline(manifest, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }
        header = parseCsvLine(trimmed);
        break;
    }

    if (header.empty()) {
        throw std::runtime_error("manifest csv has no header");
    }

    const auto header_index = buildHeaderIndex(header);
    requireColumns(header_index);

    const std::filesystem::path manifest_dir =
        std::filesystem::path(options.manifest_path).parent_path();

    BatchSummary summary;
    while (std::getline(manifest, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const std::vector<std::string> row = parseCsvLine(trimmed);
        CsvResultRow output_row;
        output_row.case_id = getField(row, header_index, "case_id");
        output_row.reference_path = getField(row, header_index, "ref_path");
        output_row.distorted_path = getField(row, header_index, "dist_path");
        output_row.distortion_type = getField(row, header_index, "distortion_type");

        ++summary.total;
        const auto start = Clock::now();
        const std::string reference_path = resolvePath(manifest_dir, output_row.reference_path);
        const std::string distorted_path = resolvePath(manifest_dir, output_row.distorted_path);

        try {
            ImageLoadResult loaded = loadImagePair(reference_path, distorted_path);
            if (!loaded.ok) {
                output_row.status = "failed";
                output_row.error = loaded.error;
                output_row.elapsed_ms = elapsedMilliseconds(start, Clock::now());
                writer.writeRow(output_row);
                ++summary.failed;
                continue;
            }

            const MetricResult metrics =
                computeFullReferenceMetrics(loaded.pair.reference, loaded.pair.distorted);

            output_row.width = loaded.pair.width;
            output_row.height = loaded.pair.height;
            output_row.channels = loaded.pair.channels;
            output_row.mse_standard = metrics.mse_standard;
            output_row.psnr_db = metrics.psnr_db;
            output_row.ssim = metrics.ssim;
            output_row.elapsed_ms = elapsedMilliseconds(start, Clock::now());
            output_row.status = "ok";
            writer.writeRow(output_row);
            ++summary.succeeded;
        } catch (const std::exception& ex) {
            output_row.status = "failed";
            output_row.error = ex.what();
            output_row.elapsed_ms = elapsedMilliseconds(start, Clock::now());
            writer.writeRow(output_row);
            ++summary.failed;
        }
    }

    return summary;
}

}  // namespace iqa
