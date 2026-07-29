#include "iqa/csv_writer.hpp"
#include "iqa/distortions.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr std::uintmax_t kLargeImagePixelLimit = 25'000'000;

struct RoiOptions {
    bool enabled = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct CliOptions {
    fs::path input_path = "data/raw";
    fs::path reference_dir = "data/reference";
    fs::path output_dir = "data/distorted";
    fs::path manifest_path = "data/manifests/pairs.csv";
    std::uint64_t seed = 20260723;
    RoiOptions roi;
    bool full_resolution = false;
    bool help = false;
};

struct ManifestRow {
    std::string case_id;
    fs::path reference_path;
    fs::path distorted_path;
    std::string distortion_type;
    int distortion_level = 0;
    std::string parameter;
    std::string random_seed;
};

struct BlurSetting {
    int kernel_size;
    double sigma;
    const char* tag;
};

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  iqa_generate [options]\n\n"
        << "Options:\n"
        << "  --input <file_or_dir>     Raw image or recursively scanned directory\n"
        << "                            (default: data/raw)\n"
        << "  --reference-dir <dir>     Prepared lossless reference PNG directory\n"
        << "                            (default: data/reference)\n"
        << "  --output-dir <dir>        Distorted image directory\n"
        << "                            (default: data/distorted)\n"
        << "  --manifest <csv>          Generated pair manifest\n"
        << "                            (default: data/manifests/pairs.csv)\n"
        << "  --seed <integer>          Base seed for deterministic noise\n"
        << "                            (default: 20260723)\n"
        << "  --roi <x> <y> <w> <h>    Use the same fixed ROI for every source image\n"
        << "  --full-resolution         Explicitly allow images above 25 megapixels\n"
        << "  --help, -h                Show this help\n\n"
        << "Generated distortions per source image:\n"
        << "  Gaussian noise: sigma=5, 15, 30 (PNG)\n"
        << "  Gaussian blur:  (kernel,sigma)=(3,0.8), (7,1.5), (11,3.0) (PNG)\n"
        << "  JPEG:           quality=80, 50, 20 (JPG)\n";
}

long long parseSignedInteger(const std::string& text, const std::string& flag) {
    std::size_t consumed = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + flag + ": " + text);
    }
    if (consumed != text.size()) {
        throw std::runtime_error("invalid integer for " + flag + ": " + text);
    }
    return value;
}

