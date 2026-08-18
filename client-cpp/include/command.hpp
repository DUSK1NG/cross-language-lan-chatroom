#pragma once

#include <string>

namespace command {

struct PrivateMessageCommand {
    std::string target_name;
    std::string target_user_code;
    std::string content;
};

struct RoomCommand {
    std::string room_name;
};

// Parse /msg Name#Code message. Invalid input returns false.
bool is_private_message_command(const std::string& input);
bool parse_private_message(
    const std::string& input,
    PrivateMessageCommand& command);

bool is_rooms_command(const std::string& input);
bool is_leave_command(const std::string& input);
bool is_join_command(const std::string& input);
bool parse_join_command(const std::string& input, RoomCommand& command);

}  // namespace command
