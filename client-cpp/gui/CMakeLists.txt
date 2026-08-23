cmake_minimum_required(VERSION 3.21)

project(lan-chat-gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Test Network)
find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
find_package(Threads REQUIRED)

option(LAN_CHAT_ENABLE_WEB_UI "Build the React WebEngine UI" ON)
option(LAN_CHAT_ENABLE_PERF_OVERLAY "Enable the debug-only frame performance overlay" OFF)

if(MSVC)
    add_compile_definitions(NOMINMAX)
endif()

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(lan-chat-gui
    src/main.cpp
    src/bridge_protocol.cpp
    src/bridge_protocol.hpp
    src/chat_bridge.cpp
    src/chat_bridge.hpp
    src/chat_model.cpp
    src/chat_model.hpp
    src/conversation_filter_model.cpp
    src/conversation_filter_model.hpp
    src/graphics_info.cpp
    src/graphics_info.hpp
    src/host_path_resolver.cpp
    src/host_path_resolver.hpp
    src/local_host_bootstrap.cpp
    src/local_host_bootstrap.hpp
    src/performance_profile.cpp
    src/performance_profile.hpp
    src/gui_connection_worker.cpp
    src/gui_connection_worker.hpp
    src/gui_chat_controller.cpp
    src/gui_chat_controller.hpp
    ../src/auth.cpp
    ../src/connection.cpp
    ../src/message.cpp
    ../src/protocol.cpp
)

set_source_files_properties(qml/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE
)

set(LAN_CHAT_QML_FILES
    qml/Main.qml
    qml/theme/Theme.qml
    qml/controls/AppButton.qml
    qml/controls/IconButton.qml
    qml/controls/AppTextField.qml
    qml/controls/AppDialog.qml
    qml/controls/AppPopup.qml
    qml/controls/AppToast.qml
    qml/controls/AppScrollBar.qml
    qml/controls/Avatar.qml
    qml/controls/Badge.qml
    qml/components/TitleBar.qml
    qml/components/GlassButton.qml
    qml/components/RoomItem.qml
    qml/components/DirectMessageItem.qml
    qml/components/ChatHeader.qml
    qml/components/MessageDelegate.qml
    qml/components/MemberItem.qml
    qml/components/MessageComposer.qml
    qml/components/EmojiPicker.qml
    qml/components/UserProfilePopup.qml
    qml/pages/ModeSelectionPage.qml
    qml/pages/ConnectionPage.qml
    qml/pages/HostSetupPage.qml
    qml/pages/ChatPage.qml
    qml/pages/SettingsPage.qml
)

if(LAN_CHAT_ENABLE_PERF_OVERLAY)
    list(APPEND LAN_CHAT_QML_FILES qml/controls/PerformanceOverlay.qml)
endif()

qt_add_qml_module(lan-chat-gui
    URI LanChatGui
    VERSION 1.0
    QML_FILES ${LAN_CHAT_QML_FILES}
    RESOURCES
        qml/icons/server.svg
        qml/icons/network.svg
        qml/icons/users.svg
        qml/icons/settings.svg
        qml/icons/search.svg
        qml/icons/send.svg
        qml/icons/plus.svg
        qml/icons/refresh.svg
        qml/icons/close.svg
        qml/icons/minimize.svg
        qml/icons/maximize.svg
        qml/icons/restore.svg
        qml/icons/emoji.svg
)

