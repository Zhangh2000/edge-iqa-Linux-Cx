#include "iqa/tcp_transport.hpp"

#include <opencv2/imgcodecs.hpp>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    int jpeg_quality = 90;
    std::vector<std::string> image_paths;
    bool help = false;
};

void printUsage() {
    std::cout
        << "Usage:\n"
        << "  iqa_tcp_client --image <path> [--image <path> ...] [options]\n\n"
        << "Options:\n"
        << "  --host <address>     Server address (default: 127.0.0.1)\n"
        << "  --port <1-65535>     Server port (default: 9000)\n"
        << "  --image <path>       Image to send; may be repeated\n"
        << "  --jpeg-quality <n>   JPEG quality 1-100 (default: 90)\n"
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
        } else if (arg == "--host") {
            options.host = needValue(arg);
        } else if (arg == "--port") {
            const long long port = parseInteger(needValue(arg), arg);
            if (port <= 0 || port > 65535) {
                throw std::runtime_error("port must be within [1, 65535]");
            }
            options.port = static_cast<std::uint16_t>(port);
        } else if (arg == "--image") {
            options.image_paths.push_back(needValue(arg));
        } else if (arg == "--jpeg-quality") {
            const long long quality = parseInteger(needValue(arg), arg);
            if (quality < 1 || quality > 100) {
                throw std::runtime_error("JPEG quality must be within [1, 100]");
            }
            options.jpeg_quality = static_cast<int>(quality);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

std::vector<std::uint8_t> encodeJpeg(const std::string& path, int quality) {
    const cv::Mat image = cv::imread(path, cv::IMREAD_COLOR);
    if (image.empty()) {
        throw std::runtime_error("failed to read image: " + path);
    }

    std::vector<std::uint8_t> encoded;
    if (!cv::imencode(".jpg", image, encoded, {cv::IMWRITE_JPEG_QUALITY, quality})) {
        throw std::runtime_error("failed to encode image: " + path);
    }
    return encoded;
}

int run(const Options& options) {
    if (options.image_paths.empty()) {
        throw std::runtime_error("at least one --image is required");
    }

    iqa::TcpSocket socket = iqa::connectTcp(options.host, options.port);
    std::cout << "connected: " << options.host << ':' << options.port << '\n';
    for (std::size_t i = 0; i < options.image_paths.size(); ++i) {
        const std::vector<std::uint8_t> jpeg =
            encodeJpeg(options.image_paths[i], options.jpeg_quality);
        iqa::sendPacket(socket, iqa::PacketType::JpegImage, jpeg);

        iqa::Packet response;
        if (!iqa::receivePacket(socket, response)) {
            throw std::runtime_error("server closed before returning a result");
        }
        if (response.type != iqa::PacketType::ResultText) {
            throw std::runtime_error("server returned an unexpected packet type");
        }
        const std::string result(response.payload.begin(), response.payload.end());
        std::cout << "image=" << (i + 1)
                  << " path=" << options.image_paths[i]
                  << " jpeg_bytes=" << jpeg.size()
                  << " result=" << result << '\n';
    }
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