std::uint64_t parseSeed(const std::string& text) {
    if (!text.empty() && text.front() == '-') {
        throw std::runtime_error("--seed must not be negative");
    }
    std::size_t consumed = 0;
    unsigned long long value = 0;
    try {
        value = std::stoull(text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for --seed: " + text);
    }
    if (consumed != text.size()) {
        throw std::runtime_error("invalid integer for --seed: " + text);
    }
    return static_cast<std::uint64_t>(value);
}

int parseRoiInteger(const std::string& text, const std::string& field) {
    const long long value = parseSignedInteger(text, "--roi " + field);
    if (value < 0 || value > static_cast<long long>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("--roi " + field + " is outside the supported range");
    }
    return static_cast<int>(value);
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
        } else if (arg == "--input") {
            options.input_path = needValue(arg);
        } else if (arg == "--reference-dir") {
            options.reference_dir = needValue(arg);
        } else if (arg == "--output-dir") {
            options.output_dir = needValue(arg);
        } else if (arg == "--manifest") {
            options.manifest_path = needValue(arg);
        } else if (arg == "--seed") {
            options.seed = parseSeed(needValue(arg));
        } else if (arg == "--roi") {
            if (i + 4 >= args.size()) {
                throw std::runtime_error("--roi requires x y width height");
            }
            options.roi.enabled = true;
            options.roi.x = parseRoiInteger(args[++i], "x");
            options.roi.y = parseRoiInteger(args[++i], "y");
            options.roi.width = parseRoiInteger(args[++i], "width");
            options.roi.height = parseRoiInteger(args[++i], "height");
        } else if (arg == "--full-resolution") {
            options.full_resolution = true;
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    if (options.roi.enabled && options.full_resolution) {
        throw std::runtime_error("--roi and --full-resolution cannot be used together");
    }
    if (options.roi.enabled && (options.roi.width <= 0 || options.roi.height <= 0)) {
        throw std::runtime_error("--roi width and height must be greater than zero");
    }
    return options;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool isSupportedImage(const fs::path& path) {
    const std::string extension = lowercase(path.extension().string());
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
           extension == ".bmp" || extension == ".tif" || extension == ".tiff" ||
           extension == ".webp";
}

std::vector<fs::path> collectInputImages(const fs::path& input_path) {
    std::error_code error;
    std::vector<fs::path> images;

    if (fs::is_regular_file(input_path, error)) {
        if (!isSupportedImage(input_path)) {
            throw std::runtime_error("input file does not have a supported image extension: " +
                                     input_path.string());
        }
        images.push_back(input_path);
    } else if (fs::is_directory(input_path, error)) {
        const auto options = fs::directory_options::skip_permission_denied;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(input_path, options)) {
            if (entry.is_regular_file(error) && isSupportedImage(entry.path())) {
                images.push_back(entry.path());
            }
            error.clear();
        }
    } else {
        throw std::runtime_error("input path is not a readable file or directory: " +
                                 input_path.string());
    }

    std::sort(images.begin(), images.end(), [](const fs::path& left, const fs::path& right) {
        return left.generic_string() < right.generic_string();
    });
    if (images.empty()) {
        throw std::runtime_error("no supported images found under: " + input_path.string());
    }
    return images;
}

bool isPathInside(const fs::path& child, const fs::path& parent) {
    const fs::path absolute_child = fs::absolute(child).lexically_normal();
    const fs::path absolute_parent = fs::absolute(parent).lexically_normal();
    auto child_it = absolute_child.begin();
    for (auto parent_it = absolute_parent.begin(); parent_it != absolute_parent.end(); ++parent_it) {
        if (child_it == absolute_child.end() || *child_it != *parent_it) {
            return false;
        }
        ++child_it;
    }
    return true;
}

void validateDirectoryLayout(const CliOptions& options) {
    std::error_code error;
    if (!fs::is_directory(options.input_path, error)) {
        return;
    }
    if (isPathInside(options.reference_dir, options.input_path) ||
        isPathInside(options.output_dir, options.input_path)) {
        throw std::runtime_error(
            "reference and distorted output directories must not be inside the scanned input directory");
    }
}

fs::path relativeSourcePath(const fs::path& source, const fs::path& input_path) {
    std::error_code error;
    if (fs::is_directory(input_path, error)) {
        fs::path relative = fs::relative(source, input_path, error);
        if (!error) {
            return relative;
        }
    }
    return source.filename();
}

cv::Mat prepareReference(const cv::Mat& source,
                         const fs::path& source_path,
                         const CliOptions& options) {
    if (options.roi.enabled) {
        const long long right = static_cast<long long>(options.roi.x) + options.roi.width;
        const long long bottom = static_cast<long long>(options.roi.y) + options.roi.height;
        if (right > source.cols || bottom > source.rows) {
            std::ostringstream message;
            message << "ROI (" << options.roi.x << ',' << options.roi.y << ','
                    << options.roi.width << ',' << options.roi.height
                    << ") exceeds image size " << source.cols << 'x' << source.rows
                    << ": " << source_path;
            throw std::runtime_error(message.str());
        }
        return source(cv::Rect(options.roi.x,
                               options.roi.y,
                               options.roi.width,
                               options.roi.height))
            .clone();
    }

    if (source.total() > kLargeImagePixelLimit && !options.full_resolution) {
        std::ostringstream message;
        message << "image has " << source.total()
                << " pixels; use --roi x y width height (recommended) or explicitly pass "
                   "--full-resolution: "
                << source_path;
        throw std::runtime_error(message.str());
    }
    return source;
}

void writePng(const fs::path& path, const cv::Mat& image) {
    fs::create_directories(path.parent_path());
    const std::vector<int> parameters = {cv::IMWRITE_PNG_COMPRESSION, 3};
    if (!cv::imwrite(path.string(), image, parameters)) {
        throw std::runtime_error("failed to write PNG image: " + path.string());
    }
}

void writeBinaryFile(const fs::path& path, const std::vector<unsigned char>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open binary output: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed to write binary output: " + path.string());
    }
}

