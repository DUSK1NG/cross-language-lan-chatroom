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

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    const std::string server_ip = argc >= 2 ? utf8_from_wide(argv[1]) : kDefaultServerIp;
    const int server_port = argc >= 3
        ? std::stoi(utf8_from_wide(argv[2]))
        : kDefaultServerPort;
    const std::string username = argc >= 4
        ? utf8_from_wide(argv[3])
        : kDefaultUsername;
    const std::string chat_content = argc >= 5
        ? utf8_from_wide(argv[4])
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

    const message::Message login{"login", username, ""};
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

    std::cout << "Logged in as " << username << '\n';

    const message::Message chat{"chat", "", chat_content};
    if (!message::send_message(socket_handle, chat)) {
        std::cerr << "Failed to send chat message.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    message::Message chat_response;
    if (!message::receive_message(socket_handle, chat_response)) {
        std::cerr << "Failed to receive chat response.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }
    if (chat_response.type != "chat") {
        std::cerr << "Unexpected chat response: " << chat_response.content << '\n';
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    std::cout << "Server echoed: " << chat_response.content << '\n';

    closesocket(socket_handle);
    WSACleanup();
    return 0;
}
