#pragma once

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
    /// @brief Error while getting the client address.
    GetAddress,
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
  [[nodiscard]] Error(const std::string &msg, Kind kind)
      : std::runtime_error(msg), _kind(kind) {}

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
 * @brief Writes the given data to the given socket.
 * @param client_fd The client socket.
 * @param buf The buffer.
 */
void Write(int client_fd, const std::vector<std::byte> &buf) {
  if (!buf.empty()) {
    if (write(client_fd, buf.data(), buf.size()) == -1) {
      throw Error("Failed to write response.", Error::Kind::Write);
    }
  }
}

/**
 * @brief Gets the client address.
 * @param client_fd The client socket.
 * @return
 */
[[nodiscard]] sockaddr_in GetClientAddress(int client_fd) {
  sockaddr_in client_addr{};
  socklen_t client_addr_len = sizeof(client_addr);
  if (getpeername(client_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_addr_len) == -1) {
    throw Error("Failed to get client address.", Error::Kind::GetAddress);
  }
  return client_addr;
}
}  // namespace tcp