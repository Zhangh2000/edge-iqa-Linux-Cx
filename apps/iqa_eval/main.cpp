#include "iqa/batch_runner.hpp"
#include "iqa/csv_writer.hpp"
#include "iqa/image_io.hpp"
#include "iqa/metrics.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct CliOptions {
    std::string reference_path;
    std::string distorted_path;
    std::string batch_path;
    std::string output_path;
    bool help = false;
};

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  iqa_eval --ref <reference_image> --dist <distorted_image> [--out <result.csv>]\n"
        << "  iqa_eval --batch <pairs.csv> --out <result.csv>\n\n"
        << "Metrics:\n"
        << "  mse_standard       sum(error^2) / (width * height * channels)\n"
        << "  psnr_db            10 * log10(255^2 / mse_standard)\n"
        << "  ssim               grayscale SSIM with 11x11 Gaussian window\n";
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;
    const std::vector<std::string> args(argv + 1, argv + argc);

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto needValue = [&](const std::string& flag) -> std::string {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing value for " + flag);
            }
            return args[++i];
        };

        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--ref") {
            options.reference_path = needValue(arg);
        } else if (arg == "--dist") {
            options.distorted_path = needValue(arg);
        } else if (arg == "--batch") {
            options.batch_path = needValue(arg);
        } else if (arg == "--out") {
            options.output_path = needValue(arg);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    return options;
}

double elapsedMilliseconds(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int runSingleImageMode(const CliOptions& options) {
    if (options.reference_path.empty() || options.distorted_path.empty()) {
        throw std::runtime_error("single image mode requires --ref and --dist");
    }

    const auto start = Clock::now();
    const iqa::ImageLoadResult loaded =
        iqa::loadImagePair(options.reference_path, options.distorted_path);
    if (!loaded.ok) {
        throw std::runtime_error(loaded.error);
    }

    const iqa::MetricResult metrics =
        iqa::computeFullReferenceMetrics(loaded.pair.reference, loaded.pair.distorted);
    const double elapsed_ms = elapsedMilliseconds(start, Clock::now());

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "reference: " << options.reference_path << '\n';
    std::cout << "distorted: " << options.distorted_path << '\n';
    std::cout << "width: " << loaded.pair.width << '\n';
    std::cout << "height: " << loaded.pair.height << '\n';
    std::cout << "channels: " << loaded.pair.channels << '\n';
    std::cout << "mse_standard: " << metrics.mse_standard << '\n';
    std::cout << "psnr_db: " << metrics.psnr_db << '\n';
    std::cout << "ssim: " << metrics.ssim << '\n';
    std::cout << "elapsed_ms: " << elapsed_ms << '\n';

    if (!options.output_path.empty()) {
        iqa::CsvWriter writer(options.output_path);
        if (!writer.ok()) {
            throw std::runtime_error(writer.error());
        }
        writer.writeHeader();

        iqa::CsvResultRow row;
        row.case_id = "single";
        row.reference_path = options.reference_path;
        row.distorted_path = options.distorted_path;
        row.width = loaded.pair.width;
        row.height = loaded.pair.height;
        row.channels = loaded.pair.channels;
        row.mse_standard = metrics.mse_standard;
        row.psnr_db = metrics.psnr_db;
        row.ssim = metrics.ssim;
        row.elapsed_ms = elapsed_ms;
        row.status = "ok";
        writer.writeRow(row);
    }

    return 0;
}

int runBatchMode(const CliOptions& options) {
    if (options.batch_path.empty()) {
        throw std::runtime_error("batch mode requires --batch");
    }
    if (options.output_path.empty()) {
        throw std::runtime_error("batch mode requires --out");
    }

    const iqa::BatchSummary summary =
        iqa::runBatch({options.batch_path, options.output_path});

    std::cout << "batch total: " << summary.total << '\n';
    std::cout << "batch succeeded: " << summary.succeeded << '\n';
    std::cout << "batch failed: " << summary.failed << '\n';
    std::cout << "output: " << options.output_path << '\n';
    return summary.failed == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parseArgs(argc, argv);
        if (options.help || argc == 1) {
            printUsage();
            return 0;
        }

        if (!options.batch_path.empty()) {
            return runBatchMode(options);
        }

        return runSingleImageMode(options);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        std::cerr << "run with --help for usage\n";
        return 1;
    }
}
