cmake_minimum_required(VERSION 3.21)

project(lan-chat-gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
find_package(OpenSSL 3 REQUIRED COMPONENTS SSL Crypto)
find_package(Threads REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(lan-chat-gui
    src/main.cpp
    src/chat_model.cpp
    src/chat_model.hpp
    src/mock_chat_controller.cpp
    src/mock_chat_controller.hpp
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
        qml/components/TitleBar.qml
        qml/components/RoomItem.qml
        qml/components/DirectMessageItem.qml
        qml/components/ChatHeader.qml
        qml/components/MessageDelegate.qml
        qml/components/MemberItem.qml
        qml/components/MessageComposer.qml
        qml/components/UserProfilePopup.qml
        qml/pages/ModeSelectionPage.qml
        qml/pages/ConnectionPage.qml
        qml/pages/HostSetupPage.qml
        qml/pages/ChatPage.qml
        qml/pages/SettingsPage.qml
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

set_target_properties(lan-chat-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)
