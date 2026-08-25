#include "network_diagnostics.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString diagnosticDirectory() {
    const QString overrideDirectory = qEnvironmentVariable("LAN_CHAT_DIAGNOSTICS_DIR");
    if (!overrideDirectory.isEmpty()) return QDir::cleanPath(overrideDirectory);
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                           QStringLiteral("/logs"));
}

bool environmentOverride(bool* hasOverride) {
    const QByteArray value = qgetenv("LAN_CHAT_DIAGNOSTICS").trimmed().toLower();
    *hasOverride = !value.isEmpty();
    return value == "1" || value == "true" || value == "yes";
}

QString sessionId() {
    static const QString value = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ")) +
                                 QStringLiteral("-") + QString::number(QCoreApplication::applicationPid());
    return value;
}

QString logPath(const QString& prefix) {
    if (!NetworkDiagnostics::enabled()) return {};
    return QDir(diagnosticDirectory()).filePath(prefix + QStringLiteral("-") + sessionId() + QStringLiteral(".log"));
}

QString oneLine(QString value) {
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    return value.trimmed().left(512);
}

QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}

}  // namespace

namespace NetworkDiagnostics {

bool enabled() {
    bool hasOverride = false;
    const bool overrideValue = environmentOverride(&hasOverride);
    if (hasOverride) return overrideValue;

    QSettings settings;
    return settings.value(QStringLiteral("networkDiagnostics/enabled"), false).toBool();
}

void setEnabled(const bool enabled) {
    QSettings settings;
    settings.setValue(QStringLiteral("networkDiagnostics/enabled"), enabled);
    settings.sync();
}

QString logDirectoryPath() {
    return diagnosticDirectory();
}

QString sessionLogFilePath() {
    return logPath(QStringLiteral("network"));
}

QString hostServerLogFilePath() {
    return logPath(QStringLiteral("host-server"));
}

void writeConnectionEvent(const QString& event, const QString& endpoint, const int port,
                          const int attempt, const QString& reason) {
    const QString path = sessionLogFilePath();
    if (path.isEmpty()) return;

    QMutexLocker lock(&logMutex());
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;

    QTextStream output(&file);
    output << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
           << " event=" << oneLine(event)
           << " endpoint=" << oneLine(endpoint) << ':' << port
           << " attempt=" << attempt;
    if (!reason.trimmed().isEmpty()) output << " reason=" << oneLine(reason);
    output << '\n';
}

}  // namespace NetworkDiagnostics
