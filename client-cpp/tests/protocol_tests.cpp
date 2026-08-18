#include "message.hpp"
#include "protocol.hpp"

#include <winsock2.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr DWORD kReceiveTimeoutMs = 2000;
constexpr auto kSegmentDelay = std::chrono::milliseconds(30);

struct ScopedWinsock {
    ScopedWinsock() : started(WSAStartup(MAKEWORD(2, 2), &data) == 0) {}

    ~ScopedWinsock() {
        if (started) {
            WSACleanup();
        }
    }

    WSADATA data{};
    bool started{false};
};

struct SocketPair {
    SOCKET client{INVALID_SOCKET};
    SOCKET server{INVALID_SOCKET};

    ~SocketPair() {
        if (client != INVALID_SOCKET) {
            closesocket(client);
        }
        if (server != INVALID_SOCKET) {
            closesocket(server);
        }
    }

    SocketPair(const SocketPair&) = delete;
    SocketPair& operator=(const SocketPair&) = delete;
    SocketPair() = default;
    SocketPair(SocketPair&& other) noexcept
        : client(std::exchange(other.client, INVALID_SOCKET)),
          server(std::exchange(other.server, INVALID_SOCKET)) {}
    SocketPair& operator=(SocketPair&& other) noexcept {
        if (this != &other) {
            if (client != INVALID_SOCKET) {
                closesocket(client);
            }
            if (server != INVALID_SOCKET) {
                closesocket(server);
            }
            client = std::exchange(other.client, INVALID_SOCKET);
            server = std::exchange(other.server, INVALID_SOCKET);
        }
        return *this;
    }
};

bool fail(const std::string& case_name, const std::string& message) {
    std::cerr << "[" << case_name << "] " << message << std::endl;
    return false;
}

bool expect_true(const std::string& case_name, bool value, const std::string& message) {
    if (!value) {
        return fail(case_name, message);
    }
    return true;
}

bool expect_false(const std::string& case_name, bool value, const std::string& message) {
    if (value) {
        return fail(case_name, message);
    }
    return true;
}

bool expect_equal(
    const std::string& case_name,
    const std::string& actual,
    const std::string& expected,
    const std::string& label) {
    if (actual != expected) {
        return fail(
            case_name,
            label + " mismatch: expected [" + expected + "] but got [" + actual + "]");
    }
    return true;
}

bool set_receive_timeout(SOCKET socket_handle) {
    const DWORD timeout_ms = kReceiveTimeoutMs;
    return setsockopt(
               socket_handle,
               SOL_SOCKET,
               SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout_ms),
               sizeof(timeout_ms)) != SOCKET_ERROR;
}

bool send_segments(SOCKET socket_handle, const std::vector<std::string>& segments) {
    for (const auto& segment : segments) {
        std::this_thread::sleep_for(kSegmentDelay);
        if (segment.empty() ||
            !protocol::send_all(socket_handle, segment.data(), segment.size())) {
            return false;
        }
    }
    return true;
}

bool expect_equal(
    const std::string& case_name,
    std::size_t actual,
    std::size_t expected,
    const std::string& label) {
    if (actual != expected) {
        return fail(
            case_name,
            label + " mismatch: expected " + std::to_string(expected) + " but got " +
                std::to_string(actual));
    }
    return true;
}

SocketPair create_loopback_pair(const std::string& case_name) {
    SocketPair sockets;

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        fail(case_name, "failed to create listener socket");
        return sockets;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) ==
        SOCKET_ERROR) {
        fail(case_name, "failed to bind listener socket");
        closesocket(listener);
        return sockets;
    }

    if (listen(listener, 1) == SOCKET_ERROR) {
        fail(case_name, "failed to listen on loopback socket");
        closesocket(listener);
        return sockets;
    }

    int address_length = sizeof(address);
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_length) ==
        SOCKET_ERROR) {
        fail(case_name, "failed to query listener address");
        closesocket(listener);
        return sockets;
    }

    sockets.client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockets.client == INVALID_SOCKET) {
        fail(case_name, "failed to create client socket");
        closesocket(listener);
        return sockets;
    }

    if (connect(
            sockets.client,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        fail(case_name, "failed to connect client socket");
        closesocket(listener);
        return sockets;
    }

    sockets.server = accept(listener, nullptr, nullptr);
    closesocket(listener);
    if (sockets.server == INVALID_SOCKET) {
        fail(case_name, "failed to accept server socket");
        return sockets;
    }

    if (!set_receive_timeout(sockets.server)) {
        fail(case_name, "failed to set server receive timeout");
        closesocket(sockets.server);
        sockets.server = INVALID_SOCKET;
    }

    return sockets;
}

