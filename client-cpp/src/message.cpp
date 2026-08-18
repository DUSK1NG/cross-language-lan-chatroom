#include "message.hpp"

#include "json.hpp"

#include <utility>

namespace message {
namespace {

nlohmann::json serialize(const Message& message) {
    nlohmann::json object = nlohmann::json{{"type", message.type}};
    if (!message.username.empty()) object["username"] = message.username;
    if (!message.user_code.empty()) object["user_code"] = message.user_code;
    if (!message.target_user_code.empty()) object["target_user_code"] = message.target_user_code;
    if (!message.room.empty()) object["room"] = message.room;
    if (!message.content.empty()) object["content"] = message.content;
    if (!message.users.empty()) object["users"] = message.users;
    if (!message.rooms.empty()) object["rooms"] = message.rooms;
    if (!message.password.empty()) object["password"] = message.password;
    if (message.history_limit > 0) object["history_limit"] = message.history_limit;
    if (message.is_admin) object["is_admin"] = true;
    return object;
}

template <typename ReceiveFrame>
bool receive_message_impl(ReceiveFrame receive_frame, Message& message) {
    message = Message{};
    std::string payload;
    if (!receive_frame(payload)) return false;

    try {
        const nlohmann::json object = nlohmann::json::parse(payload);
        if (!object.contains("type") || !object.at("type").is_string()) return false;

        Message parsed;
        parsed.type = object.at("type").get<std::string>();
        const auto read_string = [&object](const char* key, std::string& destination) {
            if (!object.contains(key)) return true;
            if (!object.at(key).is_string()) return false;
            destination = object.at(key).get<std::string>();
            return true;
        };
        if (!read_string("username", parsed.username) ||
            !read_string("user_code", parsed.user_code) ||
            !read_string("target_user_code", parsed.target_user_code) ||
            !read_string("room", parsed.room) ||
            !read_string("content", parsed.content) ||
            !read_string("password", parsed.password)) return false;
        if (object.contains("history_limit")) {
            if (!object.at("history_limit").is_number_integer()) return false;
            parsed.history_limit = object.at("history_limit").get<int>();
        }
        if (object.contains("is_admin")) {
            if (!object.at("is_admin").is_boolean()) return false;
            parsed.is_admin = object.at("is_admin").get<bool>();
        }

        if (object.contains("users")) {
            if (!object.at("users").is_array()) return false;
            for (const auto& value : object.at("users")) {
                if (!value.is_string()) return false;
                parsed.users.push_back(value.get<std::string>());
            }
        }
        if (object.contains("rooms")) {
            if (!object.at("rooms").is_array()) return false;
            for (const auto& value : object.at("rooms")) {
                if (!value.is_string()) return false;
                parsed.rooms.push_back(value.get<std::string>());
            }
        }

        message = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

}  // namespace

bool send_message(SOCKET socket_handle, const Message& message) {
    return protocol::send_frame(socket_handle, serialize(message).dump());
}

bool receive_message(SOCKET socket_handle, Message& message) {
    return receive_message_impl(
        [socket_handle](std::string& payload) {
            return protocol::recv_frame(socket_handle, payload);
        },
        message);
}

bool send_message(SSL* ssl_handle, const Message& message) {
    return protocol::send_frame(ssl_handle, serialize(message).dump());
}

bool receive_message(SSL* ssl_handle, Message& message) {
    return receive_message_impl(
        [ssl_handle](std::string& payload) {
            return protocol::recv_frame(ssl_handle, payload);
        },
        message);
}

}  // namespace message
