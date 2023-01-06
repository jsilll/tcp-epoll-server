#pragma once

#include "server.h"

#include <cstring>
#include <arpa/inet.h>

/// @brief Example handler for the server.
template<std::size_t BufSize>
class EchoHandler {
public:
    /**
     * @brief Called when a new connection is established.
     * @param addr The address of the new connection.
     */
    [[nodiscard]] std::vector<std::byte> OnNew(const sockaddr_in &addr) const noexcept {
        static const std::string msg = "Welcome to the echo server!";
        std::cout << "New connection from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
        std::vector<std::byte> res(msg.size());
        std::transform(msg.begin(), msg.end(), res.begin(), [](char c) { return std::byte(c); });
        return res;
    }

    /**
     * @brief Called when a connection is closed.
     * @param addr The address of the closed connection.
     */
    void OnClose(const sockaddr_in &addr) const noexcept {
        std::cout << "Connection closed from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
    }

    /**
     * @brief Called when a message is received.
     * @param addr The address of the connection that sent the message.
     * @param buf The message.
     */
    [[nodiscard]] std::vector<std::byte>
    OnRead(const sockaddr_in &addr, const std::array<std::byte, BufSize> &bytes) const noexcept {
        std::size_t len = std::strlen(reinterpret_cast<const char *>(bytes.data()));
        std::cout << "Received '" << std::string(reinterpret_cast<const char *>(bytes.data()), len)
                  << "' from " << inet_ntoa(addr.sin_addr) << ":"
                  << ntohs(addr.sin_port) << std::endl;
        return {bytes.begin(), bytes.begin() + len};
    }
};