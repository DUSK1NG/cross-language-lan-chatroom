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

bool test_parse_password_and_register() {
    auth::ClientOptions options;
    std::string error;
    const bool parsed = auth::parse_arguments(
        {"192.168.1.10", "8888", "Alice", "ALICE001", "--password", "secret", "--register",
         "--ca-file", "certs/server.crt"},
        options,
        error);
    return expect_true("parse register options", parsed, error) &&
        expect_true("register flag", options.register_account, "--register was not parsed") &&
        expect_true("password retained", options.password == "secret", "password was not parsed") &&
        expect_true("ca file", options.ca_file == "certs/server.crt", "CA path was not parsed");
}

bool test_build_auth_messages() {
    const auth::ClientOptions options{
        "127.0.0.1", 8888, "Alice", "ALICE001", "ca.crt", "secret", true};
    const message::Message registration = auth::make_register_message(options);
    const message::Message login = auth::make_login_message(options);
    return expect_true("register type", registration.type == "register", "wrong register type") &&
        expect_true("register password", registration.password == "secret", "missing register password") &&
        expect_true("login auth type", login.type == "login_auth", "password login did not use login_auth") &&
        expect_true("login password", login.password == "secret", "missing login password");
}

bool test_legacy_login_without_password() {
    const auth::ClientOptions options{
        "127.0.0.1", 8888, "Alice", "ALICE001", "", "", false};
    const message::Message login = auth::make_login_message(options);
    return expect_true(
        "legacy login type", login.type == "login", "empty password should use legacy login");
}

bool test_register_requires_password() {
    auth::ClientOptions options;
    std::string error;
    const bool parsed = auth::parse_arguments(
        {"127.0.0.1", "8888", "Alice", "ALICE001", "--register"}, options, error);
    return expect_true(
        "register password required", !parsed && error == "--register requires --password.",
        "register without password was accepted");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"parse password and register", test_parse_password_and_register},
        {"build auth messages", test_build_auth_messages},
        {"legacy login without password", test_legacy_login_without_password},
        {"register requires password", test_register_requires_password},
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
