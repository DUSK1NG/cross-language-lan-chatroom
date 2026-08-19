#include "protocol.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

#include <openssl/err.h>

namespace protocol {
namespace {

thread_local std::string g_last_error;

void set_error(const std::string& error) {
    g_last_error = error;
}

std::string ssl_error(const char* operation) {
    const unsigned long code = ERR_get_error();
    if (code == 0) return operation;
    char buffer[256]{};
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return std::string(operation) + ": " + buffer;
}

}  // namespace

bool send_all(SOCKET socket_handle, const char* data, std::size_t length) {
    std::size_t total_sent = 0;
    while (total_sent < length) {
        const std::size_t remaining = length - total_sent;
        const int chunk_size = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int sent = send(socket_handle, data + total_sent, chunk_size, 0);
        if (sent == SOCKET_ERROR || sent == 0) {
            set_error("send failed: WSA error " + std::to_string(WSAGetLastError()));
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
        if (received == SOCKET_ERROR) {
            set_error("recv failed: WSA error " + std::to_string(WSAGetLastError()));
            return false;
        }
        if (received == 0) {
            set_error("peer closed the TCP connection");
            return false;
        }
        total_received += static_cast<std::size_t>(received);
    }
    return true;
}

bool send_frame(SOCKET socket_handle, const std::string& payload) {
    if (payload.empty() || payload.size() > kMaxMessageSize) {
        set_error("invalid outgoing payload length");
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
        set_error("invalid incoming payload length: " + std::to_string(payload_length));
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
        if (sent <= 0) {
            set_error(ssl_error("SSL_write failed"));
            return false;
        }
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
        if (received <= 0) {
            const int ssl_result = SSL_get_error(ssl_handle, received);
            set_error(ssl_error(
                (ssl_result == SSL_ERROR_ZERO_RETURN)
                    ? "TLS peer closed the connection"
                    : "SSL_read failed"));
            return false;
        }
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
    if (payload_length == 0 || payload_length > kMaxMessageSize) {
        set_error("invalid incoming TLS payload length: " + std::to_string(payload_length));
        return false;
    }
    payload.resize(payload_length);
    return recv_all(ssl_handle, payload.data(), payload.size());
}

std::string last_error() {
    return g_last_error;
}

}  // namespace protocol