std::uint64_t deterministicSeed(std::uint64_t base_seed,
                                const std::string& source_key,
                                int level) {
    std::uint64_t hash = 1469598103934665603ULL ^ base_seed;
    for (const unsigned char ch : source_key) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(level);
    hash *= 1099511628211ULL;
    return hash == 0 ? 1 : hash;
}

std::string makeImageId(std::size_t index) {
    std::ostringstream id;
    id << "image_" << std::setw(4) << std::setfill('0') << index + 1;
    return id.str();
}

std::string manifestRelativePath(const fs::path& path, const fs::path& manifest_path) {
    const fs::path manifest_dir = manifest_path.parent_path().empty()
                                      ? fs::current_path()
                                      : manifest_path.parent_path();
    std::error_code error;
    const fs::path relative = fs::relative(fs::absolute(path), fs::absolute(manifest_dir), error);
    return error ? fs::absolute(path).generic_string() : relative.generic_string();
}

void writeManifest(const fs::path& manifest_path, const std::vector<ManifestRow>& rows) {
    if (!manifest_path.parent_path().empty()) {
        fs::create_directories(manifest_path.parent_path());
    }

    std::ofstream output(manifest_path);
    if (!output) {
        throw std::runtime_error("failed to open manifest output: " + manifest_path.string());
    }

    output << "case_id,ref_path,dist_path,distortion_type,distortion_level,parameter,random_seed\n";
    for (const ManifestRow& row : rows) {
        output << iqa::escapeCsvField(row.case_id) << ','
               << iqa::escapeCsvField(manifestRelativePath(row.reference_path, manifest_path)) << ','
               << iqa::escapeCsvField(manifestRelativePath(row.distorted_path, manifest_path)) << ','
               << iqa::escapeCsvField(row.distortion_type) << ','
               << row.distortion_level << ','
               << iqa::escapeCsvField(row.parameter) << ','
               << iqa::escapeCsvField(row.random_seed) << '\n';
    }
    if (!output) {
        throw std::runtime_error("failed to write manifest: " + manifest_path.string());
    }
}

void addManifestRow(std::vector<ManifestRow>& rows,
                    const std::string& case_id,
                    const fs::path& reference_path,
                    const fs::path& distorted_path,
                    const std::string& distortion_type,
                    int level,
                    const std::string& parameter,
                    const std::string& random_seed = "") {
    rows.push_back({case_id,
                    reference_path,
                    distorted_path,
                    distortion_type,
                    level,
                    parameter,
                    random_seed});
}

