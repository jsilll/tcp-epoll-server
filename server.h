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
            /// @brief Error while accepting a new connection.
            Accept,
            /// @brief Error while reading from a connection.
            Read,
            /// @brief Error while writing to a connection.
            Write,
            /// @brief Error while closing a connection.
            Close,
            /// @brief Error while getting the address of a connection.
            GetAddress,
        };

        /**
         * @brief Creates a new server error.
         * @param msg Error message.
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
         */
        [[nodiscard]] Server(std::uint16_t port,
                             std::size_t threads,
                             std::size_t buf_size,
                             int max_events) :
                _port(port),
                _thread_pool(threads),
                _buf_size(buf_size),
                _max_events(max_events),
                _epoll_fd(epoll_create1(0)),
                _server_fd(socket(AF_INET, SOCK_STREAM, 0)) {
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

            // Make the server socket non-blocking
            MakeSocketNonBlocking(_server_fd);

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
        template<typename H>
        [[noreturn]] void Run(H &handler) {
            // Listen for incoming connections
            if (listen(_server_fd, SOMAXCONN) == -1) {
                throw Error("Failed to listen on server socket.", Error::Kind::SocketListening);
            }

            // Add the server socket to the epoll instance
            AddToEpoll(_server_fd);

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
                    if (events[i].data.fd == _server_fd) { 
                        // New connection event
                        _thread_pool.Push([this, &handler] { HandleNewConnection(handler); });
                    } else { 
                        // Event on existing connection
                        _thread_pool.Push([this, &handler, fd = events[i].data.fd] { HandleConnectionUpdate(fd, handler); });
                    }
                }
            }
        }

    private:

        // -- Threaded Functions --

        template<typename H>
        void HandleNewConnection(H &handler) {
            // Accept the connection
            sockaddr_in client_addr{};
            socklen_t client_addr_len = sizeof(client_addr);
            const int client_socket = accept(_server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len);

            // Check if the connection was accepted successfully
            if (client_socket == -1) {
                return handler.OnError(client_addr, {"Failed to accept a new connection", Error::Kind::Accept});
            }

            // Make the client socket non-blocking and add it to the epoll instance
            try {
                MakeSocketNonBlocking(client_socket);
                AddToEpoll(client_socket);
            } catch (const Error &e) {
                // Close the connection
                close(client_socket);

                // Call the Handler
                return handler.OnError(client_addr, e);
            }

            // Call the Handler
            std::vector<std::byte> out_buf;
            const bool keep_alive = handler.OnNew(client_addr, out_buf);

            // Write the response to the client
            try {
                Write(client_socket, out_buf);
            } catch (const Error &e) {
                // Close the connection
                close(client_socket);

                // Call the Handler
                return handler.OnError(client_addr, e);
            }

            // Close the connection if the handler has requested it
            if (!keep_alive) {
                // Close the connection
                close(client_socket);
            }
        }

        template<typename Handler>
        void HandleConnectionUpdate(const int client_socket, Handler &handler) {
            // Read the message
            std::vector<std::byte> in_buf(_buf_size);
            const ssize_t n = read(client_socket, in_buf.data(), in_buf.size());

            if (n == -1) { 
                // Error

                // Get the client address
                const auto client_addr = GetClientAddress(client_socket);

                // Close the connection 
                close(client_socket);

                // Call the Handler
                handler.OnError(client_addr, {"Failed to read from client.", Error::Kind::Read});
            } else if (n == 0) { 
                // The client has closed the connection
                
                // Get the client address
                const auto client_addr = GetClientAddress(client_socket);
                
                // Close the connection
                close(client_socket);

                // Call the Handler
                handler.OnClose(client_addr);
            } else {
                // The client has sent a message
                
                // Get the client address
                const auto client_addr = GetClientAddress(client_socket);
                
                // Set up the buffer for the write operation
                std::vector<std::byte> out_buf;
                
                // Call the Handler
                const bool keep_alive = handler.OnRead(client_addr, in_buf, out_buf);
                
                // Write the response to the client
                try {
                    // Write the response to the client
                    Write(client_socket, out_buf);
                } catch (const Error &e) {
                    // Close the connection
                    close(client_socket);

                    // Call the Handler
                    return handler.OnError(client_addr, e);
                }
                
                // Close the connection if the handler has requested it
                if (!keep_alive) {
                    // Close the connection
                    close(client_socket);
                }
            }
        }

        // -- Helper Methods --

        void AddToEpoll(const int client_socket) const {
            // Add the client socket to the epoll instance
            epoll_event client_event = {.events = EPOLLIN, .data = {.fd = client_socket}};
            if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_socket, &client_event) == -1) {
                throw Error("Failed to add socket to epoll instance.", Error::Kind::EpollAdd);
            }
        }

        // -- Helper Static Methods --

        static void Write(int client_socket, const std::vector<std::byte> &out_buf) {
            if (!out_buf.empty()) {
                if (write(client_socket, out_buf.data(), out_buf.size()) == -1) {
                    throw Error("Failed to write response.", Error::Kind::Write);
                }
            }
        }

        static void MakeSocketNonBlocking(int fd) {
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags == -1) {
                throw Error("Failed to get socket flags.", Error::Kind::SocketCreation);
            }

            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                throw Error("Failed to set socket flags.", Error::Kind::SocketCreation);
            }
        }

        static sockaddr_in GetClientAddress(int client_fd) {
            sockaddr_in client_addr{};
            socklen_t client_addr_len = sizeof(client_addr);
            if (getpeername(client_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len) == -1) {
                return {}; // Ignore errors
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
