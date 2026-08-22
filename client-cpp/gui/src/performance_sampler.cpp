#include "performance_sampler.hpp"

#include <QQuickWindow>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaxSamples = 240;
constexpr int kMetricsUpdateStride = 8;

double percentileMs(const QVector<qint64>& samples, double percentile) {
    if (samples.isEmpty()) return 0.0;

    QVector<qint64> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const double position = percentile * static_cast<double>(sorted.size() - 1);
    const int lastIndex = static_cast<int>(sorted.size()) - 1;
    const int index = std::clamp(static_cast<int>(std::ceil(position)), 0, lastIndex);
    return static_cast<double>(sorted.at(index)) / 1'000'000.0;
}
}

PerformanceSampler::PerformanceSampler(QObject* parent) : QObject(parent) {}

void PerformanceSampler::attachWindow(QQuickWindow* window) {
    if (window_ == window) return;
    if (window_) disconnect(window_, nullptr, this, nullptr);

    window_ = window;
    lastFrameTimestampNs_ = 0;
    frameTimer_.invalidate();
    frameIntervalsNs_.clear();
    samplesSinceMetricsUpdate_ = 0;
    recomputeMetrics();
    if (!window_) return;

    connect(window_, &QQuickWindow::frameSwapped, this,
            &PerformanceSampler::handleFrameSwapped, Qt::QueuedConnection);
}

void PerformanceSampler::detachWindow() {
    attachWindow(nullptr);
}

void PerformanceSampler::handleFrameSwapped() {
    if (!window_) return;
    if (!frameTimer_.isValid()) frameTimer_.start();

    const qint64 nowNs = frameTimer_.nsecsElapsed();
    if (lastFrameTimestampNs_ > 0) {
        appendFrameInterval(nowNs - lastFrameTimestampNs_, false);
    }
    lastFrameTimestampNs_ = nowNs;
}

void PerformanceSampler::recordFrameIntervalForTesting(qint64 intervalNs) {
    appendFrameInterval(intervalNs, true);
}

void PerformanceSampler::appendFrameInterval(qint64 intervalNs, bool refreshImmediately) {
    if (intervalNs <= 0) return;
    frameIntervalsNs_.append(intervalNs);
    while (frameIntervalsNs_.size() > kMaxSamples) frameIntervalsNs_.removeFirst();
    ++samplesSinceMetricsUpdate_;
    if (refreshImmediately || samplesSinceMetricsUpdate_ >= kMetricsUpdateStride) {
        samplesSinceMetricsUpdate_ = 0;
        recomputeMetrics();
    }
}

void PerformanceSampler::recomputeMetrics() {
    if (frameIntervalsNs_.isEmpty()) {
        fps_ = 0.0;
        p95FrameMs_ = 0.0;
        p99FrameMs_ = 0.0;
        maxFrameMs_ = 0.0;
        emit metricsChanged();
        return;
    }

    qint64 totalNs = 0;
    qint64 maxNs = 0;
    for (const qint64 intervalNs : frameIntervalsNs_) {
        totalNs += intervalNs;
        maxNs = std::max(maxNs, intervalNs);
    }
    fps_ = totalNs > 0
        ? 1'000'000'000.0 * static_cast<double>(frameIntervalsNs_.size()) /
              static_cast<double>(totalNs)
        : 0.0;
    p95FrameMs_ = percentileMs(frameIntervalsNs_, 0.95);
    p99FrameMs_ = percentileMs(frameIntervalsNs_, 0.99);
    maxFrameMs_ = static_cast<double>(maxNs) / 1'000'000.0;
    emit metricsChanged();
}
