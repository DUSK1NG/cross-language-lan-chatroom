#include "host_path_resolver.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVector>

namespace {
QString existingFile(const QString& path) {
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }
    return {};
}

HostPathResolver::HostPaths legacyPathsForRoot(const QString& root) {
    const QDir directory(root);
    return {
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/chat-server.exe"))),
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/certs/server-lan.crt"))),
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/certs/server-lan.key"))),
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/chat.db")))
    };
}

QString protectedHostDataRoot() {
    // An isolated package smoke test can redirect only its child process to a
    // temporary location. Regular users never need to set this variable.
    const QString testDataRoot = qEnvironmentVariable("LAN_CHAT_TEST_HOST_DATA_ROOT").trimmed();
    if (!testDataRoot.isEmpty()) {
        return QDir::cleanPath(testDataRoot);
    }

    // The lightweight launcher and the GUI have different executable names,
    // so AppLocalDataLocation would resolve to two different directories.
    // Build the user-scoped path from GenericDataLocation explicitly instead.
    const QString localData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir::cleanPath(QDir(localData).filePath(QStringLiteral("DUSK1NG/LAN Chat/host")));
}

HostPathResolver::HostPaths pathsForRoot(const QString& root) {
    const HostPathResolver::HostPaths legacy = legacyPathsForRoot(root);
    // A test root is an explicit isolation boundary. It must win even when a
    // developer checkout happens to contain a legacy certificate or database.
    const QString testDataRoot = qEnvironmentVariable("LAN_CHAT_TEST_HOST_DATA_ROOT").trimmed();
    if (!testDataRoot.isEmpty()) {
        const QDir dataDirectory(QDir::cleanPath(testDataRoot));
        return {
            legacy.serverExe,
            QDir::cleanPath(dataDirectory.filePath(QStringLiteral("certs/server-lan.crt"))),
            QDir::cleanPath(dataDirectory.filePath(QStringLiteral("certs/server-lan.key"))),
            QDir::cleanPath(dataDirectory.filePath(QStringLiteral("chat.db")))
        };
    }
    // Keep an existing room identity in place for compatibility with earlier
    // releases. New identities are deliberately kept under the current
    // Windows user's AppLocalData directory instead of a shared install path.
    if (QFileInfo::exists(legacy.certFile) || QFileInfo::exists(legacy.keyFile) || QFileInfo::exists(legacy.dbFile)) {
        return legacy;
    }

    const QDir dataDirectory(protectedHostDataRoot());
    return {
        legacy.serverExe,
        QDir::cleanPath(dataDirectory.filePath(QStringLiteral("certs/server-lan.crt"))),
        QDir::cleanPath(dataDirectory.filePath(QStringLiteral("certs/server-lan.key"))),
        QDir::cleanPath(dataDirectory.filePath(QStringLiteral("chat.db")))
    };
}
}

namespace HostPathResolver {

bool HostPaths::available() const {
    // A unified runtime package ships the server but intentionally ships no
    // host certificate or private key. The server receives -auto-cert and
    // creates that pair locally on the first host start, so only the server
    // executable determines whether Local Host is available.
    return !existingFile(serverExe).isEmpty();
}

HostPaths resolveHostPaths(const QString& applicationDirectory) {
    QDir candidateRoot(QDir::cleanPath(applicationDirectory));

    // The official source build lives in <repository>/out/modern-msvc-x64,
    // while the legacy build directory lived under client-cpp/gui. Walk up
    // from the executable instead of encoding either build layout.
    for (int depth = 0; depth < 8; ++depth) {
        const HostPaths candidatePaths = pathsForRoot(candidateRoot.absolutePath());
        if (!existingFile(candidatePaths.serverExe).isEmpty()) {
            return candidatePaths;
        }
        if (!candidateRoot.cdUp()) {
            break;
        }
    }
    return pathsForRoot(QDir::cleanPath(applicationDirectory));
}

QString findPrivateKeyPath(const QString& serverExe, const QString& certFile) {
    QVector<QString> candidates;

    if (!certFile.trimmed().isEmpty()) {
        const QFileInfo certInfo(certFile.trimmed());
        const QString certificateBaseName = certInfo.completeBaseName();
        if (!certificateBaseName.isEmpty()) {
            candidates.append(certInfo.dir().filePath(certificateBaseName + QStringLiteral(".key")));
        }
        candidates.append(certInfo.dir().filePath(QStringLiteral("server-lan.key")));
    }

    if (!serverExe.trimmed().isEmpty()) {
        const QFileInfo serverInfo(serverExe.trimmed());
        candidates.append(serverInfo.dir().filePath(QStringLiteral("certs/server-lan.key")));
        candidates.append(serverInfo.dir().filePath(QStringLiteral("server-lan.key")));
    }

    for (const QString& candidate : candidates) {
        const QString resolved = existingFile(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }
    return {};
}

} // namespace HostPathResolver
