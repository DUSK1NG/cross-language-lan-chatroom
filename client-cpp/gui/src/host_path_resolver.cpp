#include "host_path_resolver.hpp"

#include <QDir>
#include <QFileInfo>
#include <QVector>

namespace {
QString existingFile(const QString& path) {
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return info.absoluteFilePath();
    }
    return {};
}

HostPathResolver::HostPaths pathsForRoot(const QString& root) {
    const QDir directory(root);
    return {
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/chat-server.exe"))),
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/certs/server-lan.crt"))),
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/certs/server-lan.key"))),
        QDir::cleanPath(directory.filePath(QStringLiteral("server-go/chat.db")))
    };
}
}

namespace HostPathResolver {

bool HostPaths::available() const {
    return !existingFile(serverExe).isEmpty()
        && !existingFile(certFile).isEmpty()
        && !existingFile(keyFile).isEmpty();
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
