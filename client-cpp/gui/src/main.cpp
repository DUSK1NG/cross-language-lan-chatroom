#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <winsock2.h>

#include "gui_chat_controller.hpp"

int main(int argc, char* argv[]) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("LAN Chat");
    app.setOrganizationName("DUSK1NG");
    QQuickStyle::setStyle("Fusion");

    GuiChatController chatController;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("chatController", &chatController);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("LanChatGui", "Main");
    const int exitCode = app.exec();
    WSACleanup();
    return exitCode;
}