bool test_send_frame_rejects_empty_payload() {
    const std::string case_name = "send_frame rejects empty payload";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }
    return expect_false(
        case_name,
        protocol::send_frame(sockets.client, ""),
        "protocol::send_frame should reject empty payload");
}

bool test_send_frame_rejects_oversized_payload() {
    const std::string case_name = "send_frame rejects oversized payload";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }
    return expect_false(
        case_name,
        protocol::send_frame(
            sockets.client, std::string(protocol::kMaxMessageSize + 1, 'x')),
        "protocol::send_frame should reject oversized payload");
}

bool test_recv_frame_rejects_zero_length_header() {
    const std::string case_name = "recv_frame rejects zero length header";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const std::uint32_t network_length = htonl(0);
    if (!expect_true(
            case_name,
            protocol::send_all(
                sockets.client,
                reinterpret_cast<const char*>(&network_length),
                sizeof(network_length)),
            "failed to send zero-length header")) {
        return false;
    }

    std::string payload;
    return expect_false(
        case_name,
        protocol::recv_frame(sockets.server, payload),
        "protocol::recv_frame should reject zero-length header");
}

bool test_recv_frame_rejects_oversized_header() {
    const std::string case_name = "recv_frame rejects oversized header";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const std::uint32_t network_length = htonl(protocol::kMaxMessageSize + 1);
    if (!expect_true(
            case_name,
            protocol::send_all(
                sockets.client,
                reinterpret_cast<const char*>(&network_length),
                sizeof(network_length)),
            "failed to send oversized header")) {
        return false;
    }

    std::string payload;
    return expect_false(
        case_name,
        protocol::recv_frame(sockets.server, payload),
        "protocol::recv_frame should reject oversized header");
}

bool test_recv_frame_rejects_short_header() {
    const std::string case_name = "recv_frame rejects short header";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const char short_header[3] = {0, 0, 0};
    if (!expect_true(
            case_name,
            protocol::send_all(sockets.client, short_header, sizeof(short_header)),
            "failed to send partial header")) {
        return false;
    }
    shutdown(sockets.client, SD_SEND);

    std::string payload;
    return expect_false(
        case_name,
        protocol::recv_frame(sockets.server, payload),
        "protocol::recv_frame should reject fewer than four header bytes");
}

bool test_recv_frame_rejects_truncated_payload() {
    const std::string case_name = "recv_frame rejects truncated payload";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const std::string payload = "hello";
    const std::uint32_t network_length = htonl(static_cast<std::uint32_t>(payload.size()));
    if (!expect_true(
            case_name,
            protocol::send_all(
                sockets.client,
                reinterpret_cast<const char*>(&network_length),
                sizeof(network_length)),
            "failed to send payload header")) {
        return false;
    }
    if (!expect_true(
            case_name,
            protocol::send_all(sockets.client, payload.data(), payload.size() - 1),
            "failed to send truncated payload")) {
        return false;
    }
    shutdown(sockets.client, SD_SEND);

    std::string received;
    return expect_false(
        case_name,
        protocol::recv_frame(sockets.server, received),
        "protocol::recv_frame should reject truncated payload");
}

bool test_recv_all_reads_known_byte_sequence() {
    const std::string case_name = "recv_all reads known byte sequence";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const std::vector<std::string> segments = {"known", "-byte", "-sequence"};
    const std::string sent = "known-byte-sequence";
    std::atomic<bool> sender_succeeded{false};
    std::thread sender([&]() {
        sender_succeeded.store(send_segments(sockets.client, segments));
    });

    std::string received(sent.size(), '\0');
    const bool receive_succeeded =
        protocol::recv_all(sockets.server, received.data(), received.size());
    sender.join();

    return expect_true(case_name, sender_succeeded.load(), "segmented sender failed") &&
        expect_true(
               case_name,
               receive_succeeded,
               "protocol::recv_all should read all delayed segments before timeout") &&
        expect_equal(case_name, received, sent, "recv_all payload");
}

