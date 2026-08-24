#pragma once

#include <QString>

namespace OpenSslRuntime {

// Selects the dedicated OpenSSL directory before the first delayed import.
// The packaged runtime keeps OpenSSL outside the application root so Qt
// WebEngine cannot load that copy while initializing the UI.
bool prepare(QString* error = nullptr);

}  // namespace OpenSslRuntime
