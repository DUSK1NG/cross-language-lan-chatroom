#include "connection.hpp"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <ws2tcpip.h>

#include <array>
#include <utility>

namespace connection {
namespace {

constexpr std::chrono::seconds kBackoffDelays[] = {
    std::chrono::seconds(1), std::chrono::seconds(2), std::chrono::seconds(4),
    std::chrono::seconds(8), std::chrono::seconds(16), std::chrono::seconds(30),
};

void close_socket(SOCKET socket_handle) {
    if (socket_handle != INVALID_SOCKET) {
        shutdown(socket_handle, SD_BOTH);
        closesocket(socket_handle);
    }
}

std::string openssl_error(const char* operation) {
    std::array<char, 256> buffer{};
    const unsigned long error_code = ERR_get_error();
    if (error_code == 0) return operation;
    ERR_error_string_n(error_code, buffer.data(), buffer.size());
    return std::string(operation) + ": " + buffer.data();
}

bool set_verify_host(SSL* ssl_handle, const std::string& host) {
    IN_ADDR address{};
    if (inet_pton(AF_INET, host.c_str(), &address) == 1) {
        return X509_VERIFY_PARAM_set1_ip_asc(
            SSL_get0_param(ssl_handle), host.c_str()) == 1;
    }
    return SSL_set1_host(ssl_handle, host.c_str()) == 1;
}

}  // namespace

struct ConnectionState::Session {
    SOCKET socket_handle = INVALID_SOCKET;
    SSL_CTX* ssl_context = nullptr;
    SSL* ssl = nullptr;

    ~Session() {
        if (ssl != nullptr) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        if (ssl_context != nullptr) SSL_CTX_free(ssl_context);
        close_socket(socket_handle);
    }
};

std::chrono::seconds ReconnectBackoff::next_delay() {
    const std::size_t index = attempt_ < std::size(kBackoffDelays)
        ? attempt_ : std::size(kBackoffDelays) - 1;
    if (attempt_ < std::size(kBackoffDelays)) ++attempt_;
    return kBackoffDelays[index];
}

void ReconnectBackoff::reset() { attempt_ = 0; }

ConnectionState::ConnectionState(Config config) : config_(std::move(config)) {
    OPENSSL_init_ssl(
        OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
        nullptr);
}

ConnectionState::~ConnectionState() {
    request_stop();
    close_current();
}

bool ConnectionState::connect_and_login(
    message::Message& login_response,
    LoginResult& result) {
    login_response = message::Message{};
    result = LoginResult::kRetryableFailure;

    auto candidate = std::make_shared<Session>();
    candidate->socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (candidate->socket_handle == INVALID_SOCKET) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_error_ = "socket failed";
        return false;
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_) return false;
        session_ = candidate;
        ready_ = false;
    }

    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(static_cast<u_short>(config_.server_port));
    if (inet_pton(AF_INET, config_.server_ip.c_str(), &server_address.sin_addr) != 1) {
        last_error_ = "invalid server IPv4 address";
        close_current();
        return false;
    }
    if (connect(candidate->socket_handle,
                reinterpret_cast<const sockaddr*>(&server_address),
                sizeof(server_address)) == SOCKET_ERROR) {
        last_error_ = "TCP connect failed";
        close_current();
        return false;
    }

    candidate->ssl_context = SSL_CTX_new(TLS_client_method());
    if (candidate->ssl_context == nullptr) {
        last_error_ = openssl_error("SSL_CTX_new failed");
        close_current();
        return false;
    }
    SSL_CTX_set_min_proto_version(candidate->ssl_context, TLS1_2_VERSION);
    SSL_CTX_set_verify(candidate->ssl_context, SSL_VERIFY_PEER, nullptr);
    const bool ca_loaded = config_.ca_file.empty()
        ? SSL_CTX_set_default_verify_paths(candidate->ssl_context) == 1
        : SSL_CTX_load_verify_locations(
              candidate->ssl_context, config_.ca_file.c_str(), nullptr) == 1;
    if (!ca_loaded) {
        last_error_ = config_.ca_file.empty()
            ? openssl_error("no trusted system CA paths")
            : openssl_error("failed to load --ca-file");
        close_current();
        return false;
    }

    candidate->ssl = SSL_new(candidate->ssl_context);
    if (candidate->ssl == nullptr ||
        SSL_set_fd(candidate->ssl, static_cast<int>(candidate->socket_handle)) != 1 ||
        !set_verify_host(candidate->ssl, config_.server_ip)) {
        last_error_ = openssl_error("TLS setup failed");
        close_current();
        return false;
    }
    IN_ADDR numeric_address{};
    if (inet_pton(AF_INET, config_.server_ip.c_str(), &numeric_address) != 1) {
        SSL_set_tlsext_host_name(candidate->ssl, config_.server_ip.c_str());
    }
    if (SSL_connect(candidate->ssl) != 1) {
        last_error_ = openssl_error("SSL_connect failed");
        close_current();
        return false;
    }
    if (SSL_get_verify_result(candidate->ssl) != X509_V_OK) {
        last_error_ = "TLS certificate verification failed";
        close_current();
        return false;
    }

    const message::Message login{
        "login", config_.username, config_.user_code, "", {}, ""};
    if (!message::send_message(candidate->ssl, login) ||
        !message::receive_message(candidate->ssl, login_response)) {
        last_error_ = openssl_error("TLS login exchange failed");
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
        if (stop_requested_ || session_ != candidate) {
            ready_ = false;
            return false;
        }
        ready_ = true;
        last_error_.clear();
    }
    result = LoginResult::kSuccess;
    return true;
}

bool ConnectionState::receive(message::Message& message) const {
    std::shared_ptr<Session> session;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!ready_ || stop_requested_) return false;
        session = session_;
    }
    return session != nullptr && message::receive_message(session->ssl, message);
}

bool ConnectionState::send(const message::Message& message) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return ready_ && !stop_requested_ && session_ != nullptr &&
        message::send_message(session_->ssl, message);
}

void ConnectionState::request_disconnect() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (session_ != nullptr) shutdown(session_->socket_handle, SD_BOTH);
}

void ConnectionState::request_stop() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_ = true;
        if (session_ != nullptr) shutdown(session_->socket_handle, SD_BOTH);
    }
    condition_.notify_all();
}

void ConnectionState::close_current() const {
    std::shared_ptr<Session> old_session;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        old_session = std::move(session_);
        ready_ = false;
    }
    if (old_session != nullptr) shutdown(old_session->socket_handle, SD_BOTH);
}

bool ConnectionState::is_ready() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return ready_ && !stop_requested_ && session_ != nullptr;
}

bool ConnectionState::stop_requested() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return stop_requested_;
}

std::string ConnectionState::last_error() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
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