bool test_recv_frame_reads_segmented_header_and_payload() {
    const std::string case_name = "recv_frame reads segmented header and payload";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const std::string sent = R"({"type":"chat","content":"segmented frame"})";
    const std::uint32_t network_length =
        htonl(static_cast<std::uint32_t>(sent.size()));
    const std::string header(
        reinterpret_cast<const char*>(&network_length), sizeof(network_length));
    const std::vector<std::string> segments = {
        header.substr(0, 1),
        header.substr(1, 2),
        header.substr(3, 1),
        sent.substr(0, 7),
        sent.substr(7, 9),
        sent.substr(16),
    };

    std::atomic<bool> sender_succeeded{false};
    std::thread sender([&]() {
        sender_succeeded.store(send_segments(sockets.client, segments));
    });

    std::string received;
    const bool receive_succeeded = protocol::recv_frame(sockets.server, received);
    sender.join();

    return expect_true(case_name, sender_succeeded.load(), "segmented sender failed") &&
        expect_true(
               case_name,
               receive_succeeded,
               "protocol::recv_frame should accept delayed header and payload segments") &&
        expect_equal(case_name, received, sent, "segmented frame payload");
}

bool test_receive_message_rejects_malformed_json() {
    const std::string case_name = "receive_message rejects malformed json";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    if (!expect_true(
            case_name,
            protocol::send_frame(sockets.client, "{not json"),
            "failed to send malformed JSON frame")) {
        return false;
    }

    message::Message received;
    return expect_false(
        case_name,
        message::receive_message(sockets.server, received),
        "message::receive_message should reject malformed JSON");
}

bool test_receive_message_rejects_missing_string_type() {
    const std::string case_name = "receive_message rejects missing string type";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    if (!expect_true(
            case_name,
            protocol::send_frame(sockets.client, R"({"content":"hello"})"),
            "failed to send JSON without type")) {
        return false;
    }

    message::Message received;
    return expect_false(
        case_name,
        message::receive_message(sockets.server, received),
        "message::receive_message should reject JSON without string type");
}

bool test_receive_message_rejects_numeric_content() {
    const std::string case_name = "receive_message rejects numeric content";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    if (!expect_true(
            case_name,
            protocol::send_frame(sockets.client, R"({"type":"chat","content":42})"),
            "failed to send JSON with numeric content")) {
        return false;
    }

    message::Message received;
    return expect_false(
        case_name,
        message::receive_message(sockets.server, received),
        "message::receive_message should reject numeric content");
}

bool test_valid_message_round_trip() {
    const std::string case_name = "valid message round trip";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const message::Message sent{
        "chat",
        "Alice",
        "A001",
        u8"你好，这是测试消息。",
        {},
        {}};

    if (!expect_true(
            case_name,
            message::send_message(sockets.client, sent),
            "message::send_message should succeed")) {
        return false;
    }

    message::Message received;
    if (!expect_true(
            case_name,
            message::receive_message(sockets.server, received),
            "message::receive_message should succeed")) {
        return false;
    }

    return expect_equal(case_name, received.type, sent.type, "type") &&
        expect_equal(case_name, received.username, sent.username, "username") &&
        expect_equal(case_name, received.user_code, sent.user_code, "user_code") &&
        expect_equal(case_name, received.content, sent.content, "content") &&
        expect_equal(case_name, received.users.size(), sent.users.size(), "users size");
}

