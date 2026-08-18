#include "message.hpp"

#include "json.hpp"

#include <utility>

namespace message {

bool send_message(SOCKET socket_handle, const Message& message) {
    nlohmann::json object = nlohmann::json{{"type", message.type}};
    if (!message.username.empty()) {
        object["username"] = message.username;
    }
    if (!message.user_code.empty()) {
        object["user_code"] = message.user_code;
    }
    if (!message.target_user_code.empty()) {
        object["target_user_code"] = message.target_user_code;
    }
    if (!message.content.empty()) {
        object["content"] = message.content;
    }
    if (!message.users.empty()) {
        object["users"] = message.users;
    }

    return protocol::send_frame(socket_handle, object.dump());
}

bool receive_message(SOCKET socket_handle, Message& message) {
    // 失败时也保证输出对象不暴露调用前的旧消息内容。
    message = Message{};

    std::string payload;
    if (!protocol::recv_frame(socket_handle, payload)) {
        return false;
    }

    try {
        const nlohmann::json object = nlohmann::json::parse(payload);
        if (!object.contains("type") || !object.at("type").is_string()) {
            return false;
        }

        Message parsed;
        parsed.type = object.at("type").get<std::string>();

        if (object.contains("username")) {
            if (!object.at("username").is_string()) {
                return false;
            }
            parsed.username = object.at("username").get<std::string>();
        }

        if (object.contains("user_code")) {
            if (!object.at("user_code").is_string()) {
                return false;
            }
            parsed.user_code = object.at("user_code").get<std::string>();
        }

        if (object.contains("target_user_code")) {
            if (!object.at("target_user_code").is_string()) {
                return false;
            }
            parsed.target_user_code = object.at("target_user_code").get<std::string>();
        }

        if (object.contains("content")) {
            if (!object.at("content").is_string()) {
                return false;
            }
            parsed.content = object.at("content").get<std::string>();
        }

        if (object.contains("users")) {
            if (!object.at("users").is_array()) {
                return false;
            }
            for (const auto& user_value : object.at("users")) {
                if (!user_value.is_string()) {
                    return false;
                }
                parsed.users.push_back(user_value.get<std::string>());
            }
        }

        message = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

}  // namespace message
