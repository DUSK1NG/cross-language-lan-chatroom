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
    const QDir appDir(QDir::cleanPath(applicationDirectory));
    const HostPaths repositoryPaths = pathsForRoot(appDir.filePath(QStringLiteral("../../..")));
    if (!existingFile(repositoryPaths.serverExe).isEmpty()) {
        return repositoryPaths;
    }
    return pathsForRoot(appDir.absolutePath());
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
