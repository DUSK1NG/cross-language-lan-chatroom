#pragma once

#include <QString>

namespace NetworkDiagnostics {

bool enabled();
void setEnabled(bool enabled);
QString logDirectoryPath();
QString sessionLogFilePath();
QString hostServerLogFilePath();
void writeConnectionEvent(const QString& event, const QString& endpoint, int port,
                          int attempt = 0, const QString& reason = {});

}  // namespace NetworkDiagnostics
