#pragma once

#if defined(_WIN32)
#if defined(LAN_CHAT_CORE_BUILD)
#define LAN_CHAT_CORE_API __declspec(dllexport)
#else
#define LAN_CHAT_CORE_API __declspec(dllimport)
#endif
#else
#define LAN_CHAT_CORE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* LanChatCoreHandle;

LAN_CHAT_CORE_API LanChatCoreHandle lan_chat_core_create(void);
LAN_CHAT_CORE_API void lan_chat_core_destroy(LanChatCoreHandle handle);
LAN_CHAT_CORE_API int lan_chat_core_dispatch_json(LanChatCoreHandle handle,
                                                   const char* command_json);
LAN_CHAT_CORE_API char* lan_chat_core_current_state_json(LanChatCoreHandle handle);
LAN_CHAT_CORE_API char* lan_chat_core_take_event_json(LanChatCoreHandle handle);
LAN_CHAT_CORE_API void lan_chat_core_free_string(char* value);

#ifdef __cplusplus
}
#endif
