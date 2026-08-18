#pragma once

#include <winsock2.h>

#include "message.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>

namespace connection {

struct Config {
    std::string server_ip;
    int server_port = 0;
    std::string username;
    std::string user_code;
    std::string ca_file;
};

enum class LoginResult {
    kSuccess,
    kRetryableFailure,
    kRejected,
};

class ReconnectBackoff {
public:
    std::chrono::seconds next_delay();
    void reset();

private:
    std::size_t attempt_ = 0;
};

class ConnectionState {
public:
    explicit ConnectionState(Config config);
    ~ConnectionState();

    ConnectionState(const ConnectionState&) = delete;
    ConnectionState& operator=(const ConnectionState&) = delete;

    bool connect_and_login(message::Message& login_response, LoginResult& result);
    bool receive(message::Message& message) const;
    bool send(const message::Message& message) const;

    void request_disconnect() const;
    void request_stop();
    void close_current() const;

    bool is_ready() const;
    bool stop_requested() const;
    std::string last_error() const;
    bool wait_before_retry(
        std::chrono::seconds delay,
        const std::atomic<bool>& running,
        const std::atomic<bool>& reconnect_enabled) const;

private:
    struct Session;
    Config config_;
    mutable std::mutex mutex_;
    mutable std::condition_variable condition_;
    mutable std::shared_ptr<Session> session_;
    mutable bool ready_ = false;
    bool stop_requested_ = false;
    std::string last_error_;
};

}  // namespace connection
