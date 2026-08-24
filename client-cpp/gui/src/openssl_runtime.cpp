#include "openssl_runtime.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <windows.h>

#include <mutex>

namespace OpenSslRuntime {

bool prepare(QString* error) {
    static std::once_flag configured;
    static bool ready = false;
    static QString failure;
    std::call_once(configured, [] {
        QString runtimeDirectory = qEnvironmentVariable("LAN_CHAT_OPENSSL_RUNTIME_DIR").trimmed();
        if (runtimeDirectory.isEmpty()) {
            const QString packagedRuntimeDirectory =
                QDir(QCoreApplication::applicationDirPath()).filePath("openssl");
            if (QFileInfo(packagedRuntimeDirectory).isDir()) {
                runtimeDirectory = packagedRuntimeDirectory;
            }
        }

        // Source builds may deliberately use an already configured loader
        // path. Every packaged runtime must carry both DLLs in openssl/.
        if (runtimeDirectory.isEmpty()) {
            ready = true;
            return;
        }
        const QFileInfo sslLibrary(QDir(runtimeDirectory).filePath("libssl-3-x64.dll"));
        const QFileInfo cryptoLibrary(QDir(runtimeDirectory).filePath("libcrypto-3-x64.dll"));
        if (!sslLibrary.exists() || !cryptoLibrary.exists()) {
            failure = QStringLiteral("OpenSSL runtime is missing: %1").arg(runtimeDirectory);
            return;
        }
        if (!SetDllDirectoryW(reinterpret_cast<LPCWSTR>(runtimeDirectory.utf16()))) {
            failure = QStringLiteral("Unable to prepare OpenSSL runtime directory: %1 (Windows error %2)")
                          .arg(runtimeDirectory)
                          .arg(GetLastError());
            return;
        }
        ready = true;
    });
    if (!ready && error) {
        *error = failure;
    }
    return ready;
}

}  // namespace OpenSslRuntime
