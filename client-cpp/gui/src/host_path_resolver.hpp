#pragma once

#include <QString>

namespace HostPathResolver {

QString findPrivateKeyPath(const QString& serverExe, const QString& certFile);

} // namespace HostPathResolver
