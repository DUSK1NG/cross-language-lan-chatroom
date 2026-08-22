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
}

namespace HostPathResolver {

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
