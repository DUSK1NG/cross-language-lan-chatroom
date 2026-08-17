#include <winsock2.h>
#include <ws2tcpip.h>

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

    constexpr char message[] = "Hello";
    const int sent = send(socket_handle, message, sizeof(message) - 1, 0);
    if (sent == SOCKET_ERROR) {
        print_winsock_error("send");
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    char response[1024]{};
    const int received = recv(socket_handle, response, sizeof(response) - 1, 0);
    if (received == SOCKET_ERROR) {
        print_winsock_error("recv");
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    if (received == 0) {
        std::cerr << "Server closed the connection before replying.\n";
        closesocket(socket_handle);
        WSACleanup();
        return 1;
    }

    std::cout << "Server replied: " << std::string(response, received) << '\n';

    closesocket(socket_handle);
    WSACleanup();
    return 0;
}
