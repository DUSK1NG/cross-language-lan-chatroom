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
    if (!message.message_id.empty()) object["message_id"] = message.message_id;
    if (!message.username.empty()) object["username"] = message.username;
    if (!message.user_code.empty()) object["user_code"] = message.user_code;
    if (!message.target_user_code.empty()) object["target_user_code"] = message.target_user_code;
    if (!message.room.empty()) object["room"] = message.room;
    if (!message.content.empty()) object["content"] = message.content;
    if (!message.users.empty()) object["users"] = message.users;
    if (!message.rooms.empty()) object["rooms"] = message.rooms;
    if (!message.user_details.empty()) {
        object["user_details"] = nlohmann::json::array();
        for (const OnlineUser& user : message.user_details) {
            object["user_details"].push_back({{"username", user.username},
                                               {"user_code", user.user_code},
                                               {"room", user.room},
                                               {"is_admin", user.is_admin}});
        }
    }
    if (!message.room_details.empty()) {
        object["room_details"] = nlohmann::json::array();
        for (const RoomInfo& room : message.room_details) {
            nlohmann::json value{{"name", room.name}, {"private", room.is_private},
                                 {"can_manage", room.can_manage}};
            if (!room.owner_code.empty()) value["owner_code"] = room.owner_code;
            object["room_details"].push_back(std::move(value));
        }
    }
    if (!message.password.empty()) object["password"] = message.password;
    if (message.is_admin) object["is_admin"] = true;
    if (message.is_private) object["private"] = true;
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
            !read_string("message_id", parsed.message_id) ||
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
        if (object.contains("private")) {
            if (!object.at("private").is_boolean()) {
                set_error("private is not a boolean");
                return false;
            }
            parsed.is_private = object.at("private").get<bool>();
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
        if (object.contains("user_details")) {
            if (!object.at("user_details").is_array()) {
                set_error("user_details is not an array");
                return false;
            }
            for (const auto& value : object.at("user_details")) {
                if (!value.is_object() || !value.contains("username") || !value.contains("user_code") ||
                    !value.contains("room") || !value.contains("is_admin") ||
                    !value.at("username").is_string() || !value.at("user_code").is_string() ||
                    !value.at("room").is_string() || !value.at("is_admin").is_boolean()) {
                    set_error("user_details contains an invalid member");
                    return false;
                }
                parsed.user_details.push_back({value.at("username").get<std::string>(),
                                               value.at("user_code").get<std::string>(),
                                               value.at("room").get<std::string>(),
                                               value.at("is_admin").get<bool>()});
            }
        }
        if (object.contains("room_details")) {
            if (!object.at("room_details").is_array()) {
                set_error("room_details is not an array");
                return false;
            }
            for (const auto& value : object.at("room_details")) {
                if (!value.is_object() || !value.contains("name") || !value.contains("private") ||
                    !value.contains("can_manage") || !value.at("name").is_string() ||
                    !value.at("private").is_boolean() || !value.at("can_manage").is_boolean() ||
                    (value.contains("owner_code") && !value.at("owner_code").is_string())) {
                    set_error("room_details contains an invalid room");
                    return false;
                }
                RoomInfo room{value.at("name").get<std::string>(), "",
                              value.at("private").get<bool>(), value.at("can_manage").get<bool>()};
                if (value.contains("owner_code")) room.owner_code = value.at("owner_code").get<std::string>();
                parsed.room_details.push_back(std::move(room));
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
