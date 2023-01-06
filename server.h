#pragma once

#include "thread_pool.h"

#include <array>
#include <vector>
#include <memory>
#include <cstdint>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace tcp {

    /// @brief Error class for the server.
    class Error : public std::runtime_error {
    public:
        /// @brief Error types for the server.
        enum Kind : int {
            /// @brief Error while creating the socket.
            SocketCreation,
            /// @brief Error while binding the socket.
            SocketBinding,
            /// @brief Error while listening on the socket.
            SocketListening,
            /// @brief Error while creating the epoll instance.
            EpollCreation,
            /// @brief Error while adding the socket to the epoll instance.
            EpollAdd,
            /// @brief Error while deleting the socket from the epoll instance.
            EpollDelete,
            /// @brief Error while waiting for events.
            EpollWait,
            /// @brief Error while accepting a new connection.
            Accept,
            /// @brief Error while getting the address of a connection.
            GetAddress,
            /// @brief Error while reading from a connection.
            Read,
            /// @brief Error while writing to a connection.
            Write,
            /// @brief Error while closing a connection.
            Close,
        };

        /**
         * @brief Creates a new server error.
         * @param msg Error message.
         */
        explicit Error(const std::string &msg, Kind kind) : std::runtime_error(msg), _kind(kind) {}

        /**
         * @brief Returns the error kind.
         * @return The error kind.
         */
        [[nodiscard]] constexpr auto kind() const noexcept { return _kind; }

    private:
        Kind _kind;
    };

    /// @brief Concept for the connection handler.
    template<class T, std::size_t BufSize>
    concept ConnectionHandler = requires(T t, const sockaddr_in &addr, const std::array<std::byte, BufSize> &buf) {
        { t.OnNew(addr) } -> std::same_as<std::vector<std::byte>>;
        { t.OnClose(addr) } -> std::same_as<void>;
        { t.OnRead(addr, buf) } -> std::same_as<std::vector<std::byte>>;
    };

    /**
     * @brief Class for the server.
     * @tparam MaxEvents Maximum number of events to handle.
     * @tparam BufSize Size of the buffer for the read operation.
     */
    template<std::size_t MaxEvents, std::size_t BufSize>
    class Server {
    public:

        /**
         * @brief Creates a new server.
         * @param port The port to listen on.
         */
        [[nodiscard]] explicit Server(std::uint16_t port, std::size_t threads) : _port(port),
                                                                                 _epoll_fd(epoll_create1(0)),
                                                                                 _server_fd(socket(AF_INET, SOCK_STREAM,
                                                                                                   0)),
                                                                                 _thread_pool(threads) {
            // Check if epoll was created successfully
            if (_epoll_fd == -1) {
                throw Error("Failed to create epoll instance.", Error::Kind::EpollCreation);
            }

            // Check if the server socket was created successfully
            if (_server_fd == -1) {
                throw Error("Failed to create server socket.", Error::Kind::SocketCreation);
            }

            // Make the server socket non-blocking
            if (MakeSocketNonBlocking(_server_fd) == -1) {
                throw Error("Failed to make server socket non-blocking.", Error::Kind::SocketCreation);
            }

            // Set socket options
            const int opt = 1;
            if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
                throw Error("Failed to set socket options.", Error::Kind::SocketCreation);
            }

            // Bind the socket to an address and port
            sockaddr_in server_addr{.sin_family = AF_INET, .sin_port = htons(
                    _port), .sin_addr = {.s_addr = INADDR_ANY}};
            if (bind(_server_fd, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
                throw Error("Failed to bind server socket.", Error::Kind::SocketBinding);
            }
        }

        /**
         * @brief Closes the sever's socket and epoll instance.
         */
        ~Server() noexcept {
            close(_server_fd);
            close(_epoll_fd);
        }

        /**
         * @brief Runs the server.
         * @param handler The handler for the server.
         */
        template<ConnectionHandler<BufSize> H>
        [[noreturn]] void Run(H &handler) {
            // Listen for incoming connections
            if (listen(_server_fd, SOMAXCONN) == -1) {
                throw Error("Failed to listen on server socket.", Error::Kind::SocketListening);
            }

            // Add the server socket to the epoll instance
            epoll_event event{.events = EPOLLIN, .data = {.fd = _server_fd}};
            if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _server_fd, &event) == -1) {
                throw Error("Failed to add server socket to epoll instance.", Error::Kind::EpollAdd);
            }

            // Set up an array to hold the events that are triggered
            epoll_event events[MaxEvents];

            // Event Loop
            while (true) {
                // Wait for events on the sockets in the epoll instance
                const int num_events = epoll_wait(_epoll_fd, events, MaxEvents, -1);
                if (num_events == -1) {
                    throw Error("Failed to wait for events.", Error::Kind::EpollWait);
                }

                // Process each event
                for (int i = 0; i < num_events; ++i) {
                    if (events[i].data.fd == _server_fd) { // New connection
                        _thread_pool.Push([this, &handler] { HandleNewConnection(handler); });
                    } else { // Existing connection
                        _thread_pool.Push(
                                [this, &handler, fd = events[i].data.fd] { HandleConnectionUpdate(fd, handler); });
                    }
                }
            }
        }

    private:
        template<ConnectionHandler<BufSize> Handler>
        void HandleNewConnection(Handler handler) {
            // Accept the connection
            sockaddr_in client_addr{};
            socklen_t client_addr_len = sizeof(client_addr);
            const int client_fd = accept(_server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);
            if (client_fd == -1) {
                throw Error("Failed to accept a new connection.", Error::Kind::Accept);
            }

            // Make the client socket non-blocking
            if (MakeSocketNonBlocking(client_fd) == -1) {
                throw Error("Failed to make client socket non-blocking.", Error::Kind::Accept);
            }

            // Add the client socket to the epoll instance
            epoll_event client_event = {.events = EPOLLIN, .data = {.fd = client_fd}};
            if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                throw Error("Failed to add client socket to epoll instance.", Error::Kind::EpollAdd);
            }

            // Call the Handler
            const std::vector<std::byte> bytes = handler.OnNew(client_addr);
            if (!bytes.empty()) {
                // Write the response to the client
                if (write(client_fd, bytes.data(), bytes.size()) == -1) {
                    throw Error("Failed to write to client socket.", Error::Kind::Write);
                }
            }

        }

        template<ConnectionHandler<BufSize> Handler>
        void HandleConnectionUpdate(int client_socket, Handler handler) {
            // Set up the buffer for the read operation
            std::array<std::byte, BufSize> buf{};

            // Read the message
            const ssize_t n = read(client_socket, buf.data(), buf.size());
            if (n == -1) {
                throw Error("Failed to read message.", Error::Kind::Read);
            }

            if (n == 0) { // If the read returns 0, it means the client has closed the connection
                // Get the client address
                const auto client_addr = GetClientAddress(client_socket);

                // Remove the client socket from the epoll instance
                if (epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, client_socket, nullptr) == -1) {
                    throw Error("Failed to remove client socket from epoll instance.", Error::Kind::EpollDelete);
                }

                // Close the socket
                if (close(client_socket) == -1) {
                    throw Error("Failed to close client socket.", Error::Kind::Close);
                }

                // Call the Handler
                handler.OnClose(client_addr);
            } else {
                // Get the client address
                const auto client_addr = GetClientAddress(client_socket);

                // Call the Handler
                const std::vector<std::byte> bytes = handler.OnRead(client_addr, buf);
                if (!bytes.empty()) {
                    // Write the response
                    if (write(client_socket, bytes.data(), bytes.size()) == -1) {
                        throw Error("Failed to write response.", Error::Kind::Write);
                    }
                }
            }
        }

        static int MakeSocketNonBlocking(int fd) {
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1) {
                throw Error("Failed to get socket flags.", Error::Kind::SocketCreation);
            }
            return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        static sockaddr_in GetClientAddress(int client_fd) {
            sockaddr_in client_addr{};
            socklen_t client_addr_len = sizeof(client_addr);
            if (getpeername(client_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len) == -1) {
                throw Error("Failed to get client address.", Error::Kind::GetAddress);
            }
            return client_addr;
        }

        /// @brief The epoll instance's file descriptor.
        int _epoll_fd;
        /// @brief The server socket's file descriptor.
        int _server_fd;
        /// @brief The port to listen on.
        std::uint16_t _port;

        /// @brief Thread pool for handling connections events.
        ThreadPool _thread_pool;
    };

}//namespace tcp
