#pragma once

class ReconnectPolicy final {
public:
    int scheduleNextAttempt();
    void markConnected();
    void cancel();

    bool isActive() const;
    int attemptCount() const;
    int generation() const;

private:
    bool active_ = true;
    int attemptCount_ = 0;
    int generation_ = 0;
};
