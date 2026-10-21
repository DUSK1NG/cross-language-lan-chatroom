#pragma once

// Cross-platform socket compatibility shim.
//
// The client was originally written against the Win32 Winsock API. The headless
// "core" CI build (and any Linux/macOS build) needs the same source to compile
// against the POSIX sockets API. This header provides the handful of Winsock
// type and function names used by the networking code as thin aliases over
// their POSIX equivalents, so the rest of the code can stay unchanged.

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>

#else  // POSIX

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

// Winsock represents sockets as an unsigned handle; POSIX uses an int fd.
using SOCKET = int;

// Win32 spells the IPv4 address struct IN_ADDR; POSIX uses struct in_addr.
using IN_ADDR = struct in_addr;

#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR (-1)
#endif

#ifndef SD_BOTH
#define SD_BOTH SHUT_RDWR
#endif

// closesocket()/WSAGetLastError() map directly onto POSIX close()/errno.
inline int closesocket(SOCKET socket_handle) { return ::close(socket_handle); }
inline int WSAGetLastError() { return errno; }

// Winsock requires explicit library start-up/tear-down; POSIX needs none.
// Provide no-op equivalents so the same init code compiles everywhere.
using WSADATA = int;
inline int WSAStartup(unsigned short /*version*/, WSADATA* data) {
    if (data) *data = 0;
    return 0;
}
inline int WSACleanup() { return 0; }
#ifndef MAKEWORD
#define MAKEWORD(low, high) \
    static_cast<unsigned short>(((low) & 0xff) | (((high) & 0xff) << 8))
#endif

using u_short = unsigned short;

#endif  // _WIN32
