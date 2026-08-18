cmake_minimum_required(VERSION 3.21)

project(lan-chat-gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(lan-chat-gui
    src/main.cpp
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
        qml/pages/ChatPage.qml
        qml/pages/SettingsPage.qml
)

target_link_libraries(lan-chat-gui PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
)

set_target_properties(lan-chat-gui PROPERTIES
    WIN32_EXECUTABLE TRUE
)
