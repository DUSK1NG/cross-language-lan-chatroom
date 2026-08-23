#pragma once

#include "host_path_resolver.hpp"

#include <QString>

namespace LocalHostBootstrap {

struct Result {
    bool ready = false;
    QString error;
};

// Initializes the bundled host's certificate, private key and database before
// the UI is shown. The Go process exits immediately and never opens port 8888.
Result ensureInitialized(const HostPathResolver::HostPaths& paths, int timeoutMs = 5000);

} // namespace LocalHostBootstrap
