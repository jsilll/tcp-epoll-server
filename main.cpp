#include "server.h"

#include <string>
#include <cstring>
#include <iostream>

#include <arpa/inet.h>

/// @brief Example handler for the server.
template<std::size_t BufSize>
class EchoHandler {
public:
    /**
     * @brief Called when a new connection is established.
     * @param addr The address of the new connection.
     */
    std::vector<std::byte> OnNew(const sockaddr_in &addr) {
        std::cout << "New connection from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
        return {};
    }

    /**
     * @brief Called when a connection is closed.
     * @param addr The address of the closed connection.
     */
    void OnClose(const sockaddr_in &addr) {
        std::cout << "Connection closed from " << inet_ntoa(addr.sin_addr) << ":" << ntohs(addr.sin_port) << std::endl;
    }

    /**
     * @brief Called when a message is received.
     * @param addr The address of the connection that sent the message.
     * @param buf The message.
     */
    std::vector<std::byte> OnRead(const sockaddr_in &addr, const std::array<std::byte, BufSize> &bytes) {
        std::size_t len = std::strlen(reinterpret_cast<const char *>(bytes.data()));
        std::cout << "Received '" << std::string(reinterpret_cast<const char *>(bytes.data()), len)
                  << "' from " << inet_ntoa(addr.sin_addr) << ":"
                  << ntohs(addr.sin_port) << std::endl;
        return {bytes.begin(), bytes.begin() + len};
    }
};

int main() {
    // Config Params
    constexpr std::size_t threads = 8;
    constexpr std::uint16_t port = 8080;
    constexpr std::size_t buf_size = 1024;
    constexpr std::size_t max_events = 16;

    // Create the handler
    EchoHandler<buf_size> handler;

    try {
        // Create and run the server
        tcp::Server<max_events, buf_size> server(port, threads);
        std::cout << "Server started on port: " << port << std::endl;
        server.Run(handler);
    } catch (const tcp::Error &e) {
        std::cerr << e.kind() << ": " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
}