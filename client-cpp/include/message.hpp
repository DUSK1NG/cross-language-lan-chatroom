#pragma once

#include "protocol.hpp"

#include <string>
#include <utility>
#include <vector>

namespace message {

struct Message {
    Message(
        std::string type_value = {},
        std::string username_value = {},
        std::string user_code_value = {},
        std::string content_value = {},
        std::vector<std::string> users_value = {},
        std::string target_user_code_value = {},
        std::string room_value = {},
        std::vector<std::string> rooms_value = {},
        std::string password_value = {},
        int history_limit_value = 0)
        : type(std::move(type_value)),
          username(std::move(username_value)),
          user_code(std::move(user_code_value)),
          content(std::move(content_value)),
          users(std::move(users_value)),
          target_user_code(std::move(target_user_code_value)),
          room(std::move(room_value)),
          rooms(std::move(rooms_value)),
          password(std::move(password_value)),
          history_limit(history_limit_value) {}

    std::string type;
    std::string username;
    std::string user_code;
    std::string content;
    std::vector<std::string> users{};
    std::string target_user_code;
    std::string room;
    std::vector<std::string> rooms{};
    std::string password;
    int history_limit = 0;
};

bool send_message(SOCKET socket_handle, const Message& message);
bool receive_message(SOCKET socket_handle, Message& message);
bool send_message(SSL* ssl_handle, const Message& message);
bool receive_message(SSL* ssl_handle, Message& message);

}  // namespace message
