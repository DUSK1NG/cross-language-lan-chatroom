#include "command.hpp"

#include <cctype>

namespace command {
namespace {

bool is_command_prefix(const std::string& input) {
    return input.size() >= 4 && input.compare(0, 4, "/msg") == 0;
}

bool is_ascii_alphanumeric(char value) {
    const unsigned char character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0;
}

bool is_valid_code(const std::string& code) {
    if (code.empty()) {
        return false;
    }
    for (const char value : code) {
        if (!is_ascii_alphanumeric(value)) {
            return false;
        }
    }
    return true;
}

std::size_t skip_spaces(const std::string& input, std::size_t position) {
    while (position < input.size() &&
           std::isspace(static_cast<unsigned char>(input[position])) != 0) {
        ++position;
    }
    return position;
}

}  // namespace

bool is_private_message_command(const std::string& input) {
    return input == "/msg" ||
        (is_command_prefix(input) &&
         input.size() > 4 &&
         std::isspace(static_cast<unsigned char>(input[4])) != 0);
}

bool parse_private_message(
    const std::string& input,
    PrivateMessageCommand& parsed_command) {
    parsed_command = PrivateMessageCommand{};
    if (!is_private_message_command(input)) {
        return false;
    }

    const std::size_t target_begin = skip_spaces(input, 5);
    if (target_begin >= input.size()) {
        return false;
    }

    const std::size_t target_end = input.find_first_of(" \t\r\n", target_begin);
    const std::size_t target_length =
        target_end == std::string::npos ? input.size() - target_begin : target_end - target_begin;
    const std::string target = input.substr(target_begin, target_length);
    if (target.empty()) {
        return false;
    }

    const std::size_t separator = target.find('#');
    if (separator == std::string::npos ||
        separator == 0 ||
        separator + 1 >= target.size() ||
        target.find('#', separator + 1) != std::string::npos) {
        return false;
    }

    const std::string target_name = target.substr(0, separator);
    const std::string target_user_code = target.substr(separator + 1);
    if (!is_valid_code(target_user_code)) {
        return false;
    }

    if (target_end == std::string::npos) {
        return false;
    }
    const std::size_t content_begin = skip_spaces(input, target_end);
    if (content_begin == input.size()) {
        return false;
    }

    parsed_command.target_name = target_name;
    parsed_command.target_user_code = target_user_code;
    parsed_command.content = input.substr(content_begin);
    return true;
}

}  // namespace command
