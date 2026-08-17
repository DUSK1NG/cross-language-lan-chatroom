#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "message.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kDefaultServerIp = "127.0.0.1";
constexpr int kDefaultServerPort = 8888;
constexpr const char* kDefaultUsername = "Alice";
constexpr const char* kDefaultUserCode = "ALICE001";
constexpr DWORD kConsolePollIntervalMs = 100;
constexpr DWORD kInputBufferSize = 256;

enum class InputReadResult {
    kLineReady,
    kInterrupted,
    kEndOfInput,
    kError,
};

void print_winsock_error(const char* operation) {
    std::cerr << operation << " failed. WSA error: " << WSAGetLastError() << '\n';
}

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

std::string format_identity(const message::Message& incoming_message) {
    if (incoming_message.username.empty()) {
        return {};
    }
    if (incoming_message.user_code.empty()) {
        return incoming_message.username;
    }
    return incoming_message.username + "#" + incoming_message.user_code;
}

bool is_console_input(HANDLE input_handle) {
    if (input_handle == INVALID_HANDLE_VALUE || input_handle == nullptr) {
        return false;
    }

    DWORD mode = 0;
    return GetConsoleMode(input_handle, &mode) != 0;
}

InputReadResult read_console_line(
    HANDLE input_handle,
    std::atomic<bool>& running,
    std::wstring& pending_input,
    std::wstring& line) {
    line.clear();

    while (running.load()) {
        const std::size_t newline_position = pending_input.find(L'\n');
        if (newline_position != std::wstring::npos) {
            line = pending_input.substr(0, newline_position);
            pending_input.erase(0, newline_position + 1);
            if (!line.empty() && line.back() == L'\r') {
                line.pop_back();
            }
            return InputReadResult::kLineReady;
        }

        const DWORD wait_result = WaitForSingleObject(input_handle, kConsolePollIntervalMs);
        if (wait_result == WAIT_TIMEOUT) {
            continue;
        }
        if (wait_result != WAIT_OBJECT_0) {
            return InputReadResult::kError;
        }

        wchar_t buffer[kInputBufferSize]{};
        DWORD chars_read = 0;
        if (!ReadConsoleW(
                input_handle,
                buffer,
                kInputBufferSize - 1,
                &chars_read,
                nullptr)) {
            return InputReadResult::kError;
        }

        if (chars_read == 0) {
            return InputReadResult::kEndOfInput;
        }

        pending_input.append(buffer, buffer + chars_read);
    }

    return InputReadResult::kInterrupted;
}

InputReadResult read_next_input_line(
    HANDLE input_handle,
    bool use_console_input,
    std::atomic<bool>& running,
    std::wstring& pending_input,
    std::wstring& line) {
    if (use_console_input) {
        return read_console_line(input_handle, running, pending_input, line);
    }

    if (!std::getline(std::wcin, line)) {
        return InputReadResult::kEndOfInput;
    }
    return InputReadResult::kLineReady;
}

void shutdown_socket(SOCKET socket_handle) {
    if (socket_handle != INVALID_SOCKET) {
        shutdown(socket_handle, SD_BOTH);
    }
}

