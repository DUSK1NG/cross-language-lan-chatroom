#pragma once

#include <QUrl>

class ChatBridge;
class QWebChannel;
class QWebEnginePage;
class QWebEngineView;

class WebUiHost final {
public:
    static bool isDevelopmentUrlAllowed(const QUrl& url);
    static QUrl releaseUrl();

    bool loadDevelopment(QWebEngineView* view, const QUrl& url) const;
    bool loadRelease(QWebEngineView* view) const;
    bool registerBridge(QWebEnginePage* page, ChatBridge* bridge) const;
};
