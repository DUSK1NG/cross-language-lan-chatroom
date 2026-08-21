#include "web_ui_host.hpp"

#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>

#include "chat_bridge.hpp"

namespace {
bool isLoopbackHost(const QString& host) {
    const QString normalized = host.toLower();
    return normalized == QStringLiteral("127.0.0.1")
        || normalized == QStringLiteral("localhost")
        || normalized == QStringLiteral("::1");
}
} // namespace

bool WebUiHost::isDevelopmentUrlAllowed(const QUrl& url) {
    if (!url.isValid() || !url.userInfo().isEmpty()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return false;
    }

    return isLoopbackHost(url.host());
}

QUrl WebUiHost::releaseUrl() {
    return QUrl(QStringLiteral("qrc:/frontend/index.html"));
}

bool WebUiHost::loadDevelopment(QWebEngineView* view, const QUrl& url) const {
    if (view == nullptr || !isDevelopmentUrlAllowed(url)) {
        return false;
    }

    view->setUrl(url);
    return true;
}

bool WebUiHost::loadRelease(QWebEngineView* view) const {
    if (view == nullptr) {
        return false;
    }

    view->setUrl(releaseUrl());
    return true;
}

bool WebUiHost::registerBridge(QWebEnginePage* page, ChatBridge* bridge) const {
    if (page == nullptr || bridge == nullptr) {
        return false;
    }

    auto* channel = new QWebChannel(page);
    channel->registerObject(QStringLiteral("chatBridge"), bridge);
    page->setWebChannel(channel);
    return true;
}
