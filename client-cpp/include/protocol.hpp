#pragma once

#include <winsock2.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace protocol {

constexpr std::uint32_t kMaxMessageSize = 64 * 1024;

bool send_all(SOCKET socket_handle, const char* data, std::size_t length);
bool recv_all(SOCKET socket_handle, char* data, std::size_t length);
bool send_frame(SOCKET socket_handle, const std::string& payload);
bool recv_frame(SOCKET socket_handle, std::string& payload);

}  // namespace protocol
