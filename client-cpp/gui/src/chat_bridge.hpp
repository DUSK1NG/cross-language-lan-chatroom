#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <memory>

#include "lan_discovery_service.hpp"

class QAbstractItemModel;
class GuiChatController;
class GraphicsInfo;
class PerformanceProfile;

class ChatBridge final : public QObject {
    Q_OBJECT

public:
    explicit ChatBridge(GuiChatController* controller, QObject* parent = nullptr);
    ChatBridge(GuiChatController* controller, PerformanceProfile* performanceProfile,
               GraphicsInfo* graphicsInfo, QObject* parent = nullptr);

    void setHostDefaults(const QString& serverExe, const QString& certFile,
                         const QString& keyFile, const QString& dbFile,
                         bool available, const QString& unavailableReason = {});
    Q_INVOKABLE QString currentStateJson() const { return latestStateJson_; }
    qint64 lastStateBuildDurationUs() const { return lastStateBuildDurationUs_; }
    int lastSerializedModelCount() const { return lastSerializedModelCount_; }
    int cachedSerializedModelCount() const { return serializedModels_.size(); }
    int stateBuildCount() const { return stateBuildCount_; }
    Q_INVOKABLE void dispatch(const QString& commandJson);

signals:
    void stateChanged(const QString& stateJson);
    void commandResult(const QString& resultJson);
    void bridgeError(const QString& errorJson);

private:
    void scheduleStateUpdate();
    void publishState();
    void connectModel(QAbstractItemModel* model, bool scheduleUpdate = true);
    void markModelDirty(QAbstractItemModel* model, bool scheduleUpdate = true);
    void rebuildStateSnapshot();
    void handleConnectionError(const QString& code, const QString& reason, bool retryable);
    QJsonObject buildState();
    QJsonObject buildActiveConversation() const;
    void emitInvalidCommand(const QString& commandId = {},
                            const QString& code = QStringLiteral("invalid_command"));
    void completeRecallCommand(const QString& commandId, bool ok, const QString& reason = {});

    GuiChatController* controller_;
    PerformanceProfile* performanceProfile_ = nullptr;
    GraphicsInfo* graphicsInfo_ = nullptr;
    std::unique_ptr<LanDiscoveryService> lanDiscovery_;
    QTimer stateTimer_;
    QString latestStateJson_;
    QJsonObject hostDefaults_;
    QJsonObject lastError_;
    QHash<QString, QString> pendingRecallCommandIds_;
    QHash<const QAbstractItemModel*, QJsonArray> serializedModels_;
    QSet<const QAbstractItemModel*> dirtyModels_;
    QSet<const QAbstractItemModel*> connectedModels_;
    qint64 lastStateBuildDurationUs_ = 0;
    int lastSerializedModelCount_ = 0;
    int stateBuildCount_ = 0;
};
