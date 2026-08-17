#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "message.hpp"

#include <iostream>
#include <string>

namespace {

constexpr const char* kDefaultServerIp = "127.0.0.1";
constexpr int kDefaultServerPort = 8888;
constexpr const char* kDefaultUsername = "Alice";
constexpr const char* kDefaultUserCode = "ALICE001";
constexpr const char* kDefaultChatContent = "Hello from C++";

void print_winsock_error(const char* operation) {
    std::cerr << operation << " failed. WSA error: " << WSAGetLastError() << '\n';
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

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    const std::string server_ip = argc >= 2 ? utf8_from_wide(argv[1]) : kDefaultServerIp;
    const int server_port = argc >= 3
        ? std::stoi(utf8_from_wide(argv[2]))
        : kDefaultServerPort;
    const std::string username = argc >= 4
        ? utf8_from_wide(argv[3])
        : kDefaultUsername;
    const std::string user_code = argc >= 5
        ? utf8_from_wide(argv[4])
        : kDefaultUserCode;
    const std::string chat_content = argc >= 6
        ? utf8_from_wide(argv[5])
        : kDefaultChatContent;

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

    const std::string expected_username = login_response.username;
    const std::string expected_user_code = login_response.user_code;

    const message::Message chat{"chat", "", "", chat_content};
    if (!message::send_message(socket_handle, chat)) {
        std::cerr << "Failed to send chat message.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    while (true) {
        message::Message incoming_message;
        if (!message::receive_message(socket_handle, incoming_message)) {
            std::cerr << "Failed to receive chat response.\n";
            closesocket(socket_handle);
            WSACleanup();
            return 1;
        }

        if (incoming_message.type == "system") {
            const std::string identity = format_identity(incoming_message);
            if (!identity.empty()) {
                std::cout << "[System] " << identity << ' ' << incoming_message.content << '\n';
            } else {
                std::cout << "[System] " << incoming_message.content << '\n';
            }
            continue;
        }

        if (incoming_message.type == "chat") {
            const std::string identity = format_identity(incoming_message);
            if (!identity.empty()) {
                std::cout << identity << ": " << incoming_message.content << '\n';
            } else {
                std::cout << incoming_message.content << '\n';
            }

            if (incoming_message.username == expected_username &&
                incoming_message.user_code == expected_user_code &&
                incoming_message.content == chat_content) {
                break;
            }
            continue;
        }

        if (incoming_message.type == "error" || incoming_message.type == "login_error") {
            std::cerr << "Server error: " << incoming_message.content << '\n';
            closesocket(socket_handle);
            WSACleanup();
            return 1;
        }

        std::cout << "Received " << incoming_message.type
                  << ": " << incoming_message.content << '\n';
    }

    closesocket(socket_handle);
    WSACleanup();
    return 0;
}