int runGenerator(const CliOptions& options) {
    validateDirectoryLayout(options);
    const std::vector<fs::path> sources = collectInputImages(options.input_path);
    std::vector<ManifestRow> manifest_rows;
    manifest_rows.reserve(sources.size() * 9);

    const std::array<int, 3> noise_sigmas = {5, 15, 30};
    const std::array<BlurSetting, 3> blur_settings = {{{3, 0.8, "k03_s0p8"},
                                                       {7, 1.5, "k07_s1p5"},
                                                       {11, 3.0, "k11_s3p0"}}};
    const std::array<int, 3> jpeg_qualities = {80, 50, 20};

    for (std::size_t source_index = 0; source_index < sources.size(); ++source_index) {
        const fs::path& source_path = sources[source_index];
        const fs::path relative_source = relativeSourcePath(source_path, options.input_path);
        const std::string image_id = makeImageId(source_index);

        std::cout << '[' << source_index + 1 << '/' << sources.size() << "] "
                  << source_path << '\n';

        const cv::Mat source = cv::imread(source_path.string(), cv::IMREAD_COLOR);
        if (source.empty()) {
            throw std::runtime_error("failed to read source image: " + source_path.string());
        }
        const cv::Mat reference = prepareReference(source, source_path, options);

        const fs::path relative_parent = relative_source.parent_path();
        const std::string stem = relative_source.stem().string();
        const fs::path reference_path =
            options.reference_dir / relative_parent / (stem + ".png");
        const fs::path image_output_dir = options.output_dir / relative_parent / stem;
        writePng(reference_path, reference);

        for (std::size_t level = 0; level < noise_sigmas.size(); ++level) {
            const int sigma = noise_sigmas[level];
            const std::uint64_t noise_seed =
                deterministicSeed(options.seed, relative_source.generic_string(), level + 1);
            const cv::Mat noisy = iqa::addGaussianNoise(reference, sigma, noise_seed);
            const std::string tag = "noise_sigma_" + std::to_string(sigma);
            const fs::path output_path = image_output_dir / (tag + ".png");
            writePng(output_path, noisy);
            addManifestRow(manifest_rows,
                           image_id + "_" + tag,
                           reference_path,
                           output_path,
                           "gaussian_noise_sigma_" + std::to_string(sigma),
                           static_cast<int>(level + 1),
                           "sigma=" + std::to_string(sigma),
                           std::to_string(noise_seed));
        }

        for (std::size_t level = 0; level < blur_settings.size(); ++level) {
            const BlurSetting& setting = blur_settings[level];
            const cv::Mat blurred =
                iqa::applyGaussianBlur(reference, setting.kernel_size, setting.sigma);
            const std::string tag = "blur_" + std::string(setting.tag);
            const fs::path output_path = image_output_dir / (tag + ".png");
            writePng(output_path, blurred);

            std::ostringstream parameter;
            parameter << "kernel=" << setting.kernel_size << ";sigma=" << setting.sigma;
            addManifestRow(manifest_rows,
                           image_id + "_" + tag,
                           reference_path,
                           output_path,
                           "gaussian_blur_k" + std::to_string(setting.kernel_size) +
                               "_sigma_" + std::to_string(setting.sigma),
                           static_cast<int>(level + 1),
                           parameter.str());
        }

        for (std::size_t level = 0; level < jpeg_qualities.size(); ++level) {
            const int quality = jpeg_qualities[level];
            const std::string tag = "jpeg_quality_" + std::to_string(quality);
            const fs::path output_path = image_output_dir / (tag + ".jpg");
            writeBinaryFile(output_path, iqa::encodeJpeg(reference, quality));
            addManifestRow(manifest_rows,
                           image_id + "_" + tag,
                           reference_path,
                           output_path,
                           "jpeg_quality_" + std::to_string(quality),
                           static_cast<int>(level + 1),
                           "quality=" + std::to_string(quality));
        }
    }

    writeManifest(options.manifest_path, manifest_rows);
    std::cout << "source images: " << sources.size() << '\n';
    std::cout << "distorted images: " << manifest_rows.size() << '\n';
    std::cout << "manifest: " << options.manifest_path << '\n';
    std::cout << "base seed: " << options.seed << '\n';
    if (options.roi.enabled) {
        std::cout << "roi: " << options.roi.x << ',' << options.roi.y << ','
                  << options.roi.width << ',' << options.roi.height << '\n';
    } else {
        std::cout << "roi: full image\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const CliOptions options = parseArgs(argc, argv);
        if (options.help || argc == 1) {
            printUsage();
            return 0;
        }
        return runGenerator(options);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        std::cerr << "run with --help for usage\n";
        return 1;
    }
}
