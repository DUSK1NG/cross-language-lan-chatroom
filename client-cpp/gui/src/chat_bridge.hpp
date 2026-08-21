#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class QAbstractItemModel;
class GuiChatController;

class ChatBridge final : public QObject {
    Q_OBJECT

public:
    explicit ChatBridge(GuiChatController* controller, QObject* parent = nullptr);

    Q_INVOKABLE QString currentStateJson() const { return latestStateJson_; }
    Q_INVOKABLE void dispatch(const QString& commandJson);

signals:
    void stateChanged(const QString& stateJson);
    void commandResult(const QString& resultJson);
    void bridgeError(const QString& errorJson);

private:
    void scheduleStateUpdate();
    void publishState();
    void connectModel(QAbstractItemModel* model);
    void handleConnectionError(const QString& code, const QString& reason, bool retryable);
    QJsonObject buildState() const;
    QJsonObject buildActiveConversation() const;
    void emitInvalidCommand(const QString& commandId = {},
                            const QString& code = QStringLiteral("invalid_command"));

    GuiChatController* controller_;
    QTimer stateTimer_;
    QString latestStateJson_;
    QJsonObject lastError_;
};
