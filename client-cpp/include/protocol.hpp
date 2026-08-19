#pragma once

#include <winsock2.h>

#include <openssl/ssl.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace protocol {

constexpr std::uint32_t kMaxMessageSize = 64 * 1024;

bool send_all(SOCKET socket_handle, const char* data, std::size_t length);
bool recv_all(SOCKET socket_handle, char* data, std::size_t length);
bool send_frame(SOCKET socket_handle, const std::string& payload);
bool recv_frame(SOCKET socket_handle, std::string& payload);
bool send_all(SSL* ssl_handle, const char* data, std::size_t length);
bool recv_all(SSL* ssl_handle, char* data, std::size_t length);
bool send_frame(SSL* ssl_handle, const std::string& payload);
bool recv_frame(SSL* ssl_handle, std::string& payload);
std::string last_error();

}  // namespace protocol