bool test_valid_private_message_round_trip() {
    const std::string case_name = "valid private message round trip";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const message::Message sent{
        "private_chat",
        "Alice",
        "A001",
        u8"你好，这是私聊消息。",
        {},
        "BOB01"};

    if (!expect_true(
            case_name,
            message::send_message(sockets.client, sent),
            "message::send_message should succeed")) {
        return false;
    }

    message::Message received;
    if (!expect_true(
            case_name,
            message::receive_message(sockets.server, received),
            "message::receive_message should succeed")) {
        return false;
    }

    return expect_equal(case_name, received.type, sent.type, "type") &&
        expect_equal(case_name, received.username, sent.username, "username") &&
        expect_equal(case_name, received.user_code, sent.user_code, "user_code") &&
        expect_equal(
               case_name,
               received.target_user_code,
               sent.target_user_code,
               "target_user_code") &&
        expect_equal(case_name, received.content, sent.content, "content") &&
        expect_equal(case_name, received.users.size(), sent.users.size(), "users size");
}

bool test_receive_message_rejects_numeric_target_user_code() {
    const std::string case_name = "receive_message rejects numeric target user code";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    if (!expect_true(
            case_name,
            protocol::send_frame(
                sockets.client,
                R"({"type":"private_chat","target_user_code":123,"content":"hello"})"),
            "failed to send JSON with numeric target_user_code")) {
        return false;
    }

    message::Message received;
    return expect_false(
        case_name,
        message::receive_message(sockets.server, received),
        "message::receive_message should reject numeric target_user_code");
}

bool test_three_frames_preserve_order() {
    const std::string case_name = "three frames preserve order";
    SocketPair sockets = create_loopback_pair(case_name);
    if (sockets.client == INVALID_SOCKET || sockets.server == INVALID_SOCKET) {
        return false;
    }

    const std::vector<std::string> sent_payloads = {"first", "second", "third"};
    for (const auto& payload : sent_payloads) {
        if (!expect_true(
                case_name,
                protocol::send_frame(sockets.client, payload),
                "protocol::send_frame should succeed for ordered payloads")) {
            return false;
        }
    }

    for (std::size_t index = 0; index < sent_payloads.size(); ++index) {
        std::string received;
        if (!expect_true(
                case_name,
                protocol::recv_frame(sockets.server, received),
                "protocol::recv_frame should succeed for ordered payloads")) {
            return false;
        }
        if (!expect_equal(case_name, received, sent_payloads[index], "payload order")) {
            return false;
        }
    }

    return true;
}

}  // namespace

int main() {
    ScopedWinsock winsock;
    if (!winsock.started) {
        std::cerr << "[winsock startup] WSAStartup failed" << std::endl;
        return 1;
    }

    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"protocol::send_frame(socket, \"\") returns false",
         test_send_frame_rejects_empty_payload},
        {"protocol::send_frame(socket, oversized) returns false",
         test_send_frame_rejects_oversized_payload},
        {"protocol::recv_frame rejects zero-length header",
         test_recv_frame_rejects_zero_length_header},
        {"protocol::recv_frame rejects oversized header",
         test_recv_frame_rejects_oversized_header},
        {"protocol::recv_frame rejects fewer than 4 header bytes",
         test_recv_frame_rejects_short_header},
        {"protocol::recv_frame rejects truncated payload",
         test_recv_frame_rejects_truncated_payload},
        {"protocol::recv_all reads a complete known byte sequence",
         test_recv_all_reads_known_byte_sequence},
        {"protocol::recv_frame reads a segmented valid frame",
         test_recv_frame_reads_segmented_header_and_payload},
        {"message::receive_message rejects malformed JSON",
         test_receive_message_rejects_malformed_json},
        {"message::receive_message rejects JSON without string type",
         test_receive_message_rejects_missing_string_type},
        {"message::receive_message rejects numeric content",
         test_receive_message_rejects_numeric_content},
        {"message::receive_message rejects numeric target_user_code",
         test_receive_message_rejects_numeric_target_user_code},
        {"valid UTF-8 message round-trip preserves fields", test_valid_message_round_trip},
        {"valid private message round-trip preserves fields",
         test_valid_private_message_round_trip},
        {"three valid frames are received in send order", test_three_frames_preserve_order},
    };

    std::size_t failures = 0;
    for (const auto& [test_name, test] : tests) {
        if (!test()) {
            ++failures;
        }
    }

    if (failures != 0) {
        std::cerr << failures << " protocol test(s) failed" << std::endl;
        return 1;
    }

    std::cout << "All " << tests.size() << " protocol tests passed" << std::endl;
    return 0;
}
