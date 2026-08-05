#include "iqa/alert.hpp"
#include "iqa/bounded_queue.hpp"
#include "iqa/frame_metrics.hpp"
#include "iqa/stream_csv_writer.hpp"
#include "iqa/video_source.hpp"

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct CliOptions {
    iqa::VideoSourceConfig source;
    bool source_selected = false;
    bool headless = false;
    long long max_frames = 0;
    std::size_t queue_capacity = 4;
    std::string output_csv_path;
    iqa::AlertConfig alert_config;
    bool help = false;
};

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  iqa_stream --video <video_file> [options]\n"
        << "  iqa_stream --camera <index> [options]\n\n"
        << "Options:\n"
        << "  --video <path>       Open a local video file\n"
        << "  --camera <index>     Open a camera such as index 0\n"
        << "  --headless           Disable the display window\n"
        << "  --max-frames <n>     Stop after n frames; 0 means unlimited\n"
        << "  --queue-capacity <n> Bounded frame queue size (default: 4)\n"
        << "  --out <path.csv>     Write one metrics row per processed frame\n"
        << "  --brightness-min <v> Alarm when mean brightness stays below v\n"
        << "  --sharpness-min <v>  Alarm when Laplacian variance stays below v\n"
        << "  --alert-frames <n>   Consecutive violations required (default: 3)\n"
        << "  --recovery-frames <n> Consecutive good frames to recover (default: 3)\n"
        << "  --help, -h           Show this help\n\n"
        << "Window controls:\n"
        << "  q or Esc             Stop the stream\n";
}

long long parseNonNegativeInteger(const std::string& text, const std::string& flag) {
    std::size_t parsed = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + flag + ": " + text);
    }

    if (parsed != text.size() || value < 0) {
        throw std::runtime_error("invalid non-negative integer for " + flag + ": " + text);
    }
    return value;
}

double parseNonNegativeDouble(const std::string& text, const std::string& flag) {
    std::size_t parsed = 0;
    double value = 0.0;
    try {
        value = std::stod(text, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid number for " + flag + ": " + text);
    }

    if (parsed != text.size() || !std::isfinite(value) || value < 0.0) {
        throw std::runtime_error("invalid non-negative number for " + flag + ": " + text);
    }
    return value;
}

int parsePositiveInteger(const std::string& text, const std::string& flag) {
    const long long value = parseNonNegativeInteger(text, flag);
    if (value <= 0 || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("invalid positive integer for " + flag + ": " + text);
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
        } else if (arg == "--video") {
            if (options.source_selected) {
                throw std::runtime_error("choose exactly one of --video or --camera");
            }
            options.source.kind = iqa::VideoSourceKind::File;
            options.source.file_path = needValue(arg);
            options.source_selected = true;
        } else if (arg == "--camera") {
            if (options.source_selected) {
                throw std::runtime_error("choose exactly one of --video or --camera");
            }
            const long long index = parseNonNegativeInteger(needValue(arg), arg);
            if (index > std::numeric_limits<int>::max()) {
                throw std::runtime_error("camera index is too large");
            }
            options.source.kind = iqa::VideoSourceKind::Camera;
            options.source.camera_index = static_cast<int>(index);
            options.source_selected = true;
        } else if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--max-frames") {
            options.max_frames = parseNonNegativeInteger(needValue(arg), arg);
        } else if (arg == "--queue-capacity") {
            options.queue_capacity = static_cast<std::size_t>(
                parsePositiveInteger(needValue(arg), arg));
        } else if (arg == "--out") {
            options.output_csv_path = needValue(arg);
        } else if (arg == "--brightness-min") {
            const double value = parseNonNegativeDouble(needValue(arg), arg);
            if (value > 255.0) {
                throw std::runtime_error("brightness threshold must be within [0, 255]");
            }
            options.alert_config.brightness_min = value;
        } else if (arg == "--sharpness-min") {
            options.alert_config.sharpness_min =
                parseNonNegativeDouble(needValue(arg), arg);
        } else if (arg == "--alert-frames") {
            options.alert_config.trigger_frames = parsePositiveInteger(needValue(arg), arg);
        } else if (arg == "--recovery-frames") {
            options.alert_config.recovery_frames = parsePositiveInteger(needValue(arg), arg);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }

    return options;
}

double elapsedSeconds(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double>(end - start).count();
}

