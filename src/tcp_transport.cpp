#include "iqa/tcp_transport.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace iqa {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'E', 'I', 'Q', '1'};
constexpr std::size_t kHeaderSize = 16;

std::runtime_error socketError(const std::string& action) {
    return std::runtime_error(action + ": " + std::strerror(errno));
}

void writeUint32(std::uint8_t* destination, std::uint32_t value) {
    destination[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
    destination[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    destination[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    destination[3] = static_cast<std::uint8_t>(value & 0xffU);
}

std::uint32_t readUint32(const std::uint8_t* source) {
    return (static_cast<std::uint32_t>(source[0]) << 24U) |
           (static_cast<std::uint32_t>(source[1]) << 16U) |
           (static_cast<std::uint32_t>(source[2]) << 8U) |
           static_cast<std::uint32_t>(source[3]);
}

void sendAll(int descriptor, const std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t sent = ::send(descriptor, data + offset, size - offset, flags);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw socketError("send failed");
        }
        if (sent == 0) {
            throw std::runtime_error("send failed: peer closed the connection");
        }
        offset += static_cast<std::size_t>(sent);
    }
}

bool receiveAll(int descriptor, std::uint8_t* data, std::size_t size) {
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t received = ::recv(descriptor, data + offset, size - offset, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw socketError("receive failed");
        }
        if (received == 0) {
            if (offset == 0) {
                return false;
            }
            throw std::runtime_error("receive failed: connection closed during a packet");
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

void configureSocket(int descriptor) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        throw socketError("setsockopt SO_NOSIGPIPE failed");
    }
#else
    (void)descriptor;
#endif
}

std::string portText(std::uint16_t port) {
    return std::to_string(static_cast<unsigned int>(port));
}

}  // namespace

TcpSocket::TcpSocket(int descriptor) : descriptor_(descriptor) {
    if (descriptor_ >= 0) {
        try {
            configureSocket(descriptor_);
        } catch (...) {
            ::close(descriptor_);
            descriptor_ = -1;
            throw;
        }
    }
}

TcpSocket::~TcpSocket() {
    close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : descriptor_(other.descriptor_) {
    other.descriptor_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
    if (this != &other) {
        close();
        descriptor_ = other.descriptor_;
        other.descriptor_ = -1;
    }
    return *this;
}

bool TcpSocket::valid() const {
    return descriptor_ >= 0;
}

int TcpSocket::descriptor() const {
    if (!valid()) {
        throw std::runtime_error("socket is not open");
    }
    return descriptor_;
}

void TcpSocket::close() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
        descriptor_ = -1;
    }
}

TcpSocket connectTcp(const std::string& host, std::uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses = nullptr;
    const int lookup = ::getaddrinfo(host.c_str(), portText(port).c_str(), &hints, &addresses);
    if (lookup != 0) {
        throw std::runtime_error("address lookup failed: " + std::string(gai_strerror(lookup)));
    }

    int last_error = 0;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int descriptor =
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (descriptor < 0) {
            last_error = errno;
            continue;
        }
        if (::connect(descriptor, address->ai_addr, address->ai_addrlen) == 0) {
            ::freeaddrinfo(addresses);
            return TcpSocket(descriptor);
        }
        last_error = errno;
        ::close(descriptor);
    }

    ::freeaddrinfo(addresses);
    errno = last_error;
    throw socketError("connect failed");
}

TcpSocket listenTcp(const std::string& bind_address, std::uint16_t port, int backlog) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* addresses = nullptr;
    const char* host = bind_address.empty() ? nullptr : bind_address.c_str();
    const int lookup = ::getaddrinfo(host, portText(port).c_str(), &hints, &addresses);
    if (lookup != 0) {
        throw std::runtime_error("bind address lookup failed: " +
                                 std::string(gai_strerror(lookup)));
    }

    int last_error = 0;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int descriptor =
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (descriptor < 0) {
            last_error = errno;
            continue;
        }

        const int reuse = 1;
        ::setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (::bind(descriptor, address->ai_addr, address->ai_addrlen) == 0 &&
            ::listen(descriptor, backlog) == 0) {
            ::freeaddrinfo(addresses);
            return TcpSocket(descriptor);
        }
        last_error = errno;
        ::close(descriptor);
    }

    ::freeaddrinfo(addresses);
    errno = last_error;
    throw socketError("listen failed");
}

TcpSocket acceptTcp(const TcpSocket& listener, std::string& peer_address) {
    sockaddr_storage address{};
    socklen_t address_size = sizeof(address);
    int descriptor = -1;
    do {
        descriptor = ::accept(listener.descriptor(),
                              reinterpret_cast<sockaddr*>(&address),
                              &address_size);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        throw socketError("accept failed");
    }

    std::array<char, NI_MAXHOST> host{};
    std::array<char, NI_MAXSERV> service{};
    const int lookup = ::getnameinfo(reinterpret_cast<sockaddr*>(&address),
                                     address_size,
                                     host.data(),
                                     static_cast<socklen_t>(host.size()),
                                     service.data(),
                                     static_cast<socklen_t>(service.size()),
                                     NI_NUMERICHOST | NI_NUMERICSERV);
    peer_address = lookup == 0
        ? std::string(host.data()) + ":" + service.data()
        : "unknown";
    return TcpSocket(descriptor);
}

void sendPacket(const TcpSocket& socket,
                PacketType type,
                const std::vector<std::uint8_t>& payload) {
    if (payload.size() > 0xffffffffU) {
        throw std::runtime_error("packet payload is too large");
    }

    std::array<std::uint8_t, kHeaderSize> header{};
    std::copy(kMagic.begin(), kMagic.end(), header.begin());
    writeUint32(header.data() + 4, static_cast<std::uint32_t>(type));
    writeUint32(header.data() + 8, static_cast<std::uint32_t>(payload.size()));
    writeUint32(header.data() + 12, crc32(payload.data(), payload.size()));

    sendAll(socket.descriptor(), header.data(), header.size());
    if (!payload.empty()) {
        sendAll(socket.descriptor(), payload.data(), payload.size());
    }
}

bool receivePacket(const TcpSocket& socket, Packet& packet, std::size_t max_payload_bytes) {
    std::array<std::uint8_t, kHeaderSize> header{};
    if (!receiveAll(socket.descriptor(), header.data(), header.size())) {
        return false;
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), header.begin())) {
        throw std::runtime_error("invalid packet magic");
    }

    const std::uint32_t type = readUint32(header.data() + 4);
    const std::uint32_t payload_size = readUint32(header.data() + 8);
    const std::uint32_t expected_crc = readUint32(header.data() + 12);
    if (payload_size > max_payload_bytes) {
        throw std::runtime_error("packet payload exceeds configured limit");
    }

    packet.type = static_cast<PacketType>(type);
    packet.payload.resize(payload_size);
    if (payload_size > 0 &&
        !receiveAll(socket.descriptor(), packet.payload.data(), packet.payload.size())) {
        throw std::runtime_error("connection closed before packet payload");
    }
    if (crc32(packet.payload.data(), packet.payload.size()) != expected_crc) {
        throw std::runtime_error("packet CRC32 mismatch");
    }
    return true;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t value = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        value ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = 0U - (value & 1U);
            value = (value >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~value;
}

}  // namespace iqa
