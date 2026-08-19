#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "command.hpp"
#include "auth.hpp"
#include "connection.hpp"
#include "message.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

class HostServerProcess {
public:
    bool start(const auth::ClientOptions& options, std::string& error) {
        std::wstring command = quote(to_wide(options.server_exe)) + L" -cert " +
            quote(to_wide(options.cert_file)) + L" -key " + quote(to_wide(options.key_file)) +
            L" -admin-code " + quote(to_wide(options.user_code));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> command_buffer(command.begin(), command.end());
        command_buffer.push_back(L'\0');
        if (!CreateProcessW(nullptr, command_buffer.data(), nullptr, nullptr, FALSE,
                            CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process)) {
            error = "Failed to start Go server process. Check --server-exe, --cert and --key.";
            return false;
        }
        CloseHandle(process.hThread);
        process_handle_ = process.hProcess;
        Sleep(700);
        return true;
    }

    void stop() {
        if (process_handle_ == nullptr) return;
        TerminateProcess(process_handle_, 0);
        WaitForSingleObject(process_handle_, 3000);
        CloseHandle(process_handle_);
        process_handle_ = nullptr;
    }

    ~HostServerProcess() { stop(); }

private:
    static std::wstring to_wide(const std::string& value) {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                            result.data(), size);
        return result;
    }

    static std::wstring quote(const std::wstring& value) {
        return L"\"" + value + L"\"";
    }

    HANDLE process_handle_ = nullptr;
};

constexpr DWORD kConsolePollIntervalMs = 100;
constexpr DWORD kInputBufferSize = 256;
constexpr DWORD kConsoleEventBufferSize = 32;

enum class InputReadResult {
    kLineReady,
    kInterrupted,
    kEndOfInput,
    kError,
};

void print_locked(std::mutex& output_mutex, const std::string& text, std::ostream& stream = std::cout) {
    const std::lock_guard<std::mutex> lock(output_mutex);
    stream << text;
    stream.flush();
}

std::string utf8_from_wide(const wchar_t* value) {
    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value,
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0) {
        return {};
    }

    std::string result(static_cast<std::size_t>(required_size), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value,
            -1,
            result.data(),
            required_size,
            nullptr,
            nullptr) == 0) {
        return {};
    }
    result.resize(static_cast<std::size_t>(required_size - 1));
    return result;
}

std::wstring wide_from_utf8(const std::string& value, bool& valid) {
    valid = true;
    if (value.empty()) {
        return {};
    }

    const int value_size = static_cast<int>(value.size());
    const int required_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        value_size,
        nullptr,
        0);
    if (required_size <= 0) {
        valid = false;
        return {};
    }

    std::wstring result(static_cast<std::size_t>(required_size), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            value_size,
            result.data(),
            required_size) != required_size) {
        valid = false;
        return {};
    }
    return result;
}

std::string format_identity(const message::Message& incoming_message) {
    if (incoming_message.username.empty()) {
        return {};
    }
    if (incoming_message.user_code.empty()) {
        return incoming_message.username;
    }
    return incoming_message.username + "#" + incoming_message.user_code;
}

template <typename MessageType, typename = void>
struct has_target_user_code : std::false_type {};

template <typename MessageType>
struct has_target_user_code<
    MessageType,
    std::void_t<decltype(std::declval<MessageType&>().target_user_code)>>
    : std::true_type {};

template <typename MessageType>
std::string get_target_user_code(const MessageType& message) {
    if constexpr (has_target_user_code<MessageType>::value) {
        return message.target_user_code;
    }
    return {};
}

template <typename MessageType>
void set_target_user_code(MessageType& message, const std::string& target_user_code) {
    if constexpr (has_target_user_code<MessageType>::value) {
        message.target_user_code = target_user_code;
    }
}

std::string normalize_user_code(const std::string& user_code) {
    std::string normalized = user_code;
    for (char& value : normalized) {
        if (value >= 'A' && value <= 'Z') {
            value = static_cast<char>(value - 'A' + 'a');
        }
    }
    return normalized;
}

