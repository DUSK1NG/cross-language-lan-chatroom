#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QVector>

class QQuickWindow;

class PerformanceSampler final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double fps READ fps NOTIFY metricsChanged)
    Q_PROPERTY(double p95FrameMs READ p95FrameMs NOTIFY metricsChanged)
    Q_PROPERTY(double p99FrameMs READ p99FrameMs NOTIFY metricsChanged)
    Q_PROPERTY(double maxFrameMs READ maxFrameMs NOTIFY metricsChanged)
    Q_PROPERTY(int sampleCount READ sampleCount NOTIFY metricsChanged)

public:
    explicit PerformanceSampler(QObject* parent = nullptr);

    double fps() const { return fps_; }
    double p95FrameMs() const { return p95FrameMs_; }
    double p99FrameMs() const { return p99FrameMs_; }
    double maxFrameMs() const { return maxFrameMs_; }
    int sampleCount() const { return frameIntervalsNs_.size(); }

    void attachWindow(QQuickWindow* window);
    void detachWindow();
    void recordFrameIntervalForTesting(qint64 intervalNs);

signals:
    void metricsChanged();

private:
    void handleFrameSwapped();
    void appendFrameInterval(qint64 intervalNs, bool refreshImmediately);
    void recomputeMetrics();

    QPointer<QQuickWindow> window_;
    QElapsedTimer frameTimer_;
    QVector<qint64> frameIntervalsNs_;
    qint64 lastFrameTimestampNs_ = 0;
    double fps_ = 0.0;
    double p95FrameMs_ = 0.0;
    double p99FrameMs_ = 0.0;
    double maxFrameMs_ = 0.0;
    int samplesSinceMetricsUpdate_ = 0;
};
