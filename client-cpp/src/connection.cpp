#include "connection.hpp"

#include <ws2tcpip.h>

#include <utility>

namespace connection {
namespace {

constexpr std::chrono::seconds kBackoffDelays[] = {
    std::chrono::seconds(1),
    std::chrono::seconds(2),
    std::chrono::seconds(4),
    std::chrono::seconds(8),
    std::chrono::seconds(16),
    std::chrono::seconds(30),
};

void close_socket(SOCKET socket_handle) {
    if (socket_handle != INVALID_SOCKET) {
        shutdown(socket_handle, SD_BOTH);
        closesocket(socket_handle);
    }
}

}  // namespace

std::chrono::seconds ReconnectBackoff::next_delay() {
    const std::size_t index = attempt_ < std::size(kBackoffDelays)
        ? attempt_
        : std::size(kBackoffDelays) - 1;
    if (attempt_ < std::size(kBackoffDelays)) {
        ++attempt_;
    }
    return kBackoffDelays[index];
}

void ReconnectBackoff::reset() {
    attempt_ = 0;
}

ConnectionState::ConnectionState(Config config) : config_(std::move(config)) {}

ConnectionState::~ConnectionState() {
    request_stop();
    close_current();
}

bool ConnectionState::connect_and_login(
    message::Message& login_response,
    LoginResult& result) {
    login_response = message::Message{};
    result = LoginResult::kRetryableFailure;

    SOCKET candidate = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (candidate == INVALID_SOCKET) {
        return false;
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_) {
            close_socket(candidate);
            return false;
        }
        socket_handle_ = candidate;
        ready_ = false;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(static_cast<u_short>(config_.server_port));
    if (inet_pton(
            AF_INET,
            config_.server_ip.c_str(),
            &server_address.sin_addr) != 1) {
        close_current();
        return false;
    }

    if (connect(
            candidate,
            reinterpret_cast<const sockaddr*>(&server_address),
            sizeof(server_address)) == SOCKET_ERROR) {
        close_current();
        return false;
    }

    const message::Message login{
        "login", config_.username, config_.user_code, "", {}, ""};
    if (!message::send_message(candidate, login) ||
        !message::receive_message(candidate, login_response)) {
        close_current();
        return false;
    }

    if (login_response.type != "login_ok") {
        result = LoginResult::kRejected;
        close_current();
        return false;
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_ || socket_handle_ != candidate) {
            close_socket(candidate);
            if (socket_handle_ == candidate) {
                socket_handle_ = INVALID_SOCKET;
            }
            ready_ = false;
            return false;
        }
        ready_ = true;
    }
    result = LoginResult::kSuccess;
    return true;
}

bool ConnectionState::receive(message::Message& message) const {
    SOCKET current_socket = INVALID_SOCKET;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!ready_ || stop_requested_) {
            return false;
        }
        current_socket = socket_handle_;
    }
    return message::receive_message(current_socket, message);
}

bool ConnectionState::send(const message::Message& message) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ || stop_requested_ || socket_handle_ == INVALID_SOCKET) {
        return false;
    }
    return message::send_message(socket_handle_, message);
}

void ConnectionState::request_disconnect() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (socket_handle_ != INVALID_SOCKET) {
        shutdown(socket_handle_, SD_BOTH);
    }
}

void ConnectionState::request_stop() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
        if (socket_handle_ != INVALID_SOCKET) {
            shutdown(socket_handle_, SD_BOTH);
        }
    }
    condition_.notify_all();
}

void ConnectionState::close_current() const {
    SOCKET socket_to_close = INVALID_SOCKET;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        socket_to_close = socket_handle_;
        socket_handle_ = INVALID_SOCKET;
        ready_ = false;
    }
    close_socket(socket_to_close);
}

bool ConnectionState::is_ready() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return ready_ && !stop_requested_ && socket_handle_ != INVALID_SOCKET;
}

bool ConnectionState::stop_requested() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return stop_requested_;
}

bool ConnectionState::wait_before_retry(
    std::chrono::seconds delay,
    const std::atomic<bool>& running,
    const std::atomic<bool>& reconnect_enabled) const {
    std::unique_lock<std::mutex> lock(mutex_);
    return !condition_.wait_for(lock, delay, [&] {
        return stop_requested_ || !running.load() || !reconnect_enabled.load();
    });
}

}  // namespace connection
