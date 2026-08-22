#include "performance_profile.hpp"

#include <QQuickWindow>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kFrameWindowSize = 24;
constexpr int kAutomaticModeConfirmations = 3;
constexpr double kBalancedFrameThresholdMs = 24.0;
constexpr double kPowerSavingFrameThresholdMs = 40.0;

double p95(const QVector<double>& samples) {
    if (samples.isEmpty()) return 0.0;
    QVector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const int lastIndex = static_cast<int>(sorted.size()) - 1;
    const int index = std::clamp(
        static_cast<int>(std::ceil(0.95 * static_cast<double>(lastIndex))),
        0,
        lastIndex);
    return sorted.at(index);
}
}

PerformanceProfile::PerformanceProfile(QObject* parent) : QObject(parent) {
    QSettings settings;
    settings.beginGroup(QStringLiteral("performance"));
    mode_ = normalizeMode(settings.value(QStringLiteral("mode"), mode_).toString());
    settings.endGroup();
    recompute();
}

QString PerformanceProfile::normalizeMode(const QString& mode) {
    const QString value = mode.trimmed();
    if (value == QStringLiteral("High") ||
        value == QStringLiteral("Balanced") ||
        value == QStringLiteral("Power Saving")) {
        return value;
    }
    return QStringLiteral("Automatic");
}

void PerformanceProfile::setMode(const QString& mode) {
    const QString normalized = normalizeMode(mode);
    if (mode_ == normalized) return;

    mode_ = normalized;
    QSettings settings;
    settings.beginGroup(QStringLiteral("performance"));
    settings.setValue(QStringLiteral("mode"), mode_);
    settings.endGroup();
    frameTimesMs_.clear();
    pendingAutomaticMode_.clear();
    pendingAutomaticModeStreak_ = 0;
    recomputeMetrics();
    emit observedFrameCountChanged();
    emit metricsChanged();
    emit modeChanged();
    recompute();
}

void PerformanceProfile::attachWindow(QQuickWindow* window) {
    if (window_ == window) return;

    if (window_) {
        disconnect(window_, &QQuickWindow::frameSwapped,
                   this, &PerformanceProfile::handleFrameSwapped);
    }

    window_ = window;
    frameTimer_.invalidate();
    lastFrameTimestampNs_ = 0;
    framesSinceSample_ = 0;

    if (window_) {
        connect(window_, &QQuickWindow::frameSwapped,
                this, &PerformanceProfile::handleFrameSwapped,
                Qt::QueuedConnection);
    }
}

void PerformanceProfile::detachWindow() {
    attachWindow(nullptr);
}

void PerformanceProfile::updateGraphicsContext(const bool hardwareAcceleration,
                                                const bool softwareRendering,
                                                const double refreshRate) {
    hardwareAcceleration_ = hardwareAcceleration;
    softwareRendering_ = softwareRendering;
    refreshRate_ = refreshRate > 0.0 ? refreshRate : 60.0;
    recompute();
}

void PerformanceProfile::observeFrameTime(const double frameMs) {
    if (mode_ != QStringLiteral("Automatic") || !std::isfinite(frameMs) || frameMs <= 0.0) {
        return;
    }
    frameTimesMs_.append(frameMs);
    while (frameTimesMs_.size() > kFrameWindowSize) frameTimesMs_.removeFirst();
    recomputeMetrics();
    emit observedFrameCountChanged();
    emit metricsChanged();
    recompute();
}

void PerformanceProfile::handleFrameSwapped() {
    if (!window_) return;
    if (!frameTimer_.isValid()) frameTimer_.start();

    const qint64 nowNs = frameTimer_.nsecsElapsed();
    if (lastFrameTimestampNs_ == 0) {
        lastFrameTimestampNs_ = nowNs;
        return;
    }

    ++framesSinceSample_;
    if (framesSinceSample_ < 4) return;

    const double frameMs = static_cast<double>(nowNs - lastFrameTimestampNs_) /
        1'000'000.0 / static_cast<double>(framesSinceSample_);
    lastFrameTimestampNs_ = nowNs;
    framesSinceSample_ = 0;
    observeFrameTime(frameMs);
}

