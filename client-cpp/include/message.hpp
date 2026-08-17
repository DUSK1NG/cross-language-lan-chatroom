#pragma once

#include "protocol.hpp"

#include <string>
#include <vector>

namespace message {

struct Message {
    std::string type;
    std::string username;
    std::string user_code;
    std::string content;
    std::vector<std::string> users{};
};

bool send_message(SOCKET socket_handle, const Message& message);
bool receive_message(SOCKET socket_handle, Message& message);

}  // namespace message
