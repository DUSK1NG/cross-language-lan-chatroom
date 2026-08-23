#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <memory>
#include <winsock2.h>

#ifdef LAN_CHAT_ENABLE_WEB_UI
#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QWebEnginePage>
#include <QWebEngineView>

#include "chat_bridge.hpp"
#include "web_ui_host.hpp"
#endif

#include "gui_chat_controller.hpp"
#include "graphics_info.hpp"
#include "host_path_resolver.hpp"
#include "local_host_bootstrap.hpp"
#include "performance_profile.hpp"
#ifdef LAN_CHAT_ENABLE_PERF_OVERLAY
#include "performance_sampler.hpp"
#endif

namespace {
struct WinsockGuard final {
    ~WinsockGuard() { WSACleanup(); }
};
}

int main(int argc, char* argv[]) {
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }
    WinsockGuard winsockGuard;

#ifdef LAN_CHAT_ENABLE_WEB_UI
    QApplication app(argc, argv);
#else
    QGuiApplication app(argc, argv);
#endif
    app.setApplicationName("LAN Chat");
    app.setOrganizationName("DUSK1NG");
    QQuickStyle::setStyle("Fusion");

    GuiChatController chatController;
    GraphicsInfo graphicsInfo;
    PerformanceProfile performanceProfile;
#ifdef LAN_CHAT_ENABLE_PERF_OVERLAY
    PerformanceSampler performanceSampler;
#endif
    QObject::connect(&graphicsInfo, &GraphicsInfo::changed, &performanceProfile, [&]() {
        performanceProfile.updateGraphicsContext(graphicsInfo.hardwareAcceleration(),
                                                 graphicsInfo.softwareRendering(),
                                                 graphicsInfo.refreshRate());
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&]() {
        // Disconnect render callbacks before QML/Qt WebEngine starts tearing
        // down the window. Queued frame notifications must not outlive it.
        performanceProfile.detachWindow();
        graphicsInfo.detachWindow();
#ifdef LAN_CHAT_ENABLE_PERF_OVERLAY
        performanceSampler.detachWindow();
#endif
    }, Qt::DirectConnection);
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    const bool bundledHostAvailable = hostPaths.available();
    const LocalHostBootstrap::Result bootstrap = bundledHostAvailable
        ? LocalHostBootstrap::ensureInitialized(hostPaths)
        : LocalHostBootstrap::Result{};
    const bool hostAvailable = bundledHostAvailable && bootstrap.ready;
    const QString hostUnavailableMessage = !bundledHostAvailable
        ? QStringLiteral("此安装包不包含本地服务端；请使用主机端创建聊天室，或选择加入局域网聊天室。")
        : QStringLiteral("Local host initialization failed: ") + bootstrap.error;

    chatController.setBundledCaFile(hostAvailable ? hostPaths.certFile : QString());

    std::unique_ptr<QQmlApplicationEngine> qmlEngine;
    const auto startQml = [&]() {
        qmlEngine = std::make_unique<QQmlApplicationEngine>();
        qmlEngine->rootContext()->setContextProperty("chatController", &chatController);
        qmlEngine->rootContext()->setContextProperty("graphicsInfo", &graphicsInfo);
        qmlEngine->rootContext()->setContextProperty("performanceProfile", &performanceProfile);
#ifdef LAN_CHAT_ENABLE_PERF_OVERLAY
        qmlEngine->rootContext()->setContextProperty("performanceSampler", &performanceSampler);
#endif
        qmlEngine->rootContext()->setContextProperty("hostServerExe", hostPaths.serverExe);
        qmlEngine->rootContext()->setContextProperty("hostCertFile", hostPaths.certFile);
        qmlEngine->rootContext()->setContextProperty("hostKeyFile", hostPaths.keyFile);
        qmlEngine->rootContext()->setContextProperty("hostDbFile", hostPaths.dbFile);
        qmlEngine->rootContext()->setContextProperty("hostAvailable", hostAvailable);
        qmlEngine->rootContext()->setContextProperty("hostUnavailableMessage", hostUnavailableMessage);
        QObject::connect(qmlEngine.get(), &QQmlApplicationEngine::objectCreationFailed,
                         &app, [] { QCoreApplication::exit(-1); },
                         Qt::QueuedConnection);
        qmlEngine->loadFromModule("LanChatGui", "Main");
        if (!qmlEngine->rootObjects().isEmpty()) {
            graphicsInfo.attachWindow(qobject_cast<QQuickWindow*>(qmlEngine->rootObjects().constFirst()));
            performanceProfile.attachWindow(qobject_cast<QQuickWindow*>(qmlEngine->rootObjects().constFirst()));
            performanceProfile.updateGraphicsContext(graphicsInfo.hardwareAcceleration(),
                                                     graphicsInfo.softwareRendering(),
                                                     graphicsInfo.refreshRate());
#ifdef LAN_CHAT_ENABLE_PERF_OVERLAY
            performanceSampler.attachWindow(qobject_cast<QQuickWindow*>(qmlEngine->rootObjects().constFirst()));
#endif
        }
        return !qmlEngine->rootObjects().isEmpty();
    };

#ifdef LAN_CHAT_ENABLE_WEB_UI
    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption developmentOption(
        QStringLiteral("web-ui-dev"),
        QStringLiteral("Load the React UI from the loopback Vite development server."));
    const QCommandLineOption legacyQmlOption(
        QStringLiteral("legacy-qml"),
        QStringLiteral("Diagnostic only: start the retained Qt Quick fallback UI."));
    parser.addOption(developmentOption);
    parser.addOption(legacyQmlOption);
    parser.process(app);

    graphicsInfo.refresh();
    performanceProfile.updateGraphicsContext(graphicsInfo.hardwareAcceleration(),
                                             graphicsInfo.softwareRendering(),
                                             graphicsInfo.refreshRate());
    if (parser.isSet(legacyQmlOption)) {
        return startQml() ? app.exec() : -1;
    }

    ChatBridge bridge(&chatController, &performanceProfile, &graphicsInfo);
    bridge.setHostDefaults(hostPaths.serverExe, hostPaths.certFile, hostPaths.keyFile,
                           hostPaths.dbFile, hostAvailable,
                           hostAvailable ? QString() : hostUnavailableMessage);
    WebUiHost webUiHost;
    QWebEngineView webView;
    if (!webUiHost.registerBridge(webView.page(), &bridge)) {
        QMessageBox::critical(nullptr, QStringLiteral("LAN Chat"),
                              QStringLiteral("The modern UI bridge could not be initialized."));
        return -1;
    }

    QObject::connect(webView.page(), &QWebEnginePage::loadFinished,
                     &app, [&](const bool ok) {
        if (!ok) {
            webView.hide();
            QMessageBox::critical(nullptr, QStringLiteral("LAN Chat"),
                                  QStringLiteral("The modern UI could not be loaded. "
                                                 "Run the source launcher again to rebuild its resources."));
            QCoreApplication::exit(-1);
        }
    });

    const bool loaded = parser.isSet(developmentOption)
        ? webUiHost.loadDevelopment(&webView, QUrl("http://127.0.0.1:5173"))
        : webUiHost.loadRelease(&webView);
    if (!loaded) {
        QMessageBox::critical(nullptr, QStringLiteral("LAN Chat"),
                              QStringLiteral("The modern UI resources are unavailable."));
        return -1;
    }

    webView.resize(1280, 800);
    webView.show();
    return app.exec();
#endif

    return startQml() ? app.exec() : -1;
}
