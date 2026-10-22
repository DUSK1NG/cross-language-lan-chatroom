#pragma once

#include <QUrl>
#include <QWebEnginePage>

class ChatBridge;
class QWebChannel;
class QWebEngineView;

class WebUiHost final {
public:
    static bool isDevelopmentUrlAllowed(const QUrl& url);
    static QUrl releaseUrl();
    /** A link clicked in a message is handed to the system browser only when it is plain http(s). */
    static bool opensExternally(const QUrl& url);

    bool loadDevelopment(QWebEngineView* view, const QUrl& url) const;
    bool loadRelease(QWebEngineView* view) const;
    bool registerBridge(QWebEnginePage* page, ChatBridge* bridge) const;
};

/**
 * The chat page never navigates away from the bundled UI: clicked links open in the
 * system browser (http/https only) and every other clicked link is ignored.
 */
class ExternalLinkPage final : public QWebEnginePage {
public:
    explicit ExternalLinkPage(QObject* parent = nullptr);

protected:
    bool acceptNavigationRequest(const QUrl& url, NavigationType type, bool isMainFrame) override;
};
