#pragma once

#include <QString>

namespace DeviceIdentity {

// Returns a stable, per-host opaque device credential. The supplied CA file
// scopes the credential to the exact certificate, and the persisted value is
// encrypted for the current Windows user with DPAPI.
bool loadOrCreate(const QString& certificateFile, QString* token, QString* error = nullptr);

} // namespace DeviceIdentity