QString PerformanceProfile::automaticMode() const {
    if (softwareRendering_ || !hardwareAcceleration_) return QStringLiteral("Power Saving");
    if (refreshRate_ <= 60.0) return QStringLiteral("Balanced");

    if (frameTimesMs_.size() < kFrameWindowSize) return QStringLiteral("High");

    const double observedP95 = observedP95FrameMs_;
    if (observedP95 >= kPowerSavingFrameThresholdMs) return QStringLiteral("Power Saving");
    if (observedP95 >= kBalancedFrameThresholdMs) return QStringLiteral("Balanced");
    return QStringLiteral("High");
}

QString PerformanceProfile::automaticReasonForContext() const {
    if (softwareRendering_) return QStringLiteral("software-rendering");
    if (!hardwareAcceleration_) return QStringLiteral("no-hardware-acceleration");
    if (refreshRate_ <= 60.0) return QStringLiteral("refresh-rate-limit");
    if (frameTimesMs_.size() < kFrameWindowSize) return QStringLiteral("waiting-for-samples");
    if (observedP95FrameMs_ >= kPowerSavingFrameThresholdMs) return QStringLiteral("p95-over-40ms");
    if (observedP95FrameMs_ >= kBalancedFrameThresholdMs) return QStringLiteral("p95-over-24ms");
    return QStringLiteral("stable");
}

void PerformanceProfile::recomputeMetrics() {
    if (frameTimesMs_.isEmpty()) {
        observedFps_ = 0.0;
        observedP95FrameMs_ = 0.0;
        observedMaxFrameMs_ = 0.0;
        return;
    }

    double totalMs = 0.0;
    observedMaxFrameMs_ = 0.0;
    for (const double frameMs : frameTimesMs_) {
        totalMs += frameMs;
        observedMaxFrameMs_ = std::max(observedMaxFrameMs_, frameMs);
    }
    observedFps_ = totalMs > 0.0
        ? 1'000.0 * static_cast<double>(frameTimesMs_.size()) / totalMs
        : 0.0;
    observedP95FrameMs_ = p95(frameTimesMs_);
}

void PerformanceProfile::recompute() {
    QString nextEffective = mode_;
    if (mode_ == QStringLiteral("Automatic")) {
        const QString candidate = automaticMode();
        const bool hardLimit = softwareRendering_ || !hardwareAcceleration_ || refreshRate_ <= 60.0;
        if (hardLimit || frameTimesMs_.size() < kFrameWindowSize || candidate == effectiveMode_) {
            pendingAutomaticMode_.clear();
            pendingAutomaticModeStreak_ = 0;
            nextEffective = candidate;
        } else {
            if (pendingAutomaticMode_ == candidate) {
                ++pendingAutomaticModeStreak_;
            } else {
                pendingAutomaticMode_ = candidate;
                pendingAutomaticModeStreak_ = 1;
            }
            nextEffective = pendingAutomaticModeStreak_ >= kAutomaticModeConfirmations
                ? candidate : effectiveMode_;
            if (nextEffective == candidate) {
                pendingAutomaticMode_.clear();
                pendingAutomaticModeStreak_ = 0;
            }
        }
    }

    const QString nextReason = mode_ == QStringLiteral("Automatic")
        ? automaticReasonForContext() : QStringLiteral("manual-selection");
    const bool nextEffects = nextEffective != QStringLiteral("Power Saving");
    const bool nextAnimations = nextEffective != QStringLiteral("Power Saving");
    const bool nextGradients = nextEffective != QStringLiteral("Power Saving");
    const double nextScale = nextEffective == QStringLiteral("High")
        ? 1.0 : (nextEffective == QStringLiteral("Balanced") ? 0.75 : 0.0);

    const bool effectiveChanged = effectiveMode_ != nextEffective;
    const bool capabilitiesDidChange = effectsEnabled_ != nextEffects ||
        animationsEnabled_ != nextAnimations || gradientsEnabled_ != nextGradients ||
        animationDurationScale_ != nextScale;
    const bool reasonChanged = automaticReason_ != nextReason;

    effectiveMode_ = nextEffective;
    automaticReason_ = nextReason;
    effectsEnabled_ = nextEffects;
    animationsEnabled_ = nextAnimations;
    gradientsEnabled_ = nextGradients;
    animationDurationScale_ = nextScale;

    if (effectiveChanged) emit effectiveModeChanged();
    if (capabilitiesDidChange) emit capabilitiesChanged();
    if (reasonChanged) emit automaticReasonChanged();
}
