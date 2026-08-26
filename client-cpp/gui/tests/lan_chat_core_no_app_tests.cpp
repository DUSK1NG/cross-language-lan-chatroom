#include "lan_chat_core.h"

#include <cstdio>

int main() {
    auto* handle = lan_chat_core_create();
    if (!handle) {
        std::fputs("lan_chat_core_create returned null without a Qt application\n", stderr);
        return 1;
    }

    char* state = lan_chat_core_current_state_json(handle);
    const bool hasState = state != nullptr;
    lan_chat_core_free_string(state);
    lan_chat_core_destroy(handle);

    if (!hasState) {
        std::fputs("lan_chat_core_current_state_json returned null without a Qt application\n", stderr);
        return 1;
    }
    return 0;
}
