#include "connection.hpp"

#include <winsock2.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect_true(const std::string& name, bool value, const std::string& detail) {
    if (!value) {
        std::cerr << "[" << name << "] " << detail << '\n';
        return false;
    }
    return true;
}

bool test_backoff_sequence_and_reset() {
    connection::ReconnectBackoff backoff;
    const std::vector<long long> expected = {1, 2, 4, 8, 16, 30, 30};
    for (const long long seconds : expected) {
        if (!expect_true(
                "backoff sequence",
                backoff.next_delay() == std::chrono::seconds(seconds),
                "unexpected reconnect delay")) {
            return false;
        }
    }
    backoff.reset();
    return expect_true(
        "backoff reset",
        backoff.next_delay() == std::chrono::seconds(1),
        "reset did not restart at one second");
}

bool test_initial_connection_state() {
    const connection::Config config{"127.0.0.1", 8888, "Alice", "ALICE001", ""};
    const connection::ConnectionState state(config);
    return expect_true(
               "initial connection state",
               !state.is_ready(),
               "new connection unexpectedly reports ready") &&
        expect_true(
            "send without connection",
            !state.send(message::Message{"chat", "", "", "hello"}),
            "send should fail without an active socket");
}

bool test_stop_interrupts_retry_wait() {
    const connection::Config config{"127.0.0.1", 8888, "Alice", "ALICE001", ""};
    connection::ConnectionState state(config);
    std::atomic<bool> running{true};
    std::atomic<bool> reconnect_enabled{true};
    state.request_stop();
    return expect_true(
        "stop interrupts retry wait",
        !state.wait_before_retry(std::chrono::seconds(30), running, reconnect_enabled),
        "stop state should prevent another reconnect attempt");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"backoff sequence and reset", test_backoff_sequence_and_reset},
        {"initial connection state", test_initial_connection_state},
        {"stop interrupts retry wait", test_stop_interrupts_retry_wait},
    };

    std::size_t failures = 0;
    for (const auto& [test_name, test] : tests) {
        if (!test()) {
            ++failures;
        }
    }
    if (failures != 0) {
        std::cerr << failures << " connection test(s) failed\n";
        return 1;
    }
    std::cout << "All " << tests.size() << " connection tests passed\n";
    return 0;
}
