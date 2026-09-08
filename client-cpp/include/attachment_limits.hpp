#pragma once

#include <cstdint>

namespace attachments {
inline constexpr std::int64_t MaxLogicalBytes = 5LL * 1024 * 1024 * 1024;
inline constexpr std::int64_t PlaintextChunkBytes = 47 * 1024;
inline constexpr std::int64_t MaxChunkCount =
    (MaxLogicalBytes + PlaintextChunkBytes - 1) / PlaintextChunkBytes;
}  // namespace attachments
