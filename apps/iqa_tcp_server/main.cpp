#include "iqa/alert.hpp"
#include "iqa/frame_metrics.hpp"
#include "iqa/tcp_transport.hpp"

#include <opencv2/imgcodecs.hpp>

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 9000;
    bool once = false;
    iqa::AlertConfig alert_config;
    bool help = false;
};

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  iqa_tcp_server [options]\n\n"
        << "Options:\n"
        << "  --bind <address>     Bind address (default: 127.0.0.1)\n"
        << "  --port <1-65535>     TCP port (default: 9000)\n"
        << "  --once               Exit after one client disconnects\n"
        << "  --brightness-min <v> Alarm below mean brightness v\n"
        << "  --sharpness-min <v>  Alarm below Laplacian variance v\n"
        << "  --alert-frames <n>   Consecutive violations required (default: 3)\n"
        << "  --recovery-frames <n> Consecutive good frames to recover (default: 3)\n"
        << "  --help, -h           Show this help\n";
}

long long parseInteger(const std::string& text, const std::string& flag) {
    std::size_t parsed = 0;
    long long value = 0;
    try {
        value = std::stoll(text, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error("invalid integer for " + flag + ": " + text);
    }
    if (parsed != text.size()) {
        throw std::runtime_error("invalid integer for " + flag + ": " + text);
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

int parsePositiveInt(const std::string& text, const std::string& flag) {
    const long long value = parseInteger(text, flag);
    if (value <= 0 || value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("invalid positive integer for " + flag + ": " + text);
    }
    return static_cast<int>(value);
}

Options parseArgs(int argc, char** argv) {
    Options options;
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
        } else if (arg == "--bind") {
            options.bind_address = needValue(arg);
        } else if (arg == "--port") {
            const long long port = parseInteger(needValue(arg), arg);
            if (port <= 0 || port > 65535) {
                throw std::runtime_error("port must be within [1, 65535]");
            }
            options.port = static_cast<std::uint16_t>(port);
        } else if (arg == "--once") {
            options.once = true;
        } else if (arg == "--brightness-min") {
            const double value = parseNonNegativeDouble(needValue(arg), arg);
            if (value > 255.0) {
                throw std::runtime_error("brightness threshold must be within [0, 255]");
            }
            options.alert_config.brightness_min = value;
        } else if (arg == "--sharpness-min") {
            options.alert_config.sharpness_min = parseNonNegativeDouble(needValue(arg), arg);
        } else if (arg == "--alert-frames") {
            options.alert_config.trigger_frames = parsePositiveInt(needValue(arg), arg);
        } else if (arg == "--recovery-frames") {
            options.alert_config.recovery_frames = parsePositiveInt(needValue(arg), arg);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

std::vector<std::uint8_t> resultPayload(const iqa::FrameMetrics& metrics,
                                        const iqa::AlertDecision& alert) {
    std::ostringstream result;
    result << std::fixed << std::setprecision(3)
           << "brightness_mean=" << metrics.brightness_mean
           << ";sharpness_laplacian_variance="
           << metrics.sharpness_laplacian_variance
           << ";status=" << iqa::alertStatusName(alert.status)
           << ";reason=" << (alert.reason.empty() ? "none" : alert.reason);
    const std::string text = result.str();
    return {text.begin(), text.end()};
}

void serveClient(const iqa::TcpSocket& client, const iqa::AlertConfig& alert_config) {
    iqa::AlertStateMachine alert_machine(alert_config);
    iqa::Packet packet;
    long long images_received = 0;
    while (iqa::receivePacket(client, packet)) {
        if (packet.type != iqa::PacketType::JpegImage) {
            throw std::runtime_error("unsupported packet type");
        }

        const cv::Mat encoded(1,
                              static_cast<int>(packet.payload.size()),
                              CV_8U,
                              packet.payload.data());
        const cv::Mat image = cv::imdecode(encoded, cv::IMREAD_COLOR);
        if (image.empty()) {
            throw std::runtime_error("received payload is not a decodable image");
        }

        const iqa::FrameMetrics metrics = iqa::computeFrameMetrics(image);
        const iqa::AlertDecision alert = alert_machine.evaluate(metrics);
        iqa::sendPacket(client,
                        iqa::PacketType::ResultText,
                        resultPayload(metrics, alert));
        ++images_received;
        std::cout << "image=" << images_received
                  << " bytes=" << packet.payload.size()
                  << " status=" << iqa::alertStatusName(alert.status) << '\n';
    }
    std::cout << "client_images: " << images_received << '\n';
}

int run(const Options& options) {
    iqa::TcpSocket listener = iqa::listenTcp(options.bind_address, options.port);
    std::cout << "listening: " << options.bind_address << ':' << options.port << '\n';
    std::cout << "alert_enabled: " << (options.alert_config.enabled() ? "yes" : "no")
              << '\n';

    do {
        std::string peer_address;
        iqa::TcpSocket client = iqa::acceptTcp(listener, peer_address);
        std::cout << "client_connected: " << peer_address << '\n';
        serveClient(client, options.alert_config);
        std::cout << "client_disconnected: " << peer_address << '\n';
    } while (!options.once);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseArgs(argc, argv);
        if (options.help) {
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
