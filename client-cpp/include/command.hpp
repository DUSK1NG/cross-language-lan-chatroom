#pragma once

#include <string>

namespace command {

struct PrivateMessageCommand {
    std::string target_name;
    std::string target_user_code;
    std::string content;
};

// Parse /msg Name#Code message. Invalid input returns false.
bool is_private_message_command(const std::string& input);
bool parse_private_message(
    const std::string& input,
    PrivateMessageCommand& command);

}  // namespace command
