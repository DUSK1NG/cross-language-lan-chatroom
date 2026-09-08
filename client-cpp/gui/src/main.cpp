#include <winsock2.h>

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QWebEnginePage>
#include <QWebEngineView>

#include "chat_bridge.hpp"
#include "web_ui_host.hpp"

#include "gui_chat_controller.hpp"
#include "graphics_info.hpp"
#include "host_path_resolver.hpp"
#include "local_host_bootstrap.hpp"
#include "performance_profile.hpp"

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

    QApplication app(argc, argv);
    app.setApplicationName("LAN Chat");
    app.setOrganizationName("DUSK1NG");
    GuiChatController chatController;
    GraphicsInfo graphicsInfo;
    PerformanceProfile performanceProfile;
    QObject::connect(&graphicsInfo, &GraphicsInfo::changed, &performanceProfile, [&]() {
        performanceProfile.updateGraphicsContext(graphicsInfo.hardwareAcceleration(),
                                                 graphicsInfo.softwareRendering(),
                                                 graphicsInfo.refreshRate());
    });
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

    QCommandLineParser parser;
    parser.addHelpOption();
    const QCommandLineOption developmentOption(
        QStringLiteral("web-ui-dev"),
        QStringLiteral("Load the React UI from the loopback Vite development server."));
    parser.addOption(developmentOption);
    parser.process(app);

    graphicsInfo.refresh();
    performanceProfile.updateGraphicsContext(graphicsInfo.hardwareAcceleration(),
                                             graphicsInfo.softwareRendering(),
                                             graphicsInfo.refreshRate());
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
}
