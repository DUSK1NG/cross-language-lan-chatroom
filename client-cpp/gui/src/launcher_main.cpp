#include "host_path_resolver.hpp"
#include "local_host_bootstrap.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <windows.h>

namespace {

void showStartupError(const QString& message) {
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(message.utf16()),
                L"LAN Chat", MB_ICONERROR | MB_OK);
}

}  // namespace

// The runtime entry point intentionally stays small: a complete package first
// creates its machine-local TLS identity, then starts the WebEngine GUI. A
// member package has no server-go directory and simply forwards to the GUI.
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(applicationDirectory);

    if (hostPaths.available()) {
        const LocalHostBootstrap::Result bootstrap =
            LocalHostBootstrap::ensureInitialized(hostPaths);
        if (!bootstrap.ready) {
            showStartupError(QStringLiteral("Unable to initialize the local chat-room identity:\n%1")
                                 .arg(bootstrap.error));
            return 1;
        }
    }

    const QString guiExecutable =
        QDir(applicationDirectory).filePath(QStringLiteral("lan-chat-gui.exe"));
    if (!QFileInfo::exists(guiExecutable)) {
        showStartupError(QStringLiteral("LAN Chat graphical interface was not found:\n%1")
                             .arg(guiExecutable));
        return 1;
    }
    if (!QProcess::startDetached(guiExecutable, app.arguments().mid(1), applicationDirectory)) {
        showStartupError(QStringLiteral("Unable to start the LAN Chat graphical interface."));
        return 1;
    }
    return 0;
}
