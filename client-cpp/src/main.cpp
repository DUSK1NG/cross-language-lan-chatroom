#include <winsock2.h>
#include <ws2tcpip.h>

#include "protocol.hpp"

#include <iostream>
#include <string>

namespace {

constexpr const char* kDefaultServerIp = "127.0.0.1";
constexpr int kDefaultServerPort = 8888;

void print_winsock_error(const char* operation) {
    std::cerr << operation << " failed. WSA error: " << WSAGetLastError() << '\n';
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string server_ip = argc >= 2 ? argv[1] : kDefaultServerIp;
    const int server_port = argc >= 3 ? std::stoi(argv[2]) : kDefaultServerPort;

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

    if (!protocol::send_frame(socket_handle, "Hello") ||
        !protocol::send_frame(socket_handle, "World")) {
        std::cerr << "Failed to send framed message.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    for (int i = 0; i < 2; ++i) {
        std::string response;
        if (!protocol::recv_frame(socket_handle, response)) {
            std::cerr << "Failed to receive framed response.\n";
            closesocket(socket_handle);
            WSACleanup();
            return 1;
        }
        std::cout << "Server replied: " << response << '\n';
    }

    closesocket(socket_handle);
    WSACleanup();
    return 0;
}