target_link_libraries(lan-chat-gui PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    ws2_32
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(lan-chat-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

if(LAN_CHAT_ENABLE_PERF_OVERLAY)
    target_sources(lan-chat-gui PRIVATE
        src/performance_sampler.cpp
        src/performance_sampler.hpp
    )
    target_compile_definitions(lan-chat-gui PRIVATE LAN_CHAT_ENABLE_PERF_OVERLAY=1)
endif()

qt_add_executable(bridge-protocol-tests
    tests/bridge_protocol_tests.cpp
    src/bridge_protocol.cpp
    src/bridge_protocol.hpp
)

target_link_libraries(bridge-protocol-tests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(bridge-protocol-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

enable_testing()
add_test(NAME bridge-protocol-tests COMMAND bridge-protocol-tests)

qt_add_executable(graphics-info-tests
    tests/graphics_info_tests.cpp
    src/graphics_info.cpp
    src/graphics_info.hpp
)

target_link_libraries(graphics-info-tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Test
)

target_include_directories(graphics-info-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME graphics-info-tests COMMAND graphics-info-tests)

qt_add_executable(chat-model-tests
    tests/chat_model_tests.cpp
    src/chat_model.cpp
    src/chat_model.hpp
)

target_link_libraries(chat-model-tests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(chat-model-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME chat-model-tests COMMAND chat-model-tests)

qt_add_executable(conversation-filter-tests
    tests/conversation_filter_tests.cpp
    src/chat_model.cpp
    src/chat_model.hpp
    src/conversation_filter_model.cpp
    src/conversation_filter_model.hpp
)

target_link_libraries(conversation-filter-tests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(conversation-filter-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME conversation-filter-tests COMMAND conversation-filter-tests)

qt_add_executable(chat-bridge-tests
    tests/chat_bridge_tests.cpp
    src/chat_bridge.cpp
    src/chat_bridge.hpp
    src/graphics_info.cpp
    src/graphics_info.hpp
    src/performance_profile.cpp
    src/performance_profile.hpp
    src/bridge_protocol.cpp
    src/bridge_protocol.hpp
    src/chat_model.cpp
    src/chat_model.hpp
    src/conversation_filter_model.cpp
    src/conversation_filter_model.hpp
    src/host_path_resolver.cpp
    src/host_path_resolver.hpp
    src/gui_chat_controller.cpp
    src/gui_chat_controller.hpp
    src/gui_connection_worker.cpp
    src/gui_connection_worker.hpp
    ../src/auth.cpp
    ../src/connection.cpp
    ../src/message.cpp
    ../src/protocol.cpp
)

target_link_libraries(chat-bridge-tests PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Test
    Qt6::Network
    ws2_32
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(chat-bridge-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

add_test(NAME chat-bridge-tests COMMAND chat-bridge-tests)

qt_add_executable(performance-sampler-tests
    tests/performance_sampler_tests.cpp
    src/performance_sampler.cpp
    src/performance_sampler.hpp
)

target_link_libraries(performance-sampler-tests PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Test
)

target_include_directories(performance-sampler-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME performance-sampler-tests COMMAND performance-sampler-tests)

qt_add_executable(performance-profile-tests
    tests/performance_profile_tests.cpp
    src/performance_profile.cpp
    src/performance_profile.hpp
)

target_link_libraries(performance-profile-tests PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::Test
)

target_include_directories(performance-profile-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME performance-profile-tests COMMAND performance-profile-tests)

qt_add_executable(host-path-resolver-tests
    tests/host_path_resolver_tests.cpp
    src/host_path_resolver.cpp
    src/host_path_resolver.hpp
)

target_link_libraries(host-path-resolver-tests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(host-path-resolver-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME host-path-resolver-tests COMMAND host-path-resolver-tests)

qt_add_executable(local-host-bootstrap-tests
    tests/local_host_bootstrap_tests.cpp
    src/host_path_resolver.cpp
    src/host_path_resolver.hpp
    src/local_host_bootstrap.cpp
    src/local_host_bootstrap.hpp
)

target_link_libraries(local-host-bootstrap-tests PRIVATE
    Qt6::Core
    Qt6::Test
)

target_include_directories(local-host-bootstrap-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_test(NAME local-host-bootstrap-tests COMMAND local-host-bootstrap-tests)

# CTest on Windows does not inherit the interactive PowerShell PATH used by
# the direct test commands. Provide the Qt and build directories explicitly so
# QtGui/QtQuick test processes can locate their runtime DLLs.
get_filename_component(LAN_CHAT_QT_PREFIX "${Qt6Core_DIR}/../../.." ABSOLUTE)
set(LAN_CHAT_QT_TEST_PATH "${LAN_CHAT_QT_PREFIX}/bin;${CMAKE_CURRENT_BINARY_DIR};$ENV{PATH}")
string(REPLACE ";" "\\;" LAN_CHAT_QT_TEST_PATH_ESCAPED "${LAN_CHAT_QT_TEST_PATH}")
set_tests_properties(
    bridge-protocol-tests
    graphics-info-tests
    chat-model-tests
    conversation-filter-tests
    chat-bridge-tests
    performance-sampler-tests
    performance-profile-tests
    host-path-resolver-tests
    local-host-bootstrap-tests
    PROPERTIES ENVIRONMENT "PATH=${LAN_CHAT_QT_TEST_PATH_ESCAPED}"
)

if(LAN_CHAT_ENABLE_WEB_UI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS WebEngineWidgets WebChannel)

    set(FRONTEND_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../frontend/dist")
    if(NOT EXISTS "${FRONTEND_DIST_DIR}/index.html")
        message(FATAL_ERROR "React release UI is missing: ${FRONTEND_DIST_DIR}/index.html")
    endif()

    file(GLOB FRONTEND_ASSET_FILES CONFIGURE_DEPENDS
        "${FRONTEND_DIST_DIR}/assets/*"
    )
    if(NOT FRONTEND_ASSET_FILES)
        message(FATAL_ERROR "React release UI assets are missing: ${FRONTEND_DIST_DIR}/assets")
    endif()

    set(FRONTEND_ASSET_ENTRIES "")
    foreach(FRONTEND_ASSET IN LISTS FRONTEND_ASSET_FILES)
        get_filename_component(FRONTEND_ASSET_NAME "${FRONTEND_ASSET}" NAME)
        file(TO_CMAKE_PATH "${FRONTEND_ASSET}" FRONTEND_ASSET_QRC_PATH)
        string(APPEND FRONTEND_ASSET_ENTRIES
            "        <file alias=\"frontend/assets/${FRONTEND_ASSET_NAME}\">${FRONTEND_ASSET_QRC_PATH}</file>\n"
        )
    endforeach()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/frontend.qrc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/frontend.qrc"
        @ONLY
    )

    target_sources(lan-chat-gui PRIVATE
        src/web_ui_host.cpp
        src/web_ui_host.hpp
        "${CMAKE_CURRENT_BINARY_DIR}/frontend.qrc"
    )
    target_compile_definitions(lan-chat-gui PRIVATE LAN_CHAT_ENABLE_WEB_UI=1)
    target_link_libraries(lan-chat-gui PRIVATE
        Qt6::Widgets
        Qt6::WebEngineWidgets
        Qt6::WebChannel
    )

    qt_add_executable(web-ui-host-tests
        tests/web_ui_host_tests.cpp
        src/web_ui_host.cpp
        src/web_ui_host.hpp
    )

    target_link_libraries(web-ui-host-tests PRIVATE
        Qt6::Core
        Qt6::Test
        Qt6::Widgets
        Qt6::WebEngineWidgets
        Qt6::WebChannel
    )

    target_include_directories(web-ui-host-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME web-ui-host-tests COMMAND web-ui-host-tests)
    set_tests_properties(
        web-ui-host-tests
        PROPERTIES ENVIRONMENT "PATH=${LAN_CHAT_QT_TEST_PATH_ESCAPED}"
    )
endif()

set_target_properties(lan-chat-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)
