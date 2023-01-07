#pragma once

#include "server.h"

#include <cstring>
#include <arpa/inet.h>

/**
 * Handler for echo server. Welcomes the client and echoes back the message.
 * On every message received from the client, it sends back the same message.
 */
class EchoHandler {
public:
    /**
     * @brief Called when a new connection is established.
     * @param addr The address of the new connection.
     * @param out_buf The buffer to write the response to.
     * @return whether connection should continue.
     */
    [[nodiscard]] static auto OnNew(const sockaddr_in &addr, std::vector<std::byte> &out_buf) noexcept -> bool {
        static const std::string msg = "Welcome to the echo server!";
        out_buf.resize(msg.size());
        std::transform(msg.begin(), msg.end(), out_buf.begin(), [](char c) { return std::byte(c); });
        std::cout << "New connection from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
        return true;
    }

    /**
     * @brief Called when a message is received.
     * @param addr The address of the connection that sent the message.
     * @param buf The message.
     */
    [[nodiscard]] static auto OnRead(const sockaddr_in &addr, 
                                     const std::vector<std::byte> &in_buf,
                                     std::vector<std::byte> &out_buf) noexcept -> bool {
        std::size_t len = std::strlen(reinterpret_cast<const char *>(in_buf.data()));
        out_buf.resize(len);
        std::copy(in_buf.begin(), in_buf.begin() + static_cast<long>(len), out_buf.begin());
        std::cout << "Received '" << std::string(reinterpret_cast<const char *>(in_buf.data()), len) << "' from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
        return true;
    }

    /**
     * @brief Called when a connection is closed.
     * @param addr The address of the closed connection.
     */
    static void OnClose(const sockaddr_in &addr) noexcept {
        std::cout << "Connection closed from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
    }

    /**
     * @brief Called when an error occurs.
     * @param addr The address of the connection that caused the error.
     * @param error The error.
    */
    void OnError(const sockaddr_in &addr, const tcp::Error &error) noexcept {
        std::cout << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << ": " << error.what() << std::endl;
    }
};