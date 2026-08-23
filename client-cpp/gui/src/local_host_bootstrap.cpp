#include "local_host_bootstrap.hpp"

#include <QFileInfo>
#include <QProcess>

namespace {
bool isFile(const QString& path) {
    const QFileInfo info(path);
    return info.exists() && info.isFile();
}

QString processDetail(QProcess& process) {
    QString detail = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    const QString output = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        if (!detail.isEmpty()) detail += QStringLiteral("\n");
        detail += output;
    }
    return detail;
}
} // namespace

namespace LocalHostBootstrap {

Result ensureInitialized(const HostPathResolver::HostPaths& paths, const int timeoutMs) {
    if (!isFile(paths.serverExe)) {
        return {false, QStringLiteral("Local Go Server executable is missing.")};
    }
    if (isFile(paths.certFile) && isFile(paths.keyFile) && isFile(paths.dbFile)) {
        return {true, {}};
    }

    QProcess bootstrap;
    bootstrap.setProcessChannelMode(QProcess::SeparateChannels);
    bootstrap.setProgram(paths.serverExe);
    bootstrap.setWorkingDirectory(QFileInfo(paths.serverExe).absolutePath());
    bootstrap.setArguments({QStringLiteral("-initialize-local-host"), QStringLiteral("-cert"), paths.certFile,
                            QStringLiteral("-key"), paths.keyFile, QStringLiteral("-db"), paths.dbFile});
    bootstrap.start();
    if (!bootstrap.waitForStarted(qMax(1, timeoutMs))) {
        return {false, QStringLiteral("Unable to start local host initialization: ") + bootstrap.errorString()};
    }
    if (!bootstrap.waitForFinished(qMax(1, timeoutMs))) {
        bootstrap.kill();
        bootstrap.waitForFinished(1000);
        return {false, QStringLiteral("Local host initialization timed out.")};
    }

    const QString detail = processDetail(bootstrap);
    if (bootstrap.exitStatus() != QProcess::NormalExit || bootstrap.exitCode() != 0) {
        return {false, QStringLiteral("Local host initialization failed") +
                           (detail.isEmpty() ? QString() : QStringLiteral(": ") + detail)};
    }
    if (!isFile(paths.certFile) || !isFile(paths.keyFile) || !isFile(paths.dbFile)) {
        return {false, QStringLiteral("Local host initialization completed without creating all required files.")};
    }
    return {true, {}};
}

} // namespace LocalHostBootstrap