void print_help(std::mutex& output_mutex) {
    print_locked(
        output_mutex,
        "Commands:\n"
        "/help  Show this help\n"
        "/users Show online users\n"
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

void receive_loop(SOCKET socket_handle, std::atomic<bool>& running, std::mutex& output_mutex) {
    while (running.load()) {
        message::Message incoming_message;
        if (!message::receive_message(socket_handle, incoming_message)) {
            if (running.load()) {
                print_locked(output_mutex, "Connection to server lost.\n", std::cerr);
            }
            running.store(false);
            shutdown_socket(socket_handle);
            return;
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

    const std::string server_ip = argc >= 2 ? utf8_from_wide(argv[1]) : kDefaultServerIp;
    const std::string username = argc >= 4
        ? utf8_from_wide(argv[3])
        : kDefaultUsername;
    const std::string user_code = argc >= 5
        ? utf8_from_wide(argv[4])
        : kDefaultUserCode;

    int server_port = kDefaultServerPort;
    if (argc >= 3) {
        try {
            server_port = std::stoi(utf8_from_wide(argv[2]));
        } catch (const std::exception&) {
            std::cerr << "Invalid port.\n";
            return 1;
        }
    }

    WSADATA wsa_data{};
    const int startup_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (startup_result != 0) {
        std::cerr << "WSAStartup failed. Error: " << startup_result << '\n';
        return 1;
    }

    SOCKET socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_handle == INVALID_SOCKET) {
        print_winsock_error("socket");
        WSACleanup();
        return 1;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(static_cast<u_short>(server_port));

    const int address_result = inet_pton(
        AF_INET, server_ip.c_str(), &server_address.sin_addr);
    if (address_result != 1) {
        if (address_result == 0) {
            std::cerr << "Invalid IPv4 address: " << server_ip << '\n';
        } else {
            print_winsock_error("inet_pton");
        }
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    if (connect(
            socket_handle,
            reinterpret_cast<const sockaddr*>(&server_address),
            sizeof(server_address)) == SOCKET_ERROR) {
        print_winsock_error("connect");
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to " << server_ip << ':' << server_port << '\n';

    const message::Message login{"login", username, user_code, ""};
    if (!message::send_message(socket_handle, login)) {
        std::cerr << "Failed to send login message.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    message::Message login_response;
    if (!message::receive_message(socket_handle, login_response)) {
        std::cerr << "Failed to receive login response.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }
    if (login_response.type != "login_ok") {
        std::cerr << "Login failed: " << login_response.content << '\n';
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    std::cout << "Logged in as "
              << format_identity(login_response) << '\n';

    std::atomic<bool> running{true};
    std::mutex output_mutex;

    print_help(output_mutex);

    std::thread receiver_thread(
        receive_loop,
        socket_handle,
        std::ref(running),
        std::ref(output_mutex));

    const HANDLE input_handle = GetStdHandle(STD_INPUT_HANDLE);
    const bool use_console_input = is_console_input(input_handle);
    std::wstring pending_input;
    bool shutdown_requested = false;

    while (running.load()) {
        std::wstring input_line_wide;
        const InputReadResult read_result = read_next_input_line(
            input_handle,
            use_console_input,
            running,
            pending_input,
            input_line_wide);

        if (read_result == InputReadResult::kInterrupted) {
            break;
        }

        if (read_result == InputReadResult::kError) {
            print_locked(output_mutex, "Failed to read input.\n", std::cerr);
            running.store(false);
            shutdown_socket(socket_handle);
            shutdown_requested = true;
            break;
        }

        if (read_result == InputReadResult::kEndOfInput) {
            running.store(false);
            shutdown_socket(socket_handle);
            shutdown_requested = true;
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

        if (!input_line.empty() && input_line.front() == '/') {
            if (input_line == "/users") {
                const message::Message users_request{"users_request", "", "", ""};
                if (!message::send_message(socket_handle, users_request)) {
                    print_locked(output_mutex, "Failed to request online users.\n", std::cerr);
                    running.store(false);
                    shutdown_socket(socket_handle);
                    shutdown_requested = true;
                    break;
                }
                continue;
            }

            if (input_line == "/quit") {
                const message::Message quit_message{"quit", "", "", ""};
                if (!message::send_message(socket_handle, quit_message)) {
                    print_locked(output_mutex, "Failed to send quit message.\n", std::cerr);
                }
                running.store(false);
                shutdown_socket(socket_handle);
                shutdown_requested = true;
                break;
            }

            print_locked(output_mutex, "Unknown command. Type /help for help.\n", std::cerr);
            continue;
        }

        const message::Message chat_message{"chat", "", "", input_line};
        if (!message::send_message(socket_handle, chat_message)) {
            print_locked(output_mutex, "Failed to send chat message.\n", std::cerr);
            running.store(false);
            shutdown_socket(socket_handle);
            shutdown_requested = true;
            break;
        }
    }

    running.store(false);
    if (!shutdown_requested) {
        shutdown_socket(socket_handle);
    }

    if (receiver_thread.joinable()) {
        receiver_thread.join();
    }

    closesocket(socket_handle);
    WSACleanup();
    return 0;
}
