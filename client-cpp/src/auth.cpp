#include "auth.hpp"

#include <charconv>

namespace auth {
namespace {

bool parse_port(const std::string& value, int& port) {
    if (value.empty()) return false;
    int parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end || parsed < 1 || parsed > 65535) {
        return false;
    }
    port = parsed;
    return true;
}

bool take_value(
    const std::vector<std::string>& args,
    std::size_t& index,
    std::string& destination,
    const char* option,
    std::string& error) {
    if (index + 1 >= args.size() || args[index + 1].empty()) {
        error = std::string(option) + " requires a non-empty value.";
        return false;
    }
    destination = args[++index];
    return true;
}

}  // namespace

std::string usage() {
    return "Usage: chat-client.exe [server-ip] [port] [username] [user-code] "
           "[--password password] [--register] [--ca-file path]";
}

bool parse_arguments(
    const std::vector<std::string>& args,
    ClientOptions& options,
    std::string& error) {
    options = ClientOptions{};
    error.clear();

    std::size_t positional_count = 0;
    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string& argument = args[index];
        if (argument == "--register") {
            options.register_account = true;
            continue;
        }
        if (argument == "--password") {
            if (!take_value(args, index, options.password, "--password", error)) return false;
            continue;
        }
        if (argument == "--ca-file") {
            if (!take_value(args, index, options.ca_file, "--ca-file", error)) return false;
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            error = "Unknown option: " + argument;
            return false;
        }

        switch (positional_count++) {
        case 0: options.server_ip = argument; break;
        case 1:
            if (!parse_port(argument, options.server_port)) {
                error = "Invalid port.";
                return false;
            }
            break;
        case 2: options.username = argument; break;
        case 3: options.user_code = argument; break;
        default:
            error = "Too many positional arguments.";
            return false;
        }
    }

    if (options.register_account && options.password.empty()) {
        error = "--register requires --password.";
        return false;
    }
    return true;
}

message::Message make_register_message(const ClientOptions& options) {
    return message::Message{
        "register", options.username, options.user_code, "", {}, "", "", {},
        options.password};
}

message::Message make_login_message(const ClientOptions& options) {
    const std::string type = options.password.empty() ? "login" : "login_auth";
    return message::Message{
        type, options.username, options.user_code, "", {}, "", "", {}, options.password};
}

}  // namespace auth
