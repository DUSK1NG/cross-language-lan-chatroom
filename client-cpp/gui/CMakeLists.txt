cmake_minimum_required(VERSION 3.21)

project(lan-chat-gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Test)
find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
find_package(Threads REQUIRED)

option(LAN_CHAT_ENABLE_WEB_UI "Build the React WebEngine UI" OFF)

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

qt_add_qml_module(lan-chat-gui
    URI LanChatGui
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/theme/Theme.qml
        qml/controls/AppButton.qml
        qml/controls/IconButton.qml
        qml/controls/AppTextField.qml
        qml/controls/AppDialog.qml
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
    ws2_32
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(lan-chat-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

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

qt_add_executable(chat-bridge-tests
    tests/chat_bridge_tests.cpp
    src/chat_bridge.cpp
    src/chat_bridge.hpp
    src/bridge_protocol.cpp
    src/bridge_protocol.hpp
    src/chat_model.cpp
    src/chat_model.hpp
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
    Qt6::Test
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

if(LAN_CHAT_ENABLE_WEB_UI)
    find_package(Qt6 6.5 REQUIRED COMPONENTS WebEngineWidgets WebChannel)

    set(FRONTEND_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../frontend/dist")
    if(NOT EXISTS "${FRONTEND_DIST_DIR}/index.html")
        message(FATAL_ERROR "React release UI is missing: ${FRONTEND_DIST_DIR}/index.html")
    endif()

    target_sources(lan-chat-gui PRIVATE
        src/web_ui_host.cpp
        src/web_ui_host.hpp
        resources/frontend.qrc
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
endif()

set_target_properties(lan-chat-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)
