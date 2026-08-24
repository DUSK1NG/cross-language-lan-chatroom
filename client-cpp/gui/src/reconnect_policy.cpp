#include "reconnect_policy.hpp"

#include <algorithm>

namespace {
constexpr int kInitialDelayMs = 500;
constexpr int kMaximumDelayMs = 8000;
}

int ReconnectPolicy::scheduleNextAttempt() {
    if (!active_) return -1;

    const int exponent = std::min(attemptCount_, 4);
    const int delayMs = std::min(kInitialDelayMs * (1 << exponent), kMaximumDelayMs);
    ++attemptCount_;
    return delayMs;
}

void ReconnectPolicy::markConnected() {
    active_ = true;
    attemptCount_ = 0;
}

void ReconnectPolicy::cancel() {
    active_ = false;
}

bool ReconnectPolicy::isActive() const {
    return active_;
}

int ReconnectPolicy::attemptCount() const {
    return attemptCount_;
}
