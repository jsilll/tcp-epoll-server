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
            /// @brief Error while waiting for events.
            EpollWait,
            /// @brief Error while reading from a connection.
            Read,
            /// @brief Error while writing to a connection.
            Write,
        };

        /**
         * @brief Creates a new server error.
         * @param msg Error message.
         * @param kind Error kind.
         */
        Error(const std::string &msg, Kind kind) : std::runtime_error(msg), _kind(kind) {}

        /**
         * @brief Returns the error kind.
         * @return The error kind.
         */
        [[nodiscard]] constexpr auto kind() const noexcept { return _kind; }

    private:
        /// @brief The error kind.
        Kind _kind;
    };

    /**
     * TCP server. 
     * Accepts new connections and handles using a provided handler.
     */
    class Server {
    public:
        /**
         * @brief Creates a new server.
         * @param port The port to listen on.
         * @param threads The number of threads to use.
         * @param buf_size The buffer size for the receive operation in each connection.
         * @param max_events The maximum number of events to wait for.
         */
        [[nodiscard]] Server(std::uint16_t port,
                             std::size_t threads,
                             std::size_t buf_size,
                             int max_events) :
                _port(port),
                _buf_size(buf_size),
                _max_events(max_events),
                _epoll_fd(epoll_create1(0)),
                _server_fd(socket(AF_INET, SOCK_STREAM, 0)),
                _thread_pool(threads) {
            // Check if the max_events is valid.
            if (max_events <= 0) {
                throw Error("Invalid max events.", Error::Kind::EpollCreation);
            }

            // Check if epoll was created successfully
            if (_epoll_fd == -1) {
                throw Error("Failed to create epoll instance.", Error::Kind::EpollCreation);
            }

            // Check if the server socket was created successfully
            if (_server_fd == -1) {
                throw Error("Failed to create server socket.", Error::Kind::SocketCreation);
            }

            // Set socket options
            const int opt = 1;
            if (setsockopt(_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
                throw Error("Failed to set socket options.", Error::Kind::SocketCreation);
            }

            // Bind the socket to an address and port
            sockaddr_in server_addr{.sin_family = AF_INET, .sin_port = htons(_port), .sin_addr = {.s_addr = INADDR_ANY}};
            if (bind(_server_fd, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr)) == -1) {
                throw Error("Failed to bind server socket.", Error::Kind::SocketBinding);
            }
        }

        /**
         * @brief Closes the sever's socket and epoll instance.
         */
        ~Server() noexcept {
            close(_epoll_fd);
            close(_server_fd);
        }

        /**
         * @brief Runs the server.
         * @tparam H The connection handler type.
         * @param handler The handler for the server.
         */
        template<typename Handler>
        [[noreturn]] void Run(Handler &handler) {
            // Listen for incoming connections
            if (listen(_server_fd, SOMAXCONN) == -1) {
                throw Error("Failed to listen on server socket.", Error::Kind::SocketListening);
            }

            // Add the server socket to the epoll instance
            epoll_event server_event = {.events = EPOLLIN, .data = {.fd = _server_fd}};
            if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _server_fd, &server_event) == -1) {
                throw Error("Failed to add server socket to epoll instance.", Error::Kind::EpollAdd);
            }

            // Set up an array to hold the events that are triggered
            std::vector<epoll_event> events(_max_events);

            // Event Loop
            while (true) {
                // Wait for events on the sockets in the epoll instance
                const int num_events = epoll_wait(_epoll_fd, events.data(), _max_events, -1);
                // Check if there was an error while waiting for events
                if (num_events == -1) {
                    throw Error("Failed to wait for events.", Error::Kind::EpollWait);
                }

                // Process each event
                for (int i = 0; i < num_events; ++i) {
                    // Check if the event was triggered by our own close() call 
                    if (events[i].events & EPOLLHUP) {
                        continue;
                    }

                    if (events[i].data.fd == _server_fd) { 
                        // New connection 

                        // Accept the connection
                        sockaddr_in client_addr{};
                        socklen_t client_addr_len = sizeof(client_addr);
                        const int client_fd = accept(_server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);

                        // Check if the connection was accepted successfully
                        if (client_fd == -1) {
                            continue; // Ignore the connection
                        }

                        // Set Timeout for receive operation on the client socket 
                        timeval timeout{.tv_sec = 15, .tv_usec = 0}; // TODO: parameterize this value
                        if (setsockopt (client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
                            close(client_fd);
                            continue; // Ignore the connection
                        }

                        // Set Timeout for write operation on the client socket 
                        if (setsockopt (client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1) {
                            close(client_fd);
                            continue; // Ignore the connection
                        }

                        // Add the server socket to the epoll instance
                        epoll_event client_event = {.events = EPOLLIN, .data = {.fd = client_fd}};
                        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                            close(client_fd);
                            continue; // Ignore the connection
                        }
                        
                        _thread_pool.Push([&handler, client_fd] { HandleNewConnection(handler, client_fd); });
                    } else { 
                        // Event on existing connection

                        // Get the client socket
                        const int client_fd = events[i].data.fd;

                        // Read the message
                        std::vector<std::byte> in_buf(_buf_size);
                        const ssize_t n = read(client_fd, in_buf.data(), in_buf.size());

                        // Check if there was an error, or if the client closed the connection
                        if (n == -1) {
                            // Get the client address
                            const auto client_addr = GetClientAddress(client_fd);

                            // Close the connection 
                            close(client_fd);

                            _thread_pool.Push([&handler, client_addr] { handler.OnError(client_addr, {"Failed to read from a client.", Error::Kind::Read}); });
                        } else if (n == 0) {
                            // Get the client address
                            const auto client_addr = GetClientAddress(client_fd);

                            // Close the connection
                            close(client_fd);

                            _thread_pool.Push([&handler, client_addr] { handler.OnClose(client_addr); });
                        } else {
                            _thread_pool.Push([&handler, client_fd, in_buf = std::move(in_buf)] { HandleRead(handler, client_fd, in_buf); });
                        }
                    }
                }
            }
        }

    private:

        // -- Threaded Functions --

        template<typename Handler>
        static void HandleNewConnection(Handler &handler, const int client_fd) {
            // Get the client's address
            const auto client_addr = GetClientAddress(client_fd);

            // Call the Handler
            std::vector<std::byte> out_buf;
            const bool keep_alive = handler.OnNew(client_addr, out_buf);

            // Write the response to the client
            try {
                Write(client_fd, out_buf);
            } catch (const Error &e) {
                // Close the connection
                close(client_fd);

                // Call the Handler
                return handler.OnError(client_addr, e);
            }

            // Close the connection if the handler has requested it
            if (!keep_alive) {
                // Close the connection
                close(client_fd);
            }
        }

        template<typename Handler>
        static void HandleRead(Handler &handler, const int client_fd, const std::vector<std::byte> &in_buf) {
            // Get the client address
            const auto client_addr = GetClientAddress(client_fd);
                
            // Set up the buffer for the write operation
            std::vector<std::byte> out_buf;
                
            // Call the Handler
            const bool keep_alive = handler.OnRead(client_addr, in_buf, out_buf);
                
            // Write the response to the client
            try {
                // Write the response to the client
                Write(client_fd, out_buf);
            } catch (const Error &e) {
                // Close the connection
                close(client_fd);

                // Call the Handler
                return handler.OnError(client_addr, e);
            }
                
            // Close the connection if the handler has requested it
            if (!keep_alive) {
                // Close the connection
                close(client_fd);
            }
        }

        // -- Helper Static Methods --

        static void Write(int client_fd, const std::vector<std::byte> &out_buf) {
            if (!out_buf.empty()) {
                if (write(client_fd, out_buf.data(), out_buf.size()) == -1) {
                    throw Error("Failed to write response.", Error::Kind::Write);
                }
            }
        }

        static sockaddr_in GetClientAddress(int client_fd) {
            sockaddr_in client_addr{};
            socklen_t client_addr_len = sizeof(client_addr);
            if (getpeername(client_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len) == -1) {
                return {}; // Ignore errors, TODO: maybe fix this
            }
            return client_addr;
        }

        // -- Member Variables --

        /// @brief The port to listen on.
        std::uint16_t _port;
        /// @brief The receive buffer size.
        std::size_t _buf_size;
        /// @brief The maximum number of events to wait for at a time.
        int _max_events;

        /// @brief The epoll instance's file descriptor.
        int _epoll_fd;
        /// @brief The server socket's file descriptor.
        int _server_fd;

        /// @brief Thread pool for handling connections events.
        ThreadPool _thread_pool;
    };

}//namespace tcp
