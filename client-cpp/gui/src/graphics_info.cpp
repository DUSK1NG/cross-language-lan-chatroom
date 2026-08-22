#include "graphics_info.hpp"

#include <QGuiApplication>
#include <QScreen>
#include <QQuickWindow>
#include <QSGRendererInterface>

namespace {
QString graphicsApiName(const QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software:
        return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG:
        return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL / RHI");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("Direct3D 11 / RHI");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan / RHI");
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal / RHI");
    case QSGRendererInterface::Null:
        return QStringLiteral("Null");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("Direct3D 12 / RHI");
    case QSGRendererInterface::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
}

bool isSoftwareApi(const QSGRendererInterface::GraphicsApi api) {
    return api == QSGRendererInterface::Software || api == QSGRendererInterface::Null;
}

bool isHardwareApi(const QSGRendererInterface::GraphicsApi api) {
    return api != QSGRendererInterface::Unknown && !isSoftwareApi(api);
}
}

GraphicsInfo::GraphicsInfo(QObject* parent)
    : QObject(parent) {}

void GraphicsInfo::attachWindow(QQuickWindow* window) {
    if (window_ == window) {
        refresh();
        return;
    }

    if (window_) {
        disconnect(window_, nullptr, this, nullptr);
    }

    window_ = window;
    if (window_) {
        connect(window_, &QQuickWindow::screenChanged, this, [this] { refresh(); });
        connect(window_, &QQuickWindow::sceneGraphInitialized, this, [this] { refresh(); });
    }

    refresh();
}

void GraphicsInfo::detachWindow() {
    attachWindow(nullptr);
}

void GraphicsInfo::refresh() {
    QSGRendererInterface::GraphicsApi api = QSGRendererInterface::Unknown;
    if (window_) {
        if (const auto* rendererInterface = window_->rendererInterface()) {
            api = rendererInterface->graphicsApi();
        }
    }

    graphicsApi_ = graphicsApiName(api);
    hardwareAcceleration_ = isHardwareApi(api);
    softwareRendering_ = isSoftwareApi(api);

    // Qt's public cross-platform renderer interface intentionally does not
    // expose vendor/renderer strings. Keep these explicit instead of using
    // private APIs or GPU-vendor-specific code.
    renderer_ = QStringLiteral("Unknown");
    vendor_ = QStringLiteral("Unknown");

    QScreen* screen = window_ ? window_->screen() : nullptr;
    if (!screen && QGuiApplication::instance()) {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen) {
        refreshRate_ = screen->refreshRate();
        dpi_ = screen->logicalDotsPerInch();
        const QSize size = screen->size();
        resolution_ = QStringLiteral("%1 × %2").arg(size.width()).arg(size.height());
    } else {
        refreshRate_ = 0.0;
        dpi_ = 0.0;
        resolution_ = QStringLiteral("Unknown");
    }

    emit changed();
}
