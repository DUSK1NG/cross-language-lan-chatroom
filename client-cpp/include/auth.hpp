#pragma once

#include "message.hpp"

#include <string>
#include <vector>

namespace auth {

struct ClientOptions {
    std::string server_ip = "127.0.0.1";
    int server_port = 8888;
    std::string username = "Alice";
    std::string user_code = "ALICE001";
    std::string ca_file;
    std::string password;
    bool register_account = false;
    bool guest_mode = false;
    bool host_mode = false;
    std::string server_exe = "..\\server-go\\chat-server.exe";
    std::string cert_file = "..\\certs\\server.crt";
    std::string key_file = "..\\certs\\server.key";
};

bool parse_arguments(
    const std::vector<std::string>& args,
    ClientOptions& options,
    std::string& error);

std::string usage();
message::Message make_register_message(const ClientOptions& options);
message::Message make_login_message(const ClientOptions& options);

}  // namespace auth
