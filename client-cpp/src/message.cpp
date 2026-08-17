#include "message.hpp"

#include "json.hpp"

#include <utility>

namespace message {

bool send_message(SOCKET socket_handle, const Message& message) {
    nlohmann::json object = nlohmann::json{{"type", message.type}};
    if (!message.username.empty()) {
        object["username"] = message.username;
    }
    if (!message.content.empty()) {
        object["content"] = message.content;
    }

    return protocol::send_frame(socket_handle, object.dump());
}

bool receive_message(SOCKET socket_handle, Message& message) {
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

        if (object.contains("content")) {
            if (!object.at("content").is_string()) {
                return false;
            }
            parsed.content = object.at("content").get<std::string>();
        }

        message = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

}  // namespace message
