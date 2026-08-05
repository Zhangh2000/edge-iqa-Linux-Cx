#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace iqa {

enum class PacketType : std::uint32_t {
    JpegImage = 1,
    ResultText = 2,
};

struct Packet {
    PacketType type = PacketType::ResultText;
    std::vector<std::uint8_t> payload;
};

class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(int descriptor);
    ~TcpSocket();

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& other) noexcept;
    TcpSocket& operator=(TcpSocket&& other) noexcept;

    bool valid() const;
    int descriptor() const;
    void close();

private:
    int descriptor_ = -1;
};

TcpSocket connectTcp(const std::string& host, std::uint16_t port);
TcpSocket listenTcp(const std::string& bind_address, std::uint16_t port, int backlog = 8);
TcpSocket acceptTcp(const TcpSocket& listener, std::string& peer_address);

void sendPacket(const TcpSocket& socket, PacketType type,
                const std::vector<std::uint8_t>& payload);
bool receivePacket(const TcpSocket& socket, Packet& packet,
                   std::size_t max_payload_bytes = 32U * 1024U * 1024U);

std::uint32_t crc32(const std::uint8_t* data, std::size_t size);

}  // namespace iqa