bool is_console_input(HANDLE input_handle) {
    if (input_handle == INVALID_HANDLE_VALUE || input_handle == nullptr) {
        return false;
    }

    DWORD mode = 0;
    return GetConsoleMode(input_handle, &mode) != 0;
}

class ConsoleInputModeGuard {
public:
    bool activate(HANDLE input_handle) {
        if (active_) {
            return true;
        }

        DWORD original_mode = 0;
        if (GetConsoleMode(input_handle, &original_mode) == 0) {
            return false;
        }

        const DWORD event_mode = original_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        if (SetConsoleMode(input_handle, event_mode) == 0) {
            return false;
        }

        input_handle_ = input_handle;
        original_mode_ = original_mode;
        active_ = true;
        return true;
    }

    void restore() {
        if (!active_) {
            return;
        }
        SetConsoleMode(input_handle_, original_mode_);
        active_ = false;
    }

    ~ConsoleInputModeGuard() {
        restore();
    }

private:
    HANDLE input_handle_ = INVALID_HANDLE_VALUE;
    DWORD original_mode_ = 0;
    bool active_ = false;
};

void write_console_locked(
    HANDLE output_handle,
    std::mutex& output_mutex,
    const wchar_t* text,
    std::size_t text_length) {
    if (output_handle == INVALID_HANDLE_VALUE || output_handle == nullptr || text_length == 0) {
        return;
    }

    DWORD output_mode = 0;
    if (GetConsoleMode(output_handle, &output_mode) == 0) {
        return;
    }

    const std::lock_guard<std::mutex> lock(output_mutex);
    DWORD chars_written = 0;
    WriteConsoleW(
        output_handle,
        text,
        static_cast<DWORD>(text_length),
        &chars_written,
        nullptr);
}

bool is_high_surrogate(wchar_t value) {
    return value >= 0xD800 && value <= 0xDBFF;
}

std::size_t erase_last_utf16_code_point(std::wstring& input) {
    if (input.empty()) {
        return 0;
    }

    input.pop_back();
    std::size_t removed_count = 1;
    if (!input.empty() && is_high_surrogate(input.back())) {
        input.pop_back();
        removed_count = 2;
    }
    return removed_count;
}

InputReadResult read_console_line(
    HANDLE input_handle,
    HANDLE output_handle,
    std::atomic<bool>& running,
    std::mutex& output_mutex,
    std::wstring& current_input,
    std::wstring& line) {
    line.clear();

    while (running.load()) {
        const DWORD wait_result = WaitForSingleObject(input_handle, kConsolePollIntervalMs);
        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }
        if (wait_result != WAIT_OBJECT_0) {
            return InputReadResult::kError;
        }

        INPUT_RECORD records[kConsoleEventBufferSize]{};
        DWORD records_read = 0;
        if (!ReadConsoleInputW(
                input_handle,
                records,
                kConsoleEventBufferSize,
                &records_read)) {
            return InputReadResult::kError;
        }

        for (DWORD index = 0; index < records_read; ++index) {
            if (running.load() == false || records[index].EventType != KEY_EVENT) {
                continue;
            }

            const KEY_EVENT_RECORD& key_event = records[index].Event.KeyEvent;
            if (key_event.bKeyDown == FALSE) {
                continue;
            }

            if (key_event.wVirtualKeyCode == VK_RETURN) {
                write_console_locked(output_handle, output_mutex, L"\r\n", 2);
                line = current_input;
                current_input.clear();
                return InputReadResult::kLineReady;
            }

            if (key_event.wVirtualKeyCode == VK_BACK) {
                const std::size_t removed_count = erase_last_utf16_code_point(current_input);
                for (std::size_t removed = 0; removed < removed_count; ++removed) {
                    write_console_locked(output_handle, output_mutex, L"\b \b", 3);
                }
                continue;
            }

            const wchar_t unicode_character = key_event.uChar.UnicodeChar;
            if (unicode_character >= L' ' && unicode_character != 0x7F) {
                current_input.push_back(unicode_character);
                write_console_locked(output_handle, output_mutex, &unicode_character, 1);
            }
        }
    }

    return InputReadResult::kInterrupted;
}

