#include "message.hpp"

#include "json.hpp"

#include <utility>

namespace message {
namespace {

thread_local std::string g_last_error;

void set_error(const std::string& error) {
    g_last_error = error;
}

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
    if (message.is_admin) object["is_admin"] = true;
    return object;
}

template <typename ReceiveFrame>
bool receive_message_impl(ReceiveFrame receive_frame, Message& message) {
    message = Message{};
    std::string payload;
    if (!receive_frame(payload)) {
        set_error(protocol::last_error());
        return false;
    }

    try {
        const nlohmann::json object = nlohmann::json::parse(payload);
        if (!object.contains("type") || !object.at("type").is_string()) {
            set_error("JSON message has no string type field");
            return false;
        }

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
            !read_string("password", parsed.password)) {
            set_error("JSON message contains a field with the wrong type");
            return false;
        }
        if (object.contains("is_admin")) {
            if (!object.at("is_admin").is_boolean()) {
                set_error("is_admin is not a boolean");
                return false;
            }
            parsed.is_admin = object.at("is_admin").get<bool>();
        }

        if (object.contains("users")) {
            if (!object.at("users").is_array()) {
                set_error("users is not an array");
                return false;
            }
            for (const auto& value : object.at("users")) {
                if (!value.is_string()) {
                    set_error("users contains a non-string value");
                    return false;
                }
                parsed.users.push_back(value.get<std::string>());
            }
        }
        if (object.contains("rooms")) {
            if (!object.at("rooms").is_array()) {
                set_error("rooms is not an array");
                return false;
            }
            for (const auto& value : object.at("rooms")) {
                if (!value.is_string()) {
                    set_error("rooms contains a non-string value");
                    return false;
                }
                parsed.rooms.push_back(value.get<std::string>());
            }
        }

        message = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception& error) {
        set_error(std::string("JSON parse failed: ") + error.what());
        return false;
    }
}

}  // namespace

bool send_message(SOCKET socket_handle, const Message& message) {
    const bool sent = protocol::send_frame(socket_handle, serialize(message).dump());
    if (!sent) set_error(protocol::last_error());
    return sent;
}

bool receive_message(SOCKET socket_handle, Message& message) {
    return receive_message_impl(
        [socket_handle](std::string& payload) {
            return protocol::recv_frame(socket_handle, payload);
        },
        message);
}

bool send_message(SSL* ssl_handle, const Message& message) {
    const bool sent = protocol::send_frame(ssl_handle, serialize(message).dump());
    if (!sent) set_error(protocol::last_error());
    return sent;
}

bool receive_message(SSL* ssl_handle, Message& message) {
    return receive_message_impl(
        [ssl_handle](std::string& payload) {
            return protocol::recv_frame(ssl_handle, payload);
        },
        message);
}

std::string last_error() {
    return g_last_error;
}

}  // namespace message
