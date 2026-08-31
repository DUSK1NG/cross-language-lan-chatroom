#include "message.hpp"

#include "json.hpp"

#include <utility>
#include <cstdint>

namespace message {
namespace {

thread_local std::string g_last_error;

void set_error(const std::string& error) {
    g_last_error = error;
}

bool is_base64(const std::string& value) {
    if (value.empty() || value.size() % 4 != 0) return false;
    std::size_t padding = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        const char character = value[index];
        const bool alphabet = (character >= 'A' && character <= 'Z') ||
                              (character >= 'a' && character <= 'z') ||
                              (character >= '0' && character <= '9') || character == '+' || character == '/';
        if (character == '=') {
            ++padding;
            if (index < value.size() - 2 || padding > 2) return false;
        } else if (!alphabet || padding != 0) {
            return false;
        }
    }
    return true;
}

nlohmann::json serialize(const Message& message) {
    nlohmann::json object = nlohmann::json{{"type", message.type}};
    if (!message.protocol_version.empty()) object["protocol_version"] = message.protocol_version;
    if (!message.message_id.empty()) object["message_id"] = message.message_id;
    if (!message.command_id.empty()) object["command_id"] = message.command_id;
    if (!message.delivery_state.empty()) object["delivery_state"] = message.delivery_state;
    if (!message.created_at.empty()) object["created_at"] = message.created_at;
    if (!message.before_message_id.empty()) object["before_message_id"] = message.before_message_id;
    if (!message.search_query.empty()) object["search_query"] = message.search_query;
    if (message.limit > 0) object["limit"] = message.limit;
    if (message.has_more) object["has_more"] = true;
    if (message.recalled) object["recalled"] = true;
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
    if (message.is_admin) object["is_admin"] = true;
    if (message.is_private) object["private"] = true;
    if (!message.group_id.empty()) object["group_id"] = message.group_id;
    if (message.epoch != 0) object["epoch"] = message.epoch;
    if (!message.key_package.empty()) object["key_package"] = message.key_package;
    if (!message.proposal_id.empty()) object["proposal_id"] = message.proposal_id;
    if (!message.proposal.empty()) object["proposal"] = message.proposal;
    if (!message.commit.empty()) object["commit"] = message.commit;
    if (!message.welcome.empty()) object["welcome"] = message.welcome;
    if (!message.messages.empty()) {
        object["messages"] = nlohmann::json::array();
        for (const Message& nested : message.messages) {
            object["messages"].push_back(serialize(nested));
        }
    }
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
            !read_string("protocol_version", parsed.protocol_version) ||
            !read_string("message_id", parsed.message_id) ||
            !read_string("command_id", parsed.command_id) ||
            !read_string("delivery_state", parsed.delivery_state) ||
            !read_string("user_code", parsed.user_code) ||
            !read_string("target_user_code", parsed.target_user_code) ||
            !read_string("room", parsed.room) ||
            !read_string("content", parsed.content) ||
            !read_string("group_id", parsed.group_id) ||
            !read_string("key_package", parsed.key_package) ||
            !read_string("proposal_id", parsed.proposal_id) ||
            !read_string("proposal", parsed.proposal) ||
            !read_string("commit", parsed.commit) ||
            !read_string("welcome", parsed.welcome) ||
            !read_string("created_at", parsed.created_at) ||
            !read_string("before_message_id", parsed.before_message_id) ||
            !read_string("search_query", parsed.search_query)) {
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
        if (object.contains("has_more")) {
            if (!object.at("has_more").is_boolean()) {
                set_error("has_more is not a boolean");
                return false;
            }
            parsed.has_more = object.at("has_more").get<bool>();
        }
        if (object.contains("recalled")) {
            if (!object.at("recalled").is_boolean()) {
                set_error("recalled is not a boolean");
                return false;
            }
            parsed.recalled = object.at("recalled").get<bool>();
        }
        if (object.contains("limit")) {
            if (!object.at("limit").is_number_integer()) {
                set_error("limit is not an integer");
                return false;
            }
            parsed.limit = object.at("limit").get<int>();
        }
        if (object.contains("epoch")) {
            if (!object.at("epoch").is_number_unsigned()) {
                set_error("epoch is not an unsigned integer");
                return false;
            }
            parsed.epoch = object.at("epoch").get<std::uint64_t>();
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

        if (object.contains("messages")) {
            if (!object.at("messages").is_array()) {
                set_error("messages is not an array");
                return false;
            }
            for (const auto& value : object.at("messages")) {
                if (!value.is_object()) {
                    set_error("messages contains a non-object value");
                    return false;
                }
                Message nested;
                const auto read_nested_string = [&value](const char* key, std::string& destination) {
                    if (!value.contains(key)) return true;
                    if (!value.at(key).is_string()) return false;
                    destination = value.at(key).get<std::string>();
                    return true;
                };
                if (!read_nested_string("type", nested.type) ||
                    !read_nested_string("message_id", nested.message_id) ||
                    !read_nested_string("delivery_state", nested.delivery_state) ||
                    !read_nested_string("username", nested.username) ||
                    !read_nested_string("user_code", nested.user_code) ||
                    !read_nested_string("target_user_code", nested.target_user_code) ||
                    !read_nested_string("room", nested.room) ||
                    !read_nested_string("content", nested.content) ||
                    !read_nested_string("created_at", nested.created_at)) {
                    set_error("messages contains a field with the wrong type");
                    return false;
                }
                if (value.contains("private")) {
                    if (!value.at("private").is_boolean()) {
                        set_error("nested private is not a boolean");
                        return false;
                    }
                    nested.is_private = value.at("private").get<bool>();
                }
                if (value.contains("recalled")) {
                    if (!value.at("recalled").is_boolean()) {
                        set_error("nested recalled is not a boolean");
                        return false;
                    }
                    nested.recalled = value.at("recalled").get<bool>();
                }
                parsed.messages.push_back(std::move(nested));
            }
        }

        if (parsed.type == "mls.key_package.publish" && !parsed.key_package.empty() && !is_base64(parsed.key_package)) {
            set_error("key_package is not base64");
            return false;
        }
        if (parsed.type == "mls.key_package.fetch" && !parsed.key_package.empty() && !is_base64(parsed.key_package)) {
            set_error("key_package is not base64");
            return false;
        }
        if (parsed.type == "mls.group.commit" && !parsed.commit.empty() && !is_base64(parsed.commit)) {
            set_error("commit is not base64");
            return false;
        }
        if (parsed.type == "mls.group.proposal" && !parsed.proposal.empty() && !is_base64(parsed.proposal)) {
            set_error("proposal is not base64");
            return false;
        }
        if (parsed.type == "mls.group.welcome" && !parsed.welcome.empty() && !is_base64(parsed.welcome)) {
            set_error("welcome is not base64");
            return false;
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
