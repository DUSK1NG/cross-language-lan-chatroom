#include "command.hpp"

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

bool expect_false(const std::string& name, bool value, const std::string& detail) {
    return expect_true(name, !value, detail);
}

bool expect_equal(
    const std::string& name,
    const std::string& actual,
    const std::string& expected,
    const std::string& field) {
    if (actual != expected) {
        std::cerr << "[" << name << "] " << field << " mismatch: expected ["
                  << expected << "] but got [" << actual << "]\n";
        return false;
    }
    return true;
}

bool test_parse_private_message() {
    const std::string name = "parse valid private message";
    command::PrivateMessageCommand parsed;
    if (!expect_true(
            name,
            command::parse_private_message(
                "/msg Bob#BOB01 "
                "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C"
                "\xE8\xBF\x99\xE6\x98\xAF\xE7\xA7\x81\xE8\x81\x8A\xE3\x80\x82",
                parsed),
            "valid /msg command was rejected")) {
        return false;
    }
    const std::string expected_content =
        "\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x8C"
        "\xE8\xBF\x99\xE6\x98\xAF\xE7\xA7\x81\xE8\x81\x8A\xE3\x80\x82";
    return expect_equal(name, parsed.target_name, "Bob", "target name") &&
        expect_equal(name, parsed.target_user_code, "BOB01", "target code") &&
        expect_equal(name, parsed.content, expected_content, "content");
}

bool test_parse_private_message_allows_extra_spaces() {
    const std::string name = "parse private message with extra spaces";
    command::PrivateMessageCommand parsed;
    if (!expect_true(
            name,
            command::parse_private_message("/msg   Bob#bob01    hello", parsed),
            "extra spaces should be accepted")) {
        return false;
    }
    return expect_equal(name, parsed.target_user_code, "bob01", "target code") &&
        expect_equal(name, parsed.content, "hello", "content");
}

bool test_rejects_invalid_commands() {
    const std::vector<std::string> invalid_inputs = {
        "/msg",
        "/msg ",
        "/msg Bob hello",
        "/msg #BOB01 hello",
        "/msg Bob# hello",
        "/msg Bob#BO-B01 hello",
        "/msg Bob#BOB01",
        "/msg Bob#BOB01   ",
    };

    for (const auto& input : invalid_inputs) {
        command::PrivateMessageCommand parsed;
        if (!expect_false(
                "reject invalid private command",
                command::parse_private_message(input, parsed),
                "invalid /msg command was accepted: " + input)) {
            return false;
        }
    }
    return true;
}

bool test_prefix_detection() {
    return expect_true(
               "detect /msg prefix",
               command::is_private_message_command("/msg Bob#BOB01 hello"),
               "valid /msg prefix was not detected") &&
        expect_true(
               "detect incomplete /msg command",
               command::is_private_message_command("/msg"),
               "incomplete /msg command should be handled locally") &&
        expect_false(
               "reject similar command prefix",
               command::is_private_message_command("/message Bob#BOB01 hello"),
               "non-/msg command was detected as private message");
}

bool test_room_commands() {
    command::RoomCommand parsed;
    if (!expect_true("rooms command", command::is_rooms_command("/rooms"), "not detected")) {
        return false;
    }
    if (!expect_true("leave command", command::is_leave_command("/leave"), "not detected")) {
        return false;
    }
    if (!expect_true("join prefix", command::is_join_command("/join lobby"), "not detected")) {
        return false;
    }
    if (!expect_true(
            "valid room name",
            command::parse_join_command("/join study_room_2", parsed),
            "valid room rejected")) {
        return false;
    }
    return expect_equal("valid room name", parsed.room_name, "study_room_2", "room name");
}

bool test_invalid_room_commands() {
    const std::vector<std::string> invalid_inputs = {
        "/join", "/join ", "/join bad-room", "/join bad room", "/join !room",
        "/join " + std::string(33, 'a')};
    for (const auto& input : invalid_inputs) {
        command::RoomCommand parsed;
        if (!expect_false(
                "invalid room command", command::parse_join_command(input, parsed),
                "invalid room command accepted: " + input)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"valid private message is parsed", test_parse_private_message},
        {"extra spaces are accepted", test_parse_private_message_allows_extra_spaces},
        {"invalid private commands are rejected", test_rejects_invalid_commands},
        {"/msg prefix is detected", test_prefix_detection},
        {"room commands are parsed", test_room_commands},
        {"invalid room commands are rejected", test_invalid_room_commands},
    };

    std::size_t failures = 0;
    for (const auto& [test_name, test] : tests) {
        if (!test()) {
            ++failures;
        }
    }

    if (failures != 0) {
        std::cerr << failures << " command test(s) failed\n";
        return 1;
    }

    std::cout << "All " << tests.size() << " command tests passed\n";
    return 0;
}
