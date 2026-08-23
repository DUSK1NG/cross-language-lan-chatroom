#pragma once

#include <QString>

namespace HostPathResolver {

struct HostPaths {
    QString serverExe;
    QString certFile;
    QString keyFile;
    QString dbFile;

    bool available() const;
};

HostPaths resolveHostPaths(const QString& applicationDirectory);
QString findPrivateKeyPath(const QString& serverExe, const QString& certFile);

} // namespace HostPathResolver
