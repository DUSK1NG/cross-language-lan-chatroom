#include "graphics_info.hpp"

#include <QGuiApplication>
#include <QScreen>

GraphicsInfo::GraphicsInfo(QObject* parent)
    : QObject(parent) {}

void GraphicsInfo::refresh() {
    // QWebEngineView does not expose a Qt Quick scene graph. Keep GPU
    // fields unknown; screen diagnostics remain available through Qt Gui.
    QScreen* screen = QGuiApplication::instance()
        ? QGuiApplication::primaryScreen() : nullptr;

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