InputReadResult read_redirected_pipe_line(
    HANDLE input_handle,
    std::atomic<bool>& running,
    std::string& pending_input,
    std::wstring& line) {
    line.clear();

    while (running.load()) {
        const std::size_t newline_position = pending_input.find('\n');
        if (newline_position != std::string::npos) {
            std::string utf8_line = pending_input.substr(0, newline_position);
            pending_input.erase(0, newline_position + 1);
            if (!utf8_line.empty() && utf8_line.back() == '\r') {
                utf8_line.pop_back();
            }

            bool valid_utf8 = false;
            line = wide_from_utf8(utf8_line, valid_utf8);
            return valid_utf8 ? InputReadResult::kLineReady : InputReadResult::kError;
        }

        const DWORD wait_result = WaitForSingleObject(input_handle, kConsolePollIntervalMs);
        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }
        if (wait_result != WAIT_OBJECT_0) {
            return InputReadResult::kError;
        }

        char buffer[kInputBufferSize]{};
        DWORD bytes_read = 0;
        if (!ReadFile(input_handle, buffer, kInputBufferSize, &bytes_read, nullptr)) {
            return GetLastError() == ERROR_BROKEN_PIPE
                ? InputReadResult::kEndOfInput
                : InputReadResult::kError;
        }
        if (bytes_read == 0) {
            return InputReadResult::kEndOfInput;
        }
        pending_input.append(buffer, buffer + bytes_read);
    }

    return InputReadResult::kInterrupted;
}

InputReadResult read_next_input_line(
    HANDLE input_handle,
    bool use_console_input,
    std::atomic<bool>& running,
    HANDLE output_handle,
    std::mutex& output_mutex,
    std::wstring& console_input,
    std::string& redirected_input,
    std::wstring& line) {
    if (use_console_input) {
        return read_console_line(
            input_handle,
            output_handle,
            running,
            output_mutex,
            console_input,
            line);
    }

    if (input_handle != INVALID_HANDLE_VALUE && GetFileType(input_handle) == FILE_TYPE_PIPE) {
        return read_redirected_pipe_line(input_handle, running, redirected_input, line);
    }

    if (!std::getline(std::wcin, line)) {
        return InputReadResult::kEndOfInput;
    }
    return InputReadResult::kLineReady;
}

void print_help(std::mutex& output_mutex) {
    print_locked(
        output_mutex,
        "Commands:\n"
        "/help  Show this help\n"
        "/users Show online users\n"
        "/rooms Show available rooms\n"
        "/kick Name#Code  Kick a user (administrator)\n"
        "/mute Name#Code  Toggle mute (administrator)\n"
        "/join room_name  Join or create a room\n"
        "/leave Return to lobby\n"
        "/msg Name#Code message  Send a private message\n"
        "/quit  Exit the chat\n");
}

void print_users_response(const message::Message& incoming_message, std::mutex& output_mutex) {
    std::string output = "Online Users:\n";
    for (std::size_t index = 0; index < incoming_message.users.size(); ++index) {
        output += std::to_string(index + 1);
        output += ". ";
        output += incoming_message.users[index];
        output += '\n';
    }
    print_locked(output_mutex, output);
}

void print_room_list_response(
    const message::Message& incoming_message,
    std::mutex& output_mutex) {
    std::string output = "Rooms:\n";
    const auto& rooms = incoming_message.rooms.empty()
        ? incoming_message.users
        : incoming_message.rooms;
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        output += std::to_string(index + 1) + ". " + rooms[index] + '\n';
    }
    print_locked(output_mutex, output);
}

