#include "auth.hpp"

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

bool test_parse_password_options_removed() {
    auth::ClientOptions options;
    std::string error;
    const bool parsed = auth::parse_arguments(
        {"192.168.1.10", "8888", "Alice", "ALICE001", "--password", "secret"},
        options,
        error);
    return expect_true("password option rejected", !parsed, "password option was accepted");
}

bool test_build_passwordless_login_message() {
    const auth::ClientOptions options{
        "127.0.0.1", 8888, "Alice", "ALICE001", "ca.crt"};
    const message::Message login = auth::make_login_message(options);
    return expect_true(
        "passwordless login type", login.type == "login", "login did not use passwordless login") &&
        expect_true("login has no password", login.password.empty(), "login carried a password");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"password options removed", test_parse_password_options_removed},
        {"build passwordless login", test_build_passwordless_login_message},
    };

    std::size_t failures = 0;
    for (const auto& [test_name, test] : tests) {
        if (!test()) ++failures;
    }
    if (failures != 0) {
        std::cerr << failures << " auth test(s) failed\n";
        return 1;
    }
    std::cout << "All " << tests.size() << " auth tests passed\n";
    return 0;
}