double elapsedMilliseconds(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

class SmoothedFpsMeter {
public:
    double tick(const Clock::time_point& now) {
        if (!initialized_) {
            previous_ = now;
            initialized_ = true;
            return 0.0;
        }

        const double seconds = elapsedSeconds(previous_, now);
        previous_ = now;
        if (seconds <= 0.0) {
            return value_;
        }

        const double instantaneous_fps = 1.0 / seconds;
        if (value_ <= 0.0) {
            value_ = instantaneous_fps;
        } else {
            constexpr double alpha = 0.2;
            value_ = alpha * instantaneous_fps + (1.0 - alpha) * value_;
        }
        return value_;
    }

private:
    Clock::time_point previous_{};
    double value_ = 0.0;
    bool initialized_ = false;
};

struct MetricTotals {
    double brightness = 0.0;
    double sharpness = 0.0;
    double latency_ms = 0.0;

    void add(const iqa::FrameMetrics& metrics, double frame_latency_ms) {
        brightness += metrics.brightness_mean;
        sharpness += metrics.sharpness_laplacian_variance;
        latency_ms += frame_latency_ms;
    }
};

struct FramePacket {
    cv::Mat frame;
    long long source_frame_index = 0;
    double source_time_ms = 0.0;
};

int displayDelayMilliseconds(const iqa::VideoSourceInfo& info) {
    if (info.reported_fps <= 0.0) {
        return 1;
    }
    return std::max(1, static_cast<int>(std::lround(1000.0 / info.reported_fps)));
}

std::string formatValue(double value, int precision) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(precision) << value;
    return output.str();
}

void drawOverlay(cv::Mat& frame,
                 long long frame_number,
                 const iqa::FrameMetrics& metrics,
                 double latency_ms,
                 double smoothed_fps,
                 const iqa::AlertDecision& alert) {
    std::string status_line = "Status: ";
    status_line += iqa::alertStatusName(alert.status);
    if (!alert.reason.empty()) {
        status_line += " (" + alert.reason + ")";
    }
    const std::vector<std::string> lines = {
        "Frame: " + std::to_string(frame_number),
        "FPS: " + formatValue(smoothed_fps, 1) +
            "  Latency: " + formatValue(latency_ms, 2) + " ms",
        "Brightness: " + formatValue(metrics.brightness_mean, 1),
        "Sharpness: " + formatValue(metrics.sharpness_laplacian_variance, 1),
        status_line,
    };

    constexpr int margin = 10;
    constexpr int padding = 10;
    constexpr int line_gap = 6;
    const double font_scale = frame.cols < 640 ? 0.45 : 0.6;
    const int thickness = frame.cols < 640 ? 1 : 2;

    int baseline = 0;
    int line_height = 0;
    int max_width = 0;
    for (const std::string& line : lines) {
        const cv::Size size = cv::getTextSize(
            line, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline);
        line_height = std::max(line_height, size.height + line_gap);
        max_width = std::max(max_width, size.width);
    }

    const int panel_width = std::min(frame.cols - 2 * margin, max_width + 2 * padding);
    const int panel_height = std::min(
        frame.rows - 2 * margin,
        static_cast<int>(lines.size()) * line_height + 2 * padding);
    if (panel_width <= 0 || panel_height <= 0) {
        return;
    }

    cv::rectangle(frame,
                  cv::Rect(margin, margin, panel_width, panel_height),
                  cv::Scalar(0, 0, 0),
                  cv::FILLED);

    int y = margin + padding + line_height - line_gap;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (y >= margin + panel_height) {
            break;
        }
        cv::Scalar color(255, 255, 255);
        if (i == lines.size() - 1) {
            if (alert.status == iqa::AlertStatus::Normal) {
                color = cv::Scalar(80, 220, 80);
            } else if (alert.status == iqa::AlertStatus::Warning) {
                color = cv::Scalar(0, 220, 255);
            } else if (alert.status == iqa::AlertStatus::Alarm) {
                color = cv::Scalar(60, 60, 255);
            }
        }
        cv::putText(frame,
                    lines[i],
                    cv::Point(margin + padding, y),
                    cv::FONT_HERSHEY_SIMPLEX,
                    font_scale,
                    color,
                    thickness,
                    cv::LINE_AA);
        y += line_height;
    }
}

