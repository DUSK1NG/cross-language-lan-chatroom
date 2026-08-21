#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <memory>
#include <winsock2.h>

#ifdef LAN_CHAT_ENABLE_WEB_UI
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QWebEnginePage>
#include <QWebEngineView>

#include "chat_bridge.hpp"
#include "web_ui_host.hpp"
#endif

#include "gui_chat_controller.hpp"

int main(int argc, char* argv[]) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }

#ifdef LAN_CHAT_ENABLE_WEB_UI
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif
    app.setApplicationName("LAN Chat");
    app.setOrganizationName("DUSK1NG");
    QQuickStyle::setStyle("Fusion");

    GuiChatController chatController;
    const QDir appDir(QCoreApplication::applicationDirPath());
    QString packageRoot = QDir::cleanPath(appDir.filePath("../../.."));
    const QString nestedServer = QDir(packageRoot).filePath("server-go/chat-server.exe");
    if (!QFileInfo::exists(nestedServer)) {
        packageRoot = appDir.absolutePath();
    }

    chatController.setBundledCaFile(
        QDir::cleanPath(QDir(packageRoot).filePath("certs/server-lan.crt")));

    std::unique_ptr<QQmlApplicationEngine> qmlEngine;
    const auto startQml = [&]() {
        qmlEngine = std::make_unique<QQmlApplicationEngine>();
        qmlEngine->rootContext()->setContextProperty("chatController", &chatController);
        qmlEngine->rootContext()->setContextProperty(
            "hostServerExe", QDir::cleanPath(QDir(packageRoot).filePath("server-go/chat-server.exe")));
        qmlEngine->rootContext()->setContextProperty(
            "hostCertFile", QDir::cleanPath(QDir(packageRoot).filePath("certs/server-lan.crt")));
        qmlEngine->rootContext()->setContextProperty(
            "hostKeyFile", QDir::cleanPath(QDir(packageRoot).filePath("certs/server-lan.key")));
        qmlEngine->rootContext()->setContextProperty(
            "hostDbFile", QDir::cleanPath(QDir(packageRoot).filePath("server-go/chat.db")));
        QObject::connect(qmlEngine.get(), &QQmlApplicationEngine::objectCreationFailed,
                         &app, [] { QCoreApplication::exit(-1); },
                         Qt::QueuedConnection);
        qmlEngine->loadFromModule("LanChatGui", "Main");
        return !qmlEngine->rootObjects().isEmpty();
    };

#ifdef LAN_CHAT_ENABLE_WEB_UI
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption developmentOption(
        QStringLiteral("web-ui-dev"),
        QStringLiteral("Load the React UI from the loopback Vite development server."));
    parser.addOption(developmentOption);
    parser.process(app);

    ChatBridge bridge(&chatController);
    WebUiHost webUiHost;
    QWebEngineView webView;
    const bool bridgeRegistered = webUiHost.registerBridge(webView.page(), &bridge);
    if (bridgeRegistered) {
        QObject::connect(webView.page(), &QWebEnginePage::loadFinished,
                         &app, [&](const bool ok) {
            if (!ok && qmlEngine == nullptr) {
                webView.hide();
                startQml();
            }
        });

        const bool loaded = parser.isSet(developmentOption)
            ? webUiHost.loadDevelopment(&webView, QUrl("http://127.0.0.1:5173"))
            : webUiHost.loadRelease(&webView);
        if (loaded) {
            webView.resize(1280, 800);
            webView.show();
            const int exitCode = app.exec();
            WSACleanup();
            return exitCode;
        }
    }
#endif

    startQml();
    const int exitCode = app.exec();
    WSACleanup();
    return exitCode;
}
