#include <QGuiApplication>
#include <QDir>
#include <QFileInfo>
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
    const QDir appDir(QCoreApplication::applicationDirPath());
    QString packageRoot = QDir::cleanPath(appDir.filePath("../../.."));
    const QString nestedServer = QDir(packageRoot).filePath("server-go/chat-server.exe");
    if (!QFileInfo::exists(nestedServer)) {
        packageRoot = appDir.absolutePath();
    }
    engine.rootContext()->setContextProperty(
        "hostServerExe", QDir::cleanPath(QDir(packageRoot).filePath("server-go/chat-server.exe")));
    engine.rootContext()->setContextProperty(
        "hostCertFile", QDir::cleanPath(QDir(packageRoot).filePath("certs/server-lan.crt")));
    engine.rootContext()->setContextProperty(
        "hostKeyFile", QDir::cleanPath(QDir(packageRoot).filePath("certs/server-lan.key")));
    engine.rootContext()->setContextProperty(
        "hostDbFile", QDir::cleanPath(QDir(packageRoot).filePath("server-go/chat.db")));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(-1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("LanChatGui", "Main");
    const int exitCode = app.exec();
    WSACleanup();
    return exitCode;
}