void receive_loop(
    connection::ConnectionState& connection_state,
    std::atomic<bool>& running,
    std::atomic<bool>& reconnect_enabled,
    std::mutex& output_mutex,
    const std::string& local_user_code,
    std::unordered_map<std::string, std::string>& private_peer_names,
    std::mutex& private_peer_mutex) {
    connection::ReconnectBackoff backoff;
    while (running.load()) {
        message::Message incoming_message;
        if (!connection_state.receive(incoming_message)) {
            connection_state.close_current();
            if (!running.load() || !reconnect_enabled.load()) {
                return;
            }

            print_locked(output_mutex, "Connection to server lost.\n", std::cerr);
            while (running.load() && reconnect_enabled.load()) {
                print_locked(output_mutex, "Reconnecting...\n");
                const std::chrono::seconds delay = backoff.next_delay();
                if (!connection_state.wait_before_retry(
                        delay, running, reconnect_enabled)) {
                    return;
                }

                message::Message login_response;
                connection::LoginResult login_result =
                    connection::LoginResult::kRetryableFailure;
                if (connection_state.connect_and_login(login_response, login_result)) {
                    backoff.reset();
                    print_locked(output_mutex, "Reconnected and logged in.\n");
                    break;
                }

                if (login_result == connection::LoginResult::kRejected) {
                    print_locked(
                        output_mutex,
                        "Login rejected after reconnect: " +
                            login_response.content + "\n",
                        std::cerr);
                    reconnect_enabled.store(false);
                    running.store(false);
                    connection_state.request_stop();
                    return;
                }
            }
            continue;
        }

        if (incoming_message.type == "chat") {
            const std::string identity = format_identity(incoming_message);
            if (!identity.empty()) {
                print_locked(output_mutex, identity + ": " + incoming_message.content + "\n");
            } else {
                print_locked(output_mutex, incoming_message.content + "\n");
            }
            continue;
        }

        if (incoming_message.type == "system") {
            const std::string identity = format_identity(incoming_message);
            if (!identity.empty()) {
                print_locked(
                    output_mutex,
                    "[System] " + identity + ' ' + incoming_message.content + "\n");
            } else {
                print_locked(output_mutex, "[System] " + incoming_message.content + "\n");
            }
            continue;
        }

        if (incoming_message.type == "users_response") {
            print_users_response(incoming_message, output_mutex);
            continue;
        }

        if (incoming_message.type == "room_list_response" ||
            incoming_message.type == "rooms_response") {
            print_room_list_response(incoming_message, output_mutex);
            continue;
        }

        if (incoming_message.type == "private_chat") {
            const std::string sender_code = normalize_user_code(incoming_message.user_code);
            const bool sent_by_local_user =
                !sender_code.empty() && sender_code == normalize_user_code(local_user_code);
            std::string peer_identity;
            if (sent_by_local_user) {
                const std::string target_code = get_target_user_code(incoming_message);
                {
                    const std::lock_guard<std::mutex> lock(private_peer_mutex);
                    const auto peer = private_peer_names.find(normalize_user_code(target_code));
                    if (peer != private_peer_names.end()) {
                        peer_identity = peer->second;
                    }
                }
                if (peer_identity.empty()) {
                    peer_identity = "#" + target_code;
                }
                print_locked(
                    output_mutex,
                    "[Private -> " + peer_identity + "] " + incoming_message.content + "\n");
            } else {
                peer_identity = format_identity(incoming_message);
                if (peer_identity.empty()) {
                    peer_identity = incoming_message.username;
                }
                print_locked(
                    output_mutex,
                    "[Private from " + peer_identity + "] " + incoming_message.content + "\n");
            }
            continue;
        }

        if (incoming_message.type == "offline_message") {
            const std::string identity = format_identity(incoming_message);
            print_locked(output_mutex,
                "[Offline private from " + (identity.empty() ? incoming_message.username : identity) + "] " +
                    incoming_message.content + "\n");
            continue;
        }

        if (incoming_message.type == "error") {
            print_locked(output_mutex, "Server error: " + incoming_message.content + "\n", std::cerr);
            continue;
        }

        if (incoming_message.type == "login_error") {
            print_locked(
                output_mutex,
                "Unexpected login_error after login: " + incoming_message.content + "\n",
                std::cerr);
            continue;
        }

        print_locked(
            output_mutex,
            "Received " + incoming_message.type + ": " + incoming_message.content + "\n");
    }
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::vector<std::string> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
    for (int index = 1; index < argc; ++index) {
        arguments.push_back(utf8_from_wide(argv[index]));
    }

    auth::ClientOptions client_options;
    std::string argument_error;
    if (!auth::parse_arguments(arguments, client_options, argument_error)) {
        std::cerr << argument_error << '\n' << auth::usage() << '\n';
        return 1;
    }

    HostServerProcess host_server;
    if (client_options.host_mode) {
        std::string host_error;
        if (!host_server.start(client_options, host_error)) {
            std::cerr << host_error << '\n';
            return 1;
        }
        if (client_options.ca_file.empty()) {
            client_options.ca_file = client_options.cert_file;
        }
        std::cout << "本机聊天已启动，其他用户请连接本机局域网 IP。\n";
    }

    const std::string& user_code = client_options.user_code;

    WSADATA wsa_data{};
    const int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        std::cerr << "WSAStartup failed. Error: " << startup_result << '\n';
        return 1;
    }

    connection::ConnectionState connection_state(
        connection::Config{
            client_options.server_ip,
            client_options.server_port,
            client_options.username,
            client_options.user_code,
            client_options.ca_file,
            client_options.password,
            client_options.register_account});
    message::Message login_response;
    connection::LoginResult login_result = connection::LoginResult::kRetryableFailure;
    if (!connection_state.connect_and_login(login_response, login_result)) {
        if (login_result == connection::LoginResult::kRejected) {
            std::cerr << "Login failed: " << login_response.content << '\n';
        } else {
            std::cerr << "Failed to connect or receive login response: "
                      << connection_state.last_error() << '\n';
        }
        WSACleanup();
        return 1;
    }

    std::cout << "Logged in as "
              << format_identity(login_response)
              << (login_response.is_admin ? " (administrator)" : "") << '\n';

    std::atomic<bool> running{true};
    std::atomic<bool> reconnect_enabled{true};
    std::mutex output_mutex;
    std::unordered_map<std::string, std::string> private_peer_names;
    std::mutex private_peer_mutex;

    print_help(output_mutex);

    const auto send_or_report = [&](const message::Message& outgoing_message,
                                    const char* error_text) {
        if (connection_state.send(outgoing_message)) {
            return true;
        }
        print_locked(output_mutex, std::string(error_text) + "\n", std::cerr);
        connection_state.request_disconnect();
        return false;
    };

    std::thread receiver_thread(
        receive_loop,
        std::ref(connection_state),
        std::ref(running),
        std::ref(reconnect_enabled),
        std::ref(output_mutex),
        std::cref(user_code),
        std::ref(private_peer_names),
        std::ref(private_peer_mutex));

    const HANDLE input_handle = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool use_console_input = is_console_input(input_handle);
    ConsoleInputModeGuard console_input_mode;
    const bool console_input_ready = !use_console_input || console_input_mode.activate(input_handle);
    std::wstring console_input;
    std::string redirected_input;
    if (!console_input_ready) {
        print_locked(output_mutex, "Failed to configure console input.\n", std::cerr);
        reconnect_enabled.store(false);
        running.store(false);
        connection_state.request_stop();
    }

    while (console_input_ready && running.load()) {
        std::wstring input_line_wide;
        const InputReadResult read_result = read_next_input_line(
            input_handle,
            use_console_input,
            running,
            output_handle,
            output_mutex,
            console_input,
            redirected_input,
            input_line_wide);

        if (read_result == InputReadResult::kInterrupted) {
            break;
        }

        if (read_result == InputReadResult::kError) {
            print_locked(output_mutex, "Failed to read input.\n", std::cerr);
            reconnect_enabled.store(false);
            running.store(false);
            connection_state.request_stop();
            break;
        }

        if (read_result == InputReadResult::kEndOfInput) {
            reconnect_enabled.store(false);
            running.store(false);
            connection_state.request_stop();
            break;
        }

        std::string input_line = utf8_from_wide(input_line_wide.c_str());
        if (input_line_wide.empty()) {
            input_line.clear();
        }
        if (input_line.empty()) {
            continue;
        }

        if (input_line == "/help") {
            print_help(output_mutex);
            continue;
        }

        if (input_line.rfind("/kick ", 0) == 0 || input_line.rfind("/mute ", 0) == 0) {
            const std::string action = input_line.substr(1, 4);
            const std::string target = input_line.substr(6);
            const std::size_t separator = target.find('#');
            if (separator == std::string::npos || separator == 0 || separator + 1 >= target.size() ||
                target.find_first_of(" \t\r\n", separator + 1) != std::string::npos) {
                print_locked(output_mutex, "Usage: /" + action + " Name#Code\n", std::cerr);
                continue;
            }
            message::Message admin_request{
                "admin_action", "", "", action, {}, target.substr(separator + 1)};
            send_or_report(admin_request, "Failed to send administrator command.");
            continue;
        }

        if (!input_line.empty() && input_line.front() == '/') {
            if (input_line == "/users") {
                const message::Message users_request{"users_request", "", "", "", {}, ""};
                send_or_report(users_request, "Failed to request online users.");
                continue;
            }

            if (input_line == "/rooms") {
                const message::Message rooms_request{"rooms_request", "", "", "", {}, ""};
                send_or_report(rooms_request, "Failed to request rooms.");
                continue;
            }

            if (input_line == "/leave") {
                const message::Message leave_message{"room_leave", "", "", "", {}, ""};
                send_or_report(leave_message, "Failed to leave room.");
                continue;
            }

            if (command::is_join_command(input_line)) {
                command::RoomCommand room_command;
                if (!command::parse_join_command(input_line, room_command)) {
                    print_locked(
                        output_mutex,
                        "Invalid room name. Use ASCII letters, digits, or underscore; length 1-32.\n",
                        std::cerr);
                    continue;
                }
                message::Message join_message{
                    "room_join", "", "", room_command.room_name, {}, ""};
                join_message.room = room_command.room_name;
                send_or_report(join_message, "Failed to join room.");
                continue;
            }

            if (input_line == "/quit") {
                const message::Message quit_message{"quit", "", "", "", {}, ""};
                if (!connection_state.send(quit_message)) {
                    print_locked(output_mutex, "Failed to send quit message.\n", std::cerr);
                }
                reconnect_enabled.store(false);
                running.store(false);
                connection_state.request_stop();
                break;
            }

            if (command::is_private_message_command(input_line)) {
                command::PrivateMessageCommand private_command;
                if (!command::parse_private_message(input_line, private_command)) {
                    print_locked(
                        output_mutex,
                        "Invalid private message. Use: /msg Name#Code message\n",
                        std::cerr);
                    continue;
                }

                message::Message private_message{
                    "private_chat", "", "", private_command.content, {},
                    private_command.target_user_code};

                {
                    const std::lock_guard<std::mutex> lock(private_peer_mutex);
                    private_peer_names[normalize_user_code(private_command.target_user_code)] =
                        private_command.target_name + "#" + private_command.target_user_code;
                }
                set_target_user_code(private_message, private_command.target_user_code);
                send_or_report(private_message, "Failed to send private message.");

                continue;
            }

            print_locked(output_mutex, "Unknown command. Type /help for help.\n", std::cerr);
            continue;
        }

        const message::Message chat_message{"chat", "", "", input_line, {}, ""};
        send_or_report(chat_message, "Failed to send chat message.");
    }

    console_input_mode.restore();
    reconnect_enabled.store(false);
    running.store(false);
    connection_state.request_stop();

    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    connection_state.close_current();
    WSACleanup();
    return 0;
}
