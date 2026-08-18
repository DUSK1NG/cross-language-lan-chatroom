#include "protocol.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace protocol {

bool send_all(SOCKET socket_handle, const char* data, std::size_t length) {
    std::size_t total_sent = 0;
    while (total_sent < length) {
        const std::size_t remaining = length - total_sent;
        const int chunk_size = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int sent = send(socket_handle, data + total_sent, chunk_size, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }
    return true;
}

bool recv_all(SOCKET socket_handle, char* data, std::size_t length) {
    std::size_t total_received = 0;
    while (total_received < length) {
        const std::size_t remaining = length - total_received;
        const int chunk_size = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int received = recv(socket_handle, data + total_received, chunk_size, 0);
        if (received == SOCKET_ERROR || received == 0) {
            return false;
        }
        total_received += static_cast<std::size_t>(received);
    }
    return true;
}

bool send_frame(SOCKET socket_handle, const std::string& payload) {
    if (payload.empty() || payload.size() > kMaxMessageSize) {
        return false;
    }

    const std::uint32_t network_length = htonl(
        static_cast<std::uint32_t>(payload.size()));
    if (!send_all(
            socket_handle,
            reinterpret_cast<const char*>(&network_length),
            sizeof(network_length))) {
        return false;
    }

    return send_all(socket_handle, payload.data(), payload.size());
}

bool recv_frame(SOCKET socket_handle, std::string& payload) {
    std::uint32_t network_length = 0;
    if (!recv_all(
            socket_handle,
            reinterpret_cast<char*>(&network_length),
            sizeof(network_length))) {
        return false;
    }

    const std::uint32_t payload_length = ntohl(network_length);
    if (payload_length == 0 || payload_length > kMaxMessageSize) {
        return false;
    }

    payload.resize(payload_length);
    return recv_all(socket_handle, payload.data(), payload.size());
}

bool send_all(SSL* ssl_handle, const char* data, std::size_t length) {
    std::size_t total_sent = 0;
    while (total_sent < length) {
        const std::size_t remaining = length - total_sent;
        const int chunk_size = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int sent = SSL_write(ssl_handle, data + total_sent, chunk_size);
        if (sent <= 0) return false;
        total_sent += static_cast<std::size_t>(sent);
    }
    return true;
}

bool recv_all(SSL* ssl_handle, char* data, std::size_t length) {
    std::size_t total_received = 0;
    while (total_received < length) {
        const std::size_t remaining = length - total_received;
        const int chunk_size = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int received = SSL_read(ssl_handle, data + total_received, chunk_size);
        if (received <= 0) return false;
        total_received += static_cast<std::size_t>(received);
    }
    return true;
}

bool send_frame(SSL* ssl_handle, const std::string& payload) {
    if (payload.empty() || payload.size() > kMaxMessageSize) return false;
    const std::uint32_t network_length = htonl(
        static_cast<std::uint32_t>(payload.size()));
    return send_all(
               ssl_handle,
               reinterpret_cast<const char*>(&network_length),
               sizeof(network_length)) &&
        send_all(ssl_handle, payload.data(), payload.size());
}

bool recv_frame(SSL* ssl_handle, std::string& payload) {
    std::uint32_t network_length = 0;
    if (!recv_all(
            ssl_handle,
            reinterpret_cast<char*>(&network_length),
            sizeof(network_length))) return false;
    const std::uint32_t payload_length = ntohl(network_length);
    if (payload_length == 0 || payload_length > kMaxMessageSize) return false;
    payload.resize(payload_length);
    return recv_all(ssl_handle, payload.data(), payload.size());
}

}  // namespace protocol
