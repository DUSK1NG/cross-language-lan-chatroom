#include "auth.hpp"

#include <charconv>
#include <random>

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

std::string make_guest_code() {
    static constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device random;
    std::uniform_int_distribution<std::size_t> distribution(0, sizeof(alphabet) - 2);
    std::string code = "GUEST";
    for (int index = 0; index < 6; ++index) code += alphabet[distribution(random)];
    return code;
}

}  // namespace

std::string usage() {
    return "Usage: chat-client.exe [server-ip] [port] [username] [user-code] "
           "[--password password] [--register] [--ca-file path]\n"
           "       chat-client.exe --guest [server-ip] [port] [username] [--ca-file path]\n"
           "       chat-client.exe --host [username] [--server-exe path] [--cert path] [--key path]";
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
        if (argument == "--guest") {
            options.guest_mode = true;
            options.user_code.clear();
            continue;
        }
        if (argument == "--host") {
            options.host_mode = true;
            options.guest_mode = true;
            options.user_code.clear();
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
        if (argument == "--server-exe") {
            if (!take_value(args, index, options.server_exe, "--server-exe", error)) return false;
            continue;
        }
        if (argument == "--cert") {
            if (!take_value(args, index, options.cert_file, "--cert", error)) return false;
            continue;
        }
        if (argument == "--key") {
            if (!take_value(args, index, options.key_file, "--key", error)) return false;
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

    if (options.host_mode) {
        if (positional_count == 0) {
            options.username = "Host";
        } else if (positional_count == 1) {
            options.username = options.server_ip;
            options.server_ip = "127.0.0.1";
        } else {
            error = "Host mode accepts only an optional username.";
            return false;
        }
        options.server_port = 8888;
    } else if (options.guest_mode) {
        if (positional_count != 3) {
            error = "Guest mode requires: --guest server-ip port username";
            return false;
        }
    } else if (positional_count < 4) {
        error = "Server mode requires: server-ip port username user-code";
        return false;
    }

    if (options.guest_mode && options.user_code.empty()) {
        options.user_code = make_guest_code();
    }

    if (options.register_account && options.password.empty()) {
        error = "--register requires --password.";
        return false;
    }
    if (options.guest_mode && (options.register_account || !options.password.empty())) {
        error = "Guest/host mode does not use account passwords.";
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
