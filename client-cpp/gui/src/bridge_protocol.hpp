#pragma once

#include <QJsonObject>
#include <QString>

namespace bridge {

bool validateCommand(const QJsonObject& command, QString* errorCode = nullptr);

QJsonObject makeCommandResult(const QString& commandId, bool ok,
                              const QJsonObject& error = {});

QJsonObject makeError(const QString& code, const QString& message, bool retryable,
                      const QString& source, const QString& commandId = {});

QString serializeState(const QJsonObject& state);

}  // namespace bridge