int run(const CliOptions& options) {
    if (!options.source_selected) {
        throw std::runtime_error("one of --video or --camera is required");
    }

    iqa::VideoSource source;
    if (!source.open(options.source)) {
        throw std::runtime_error(source.error());
    }

    const iqa::VideoSourceInfo info = source.info();
    std::cout << "source: " << info.label << '\n';
    std::cout << "backend: " << info.backend_name << '\n';
    std::cout << "width: " << info.width << '\n';
    std::cout << "height: " << info.height << '\n';
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "reported_fps: " << info.reported_fps << '\n';
    std::cout << "reported_frame_count: " << info.reported_frame_count << '\n';
    std::cout << "display: " << (options.headless ? "off" : "on") << '\n';
    std::cout << "alert_enabled: " << (options.alert_config.enabled() ? "yes" : "no")
              << '\n';
    const bool drop_oldest = options.source.kind == iqa::VideoSourceKind::Camera;
    std::cout << "queue_capacity: " << options.queue_capacity << '\n';
    std::cout << "queue_policy: " << (drop_oldest ? "drop-oldest" : "block") << '\n';

    std::unique_ptr<iqa::StreamCsvWriter> csv_writer;
    if (!options.output_csv_path.empty()) {
        csv_writer = std::make_unique<iqa::StreamCsvWriter>(options.output_csv_path);
        if (!csv_writer->ok()) {
            throw std::runtime_error(csv_writer->error());
        }
        csv_writer->writeHeader();
        std::cout << "csv_output: " << options.output_csv_path << '\n';
    }

    const auto start = Clock::now();
    long long frames_processed = 0;
    const int wait_ms = displayDelayMilliseconds(info);
    SmoothedFpsMeter fps_meter;
    MetricTotals totals;
    iqa::AlertStateMachine alert_machine(options.alert_config);
    long long warning_frames = 0;
    long long alarm_frames = 0;
    iqa::BoundedQueue<FramePacket> frame_queue(options.queue_capacity);
    std::atomic<long long> frames_captured{0};
    std::atomic<long long> frames_dropped{0};
    std::exception_ptr producer_error;
    std::mutex producer_error_mutex;

    std::thread producer([&] {
        try {
            cv::Mat captured_frame;
            while (source.read(captured_frame)) {
                FramePacket packet;
                packet.frame = std::move(captured_frame);
                packet.source_frame_index = frames_captured.fetch_add(1) + 1;
                packet.source_time_ms = source.positionMilliseconds();

                bool accepted = false;
                if (drop_oldest) {
                    bool dropped = false;
                    accepted = frame_queue.pushDropOldest(std::move(packet), dropped);
                    if (dropped) {
                        frames_dropped.fetch_add(1);
                    }
                } else {
                    accepted = frame_queue.push(std::move(packet));
                }
                if (!accepted) {
                    break;
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(producer_error_mutex);
            producer_error = std::current_exception();
        }
        frame_queue.close();
    });

    const auto stopProducer = [&] {
        frame_queue.close();
        if (producer.joinable()) {
            producer.join();
        }
    };

    try {
        FramePacket packet;
        while (frame_queue.pop(packet)) {
            ++frames_processed;
            const auto processing_start = Clock::now();
            const iqa::FrameMetrics metrics = iqa::computeFrameMetrics(packet.frame);
            const double latency_ms = elapsedMilliseconds(processing_start, Clock::now());
            const double smoothed_fps = fps_meter.tick(Clock::now());
            const iqa::AlertDecision alert = alert_machine.evaluate(metrics);
            totals.add(metrics, latency_ms);
            if (alert.status == iqa::AlertStatus::Warning) {
                ++warning_frames;
            } else if (alert.status == iqa::AlertStatus::Alarm) {
                ++alarm_frames;
            }

            if (csv_writer) {
                iqa::StreamCsvRow row;
                row.source = info.label;
                row.frame_index = packet.source_frame_index;
                row.source_time_ms = packet.source_time_ms;
                row.brightness_mean = metrics.brightness_mean;
                row.sharpness_laplacian_variance = metrics.sharpness_laplacian_variance;
                row.processing_latency_ms = latency_ms;
                row.smoothed_fps = smoothed_fps;
                row.status = iqa::alertStatusName(alert.status);
                row.reason = alert.reason;
                csv_writer->writeRow(row);
                if (!csv_writer->ok()) {
                    throw std::runtime_error(csv_writer->error());
                }
            }

            if (!options.headless) {
                drawOverlay(packet.frame,
                            packet.source_frame_index,
                            metrics,
                            latency_ms,
                            smoothed_fps,
                            alert);
                cv::imshow("edge-iqa stream", packet.frame);
                const int key = cv::waitKey(wait_ms) & 0xff;
                if (key == 'q' || key == 27) {
                    break;
                }
            }

            if (options.max_frames > 0 && frames_processed >= options.max_frames) {
                frame_queue.close();
                break;
            }
        }
    } catch (...) {
        stopProducer();
        if (!options.headless) {
            cv::destroyAllWindows();
        }
        throw;
    }

    stopProducer();

    if (!options.headless) {
        cv::destroyAllWindows();
    }

    {
        std::lock_guard<std::mutex> lock(producer_error_mutex);
        if (producer_error) {
            std::rethrow_exception(producer_error);
        }
    }

    const double total_seconds = elapsedSeconds(start, Clock::now());
    const double average_fps = total_seconds > 0.0 ? frames_processed / total_seconds : 0.0;
    std::cout << "frames_processed: " << frames_processed << '\n';
    std::cout << "elapsed_seconds: " << total_seconds << '\n';
    std::cout << "average_fps: " << average_fps << '\n';
    std::cout << "frames_captured: " << frames_captured.load() << '\n';
    std::cout << "frames_dropped: " << frames_dropped.load() << '\n';
    std::cout << "max_queue_depth: " << frame_queue.maxObservedSize() << '\n';

    if (frames_processed == 0) {
        throw std::runtime_error("video source opened but no frames were decoded");
    }
    const double frame_count = static_cast<double>(frames_processed);
    std::cout << "average_brightness: " << totals.brightness / frame_count << '\n';
    std::cout << "average_sharpness: " << totals.sharpness / frame_count << '\n';
    std::cout << "average_processing_latency_ms: " << totals.latency_ms / frame_count << '\n';
    std::cout << "warning_frames: " << warning_frames << '\n';
    std::cout << "alarm_frames: " << alarm_frames << '\n';
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
        return run(options);
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        std::cerr << "run with --help for usage\n";
        return 1;
    }
}
