#include "chat_bridge.hpp"

#include "bridge_protocol.hpp"
#include "graphics_info.hpp"
#include "gui_chat_controller.hpp"
#include "lan_discovery_service.hpp"
#include "network_diagnostics.hpp"
#include "performance_profile.hpp"

#include <QAbstractItemModel>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <memory>

namespace {
constexpr int kMaxSerializedRows = 500;
constexpr int kStatePublishIntervalMs = 100;

QJsonArray serializeModel(const QAbstractItemModel* model, int maxRows = kMaxSerializedRows) {
    QJsonArray result;
    if (!model) {
        return result;
    }

    const QHash<int, QByteArray> roles = model->roleNames();
    const int firstRow = qMax(0, model->rowCount() - maxRows);
    for (int row = firstRow; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, 0);
        QJsonObject item;
        for (auto it = roles.begin(); it != roles.end(); ++it) {
            item.insert(QString::fromUtf8(it.value()),
                        QJsonValue::fromVariant(model->data(index, it.key())));
        }
        result.append(item);
    }
    return result;
}

QString roomFromConversationKey(const QString& key) {
    return key.startsWith(QStringLiteral("room:")) ? key.mid(5) : QString();
}
}

ChatBridge::ChatBridge(GuiChatController* controller, QObject* parent)
    : ChatBridge(controller, nullptr, nullptr, parent) {}

ChatBridge::ChatBridge(GuiChatController* controller, PerformanceProfile* performanceProfile,
                       GraphicsInfo* graphicsInfo, QObject* parent)
    : QObject(parent), controller_(controller), performanceProfile_(performanceProfile),
      graphicsInfo_(graphicsInfo), lanDiscovery_(std::make_unique<LanDiscoveryService>()) {
    Q_ASSERT(controller_);
    stateTimer_.setSingleShot(true);
    stateTimer_.setInterval(kStatePublishIntervalMs);
    connect(&stateTimer_, &QTimer::timeout, this, &ChatBridge::publishState);

    connect(controller_, &GuiChatController::connectedChanged, this, [this]() {
        if (controller_->connected()) {
            lastError_ = {};
        }
        scheduleStateUpdate();
    });
    connect(controller_, &GuiChatController::reconnectingChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::adminChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::localIdentityChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::onlineMemberCountChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::statusTextChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::activeMessageModelChanged, this, [this]() {
        connectModel(controller_->activeMessageModel());
        scheduleStateUpdate();
    });
    connect(controller_, &GuiChatController::activeRoomCanManageChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::savedConnectionChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(controller_, &GuiChatController::pendingConnectionApprovalsChanged, this, &ChatBridge::scheduleStateUpdate);
    connect(lanDiscovery_.get(), &LanDiscoveryService::hostsChanged,
            this, &ChatBridge::scheduleStateUpdate);
    connect(lanDiscovery_.get(), &LanDiscoveryService::scanningChanged,
            this, &ChatBridge::scheduleStateUpdate);
    if (performanceProfile_) {
        connect(performanceProfile_, &PerformanceProfile::modeChanged,
                this, &ChatBridge::scheduleStateUpdate);
        connect(performanceProfile_, &PerformanceProfile::effectiveModeChanged,
                this, &ChatBridge::scheduleStateUpdate);
        connect(performanceProfile_, &PerformanceProfile::capabilitiesChanged,
                this, &ChatBridge::scheduleStateUpdate);
        connect(performanceProfile_, &PerformanceProfile::metricsChanged,
                this, &ChatBridge::scheduleStateUpdate);
        connect(performanceProfile_, &PerformanceProfile::automaticReasonChanged,
                this, &ChatBridge::scheduleStateUpdate);
    }
    if (graphicsInfo_) {
        connect(graphicsInfo_, &GraphicsInfo::changed,
                this, &ChatBridge::scheduleStateUpdate);
    }
    connect(controller_, &GuiChatController::connectionFailed, this, [this](const QString& reason) {
        handleConnectionError(QStringLiteral("connection_failed"), reason, true);
    });
    connect(controller_, &GuiChatController::connectionLost, this, [this](const QString& reason) {
        if (controller_->reconnecting()) {
            lastError_ = {};
            scheduleStateUpdate();
            return;
        }
        handleConnectionError(QStringLiteral("connection_lost"), reason, true);
    });
    connect(controller_, &GuiChatController::recallSucceeded, this, [this](const QString& commandId) {
        completeRecallCommand(commandId, true);
    });
    connect(controller_, &GuiChatController::recallFailed, this, [this](const QString& commandId, const QString& reason) {
        completeRecallCommand(commandId, false, reason);
    });
    connect(controller_, &GuiChatController::mlsCommandResult,
            this, [this](const QString& commandId, bool ok, const QString& code, const QString& message) {
        if (commandId.isEmpty()) return;
        QJsonObject error;
        if (!ok) {
            error = bridge::makeError(code.isEmpty() ? QStringLiteral("mls_operation_failed") : code,
                                      message.isEmpty() ? QStringLiteral("MLS operation failed") : message,
                                      false, QStringLiteral("mls"), commandId);
        }
        emit commandResult(QString::fromUtf8(
            QJsonDocument(bridge::makeCommandResult(commandId, ok, error)).toJson(QJsonDocument::Compact)));
    });

    connectModel(controller_->roomModel(), false);
    connectModel(controller_->directMessageModel(), false);
    connectModel(controller_->messageModel(), false);
    connectModel(controller_->memberModel(), false);
    connectModel(controller_->activeMessageModel(), false);
    rebuildStateSnapshot();
}

void ChatBridge::setHostDefaults(const QString& serverExe, const QString& certFile,
                                 const QString& keyFile, const QString& dbFile,
                                 const bool available, const QString& unavailableReason) {
    hostDefaults_ = QJsonObject{{"serverExe", serverExe}, {"certFile", certFile},
                                {"keyFile", keyFile}, {"dbFile", dbFile},
                                {"available", available}, {"unavailableReason", unavailableReason}};
    rebuildStateSnapshot();
}

void ChatBridge::connectModel(QAbstractItemModel* model, bool scheduleUpdate) {
    if (!model || connectedModels_.contains(model)) {
        return;
    }
    connectedModels_.insert(model);

    const auto markDirty = [this, model]() { markModelDirty(model); };
    connect(model, &QAbstractItemModel::dataChanged, this, markDirty);
    connect(model, &QAbstractItemModel::rowsInserted, this, markDirty);
    connect(model, &QAbstractItemModel::rowsRemoved, this, markDirty);
    connect(model, &QAbstractItemModel::modelReset, this, markDirty);
    connect(model, &QObject::destroyed, this, [this, model]() {
        serializedModels_.remove(model);
        dirtyModels_.remove(model);
        connectedModels_.remove(model);
    });
    markModelDirty(model, scheduleUpdate);
}

void ChatBridge::markModelDirty(QAbstractItemModel* model, bool scheduleUpdate) {
    if (!model) {
        return;
    }
    dirtyModels_.insert(model);
    if (scheduleUpdate) {
        scheduleStateUpdate();
    }
}

void ChatBridge::scheduleStateUpdate() {
    if (!stateTimer_.isActive()) {
        stateTimer_.start();
    }
}

void ChatBridge::publishState() {
    rebuildStateSnapshot();
    emit stateChanged(latestStateJson_);
}

void ChatBridge::rebuildStateSnapshot() {
    QElapsedTimer timer;
    timer.start();
    latestStateJson_ = bridge::serializeState(buildState());
    lastStateBuildDurationUs_ = timer.nsecsElapsed() / 1000;
    ++stateBuildCount_;
}

void ChatBridge::handleConnectionError(const QString& code, const QString& reason, bool retryable) {
    lastError_ = bridge::makeError(code,
                                   reason.isEmpty() ? controller_->statusText() : reason,
                                   retryable, QStringLiteral("controller"));
    const QString errorJson = QString::fromUtf8(
        QJsonDocument(lastError_).toJson(QJsonDocument::Compact));
    emit bridgeError(errorJson);
    scheduleStateUpdate();
}

QJsonObject ChatBridge::buildActiveConversation() const {
    const QString key = controller_->activeConversationKey();
    if (key.startsWith(QStringLiteral("dm:"))) {
        const QString userCode = key.mid(3);
        return QJsonObject{{"kind", "dm"}, {"id", userCode}, {"title", userCode},
                           {"userCode", userCode}};
    }

    const QString room = roomFromConversationKey(key);
    return QJsonObject{{"kind", "room"}, {"id", room}, {"title", room}};
}

QJsonObject ChatBridge::buildState() {
    lastSerializedModelCount_ = dirtyModels_.size();
    for (const QAbstractItemModel* model : dirtyModels_) {
        serializedModels_.insert(model, serializeModel(model));
    }
    dirtyModels_.clear();

    const auto snapshotFor = [this](const QAbstractItemModel* model) {
        return serializedModels_.value(model);
    };
    QString phase = QStringLiteral("idle");
    if (controller_->connected()) {
        phase = QStringLiteral("connected");
    } else if (controller_->reconnecting()) {
        phase = QStringLiteral("reconnecting");
    } else if (!lastError_.isEmpty()) {
        phase = QStringLiteral("error");
    } else if (controller_->statusText() != QStringLiteral("未连接")) {
        phase = QStringLiteral("connecting");
    }

    QJsonObject connection{{"phase", phase},
                           {"statusText", controller_->statusText()},
                           {"retryable", !lastError_.isEmpty() && lastError_.value("retryable").toBool()}};
    if (!lastError_.isEmpty()) {
        connection.insert(QStringLiteral("lastError"), lastError_);
    }

    QJsonObject performance;
    if (performanceProfile_) {
        performance = QJsonObject{
            {"mode", performanceProfile_->mode()},
            {"effectiveMode", performanceProfile_->effectiveMode()},
            {"effectsEnabled", performanceProfile_->effectsEnabled()},
            {"animationsEnabled", performanceProfile_->animationsEnabled()},
            {"gradientsEnabled", performanceProfile_->gradientsEnabled()},
            {"animationDurationScale", performanceProfile_->animationDurationScale()},
            {"observedFrameCount", performanceProfile_->observedFrameCount()},
            {"observedFps", performanceProfile_->observedFps()},
            {"observedP95FrameMs", performanceProfile_->observedP95FrameMs()},
            {"observedMaxFrameMs", performanceProfile_->observedMaxFrameMs()},
            {"automaticReason", performanceProfile_->automaticReason()}
        };
    }

    QJsonObject graphics;
    if (graphicsInfo_) {
        graphics = QJsonObject{
            {"graphicsApi", graphicsInfo_->graphicsApi()},
            {"renderer", graphicsInfo_->renderer()},
            {"vendor", graphicsInfo_->vendor()},
            {"hardwareAcceleration", graphicsInfo_->hardwareAcceleration()},
            {"softwareRendering", graphicsInfo_->softwareRendering()},
            {"refreshRate", graphicsInfo_->refreshRate()},
            {"dpi", graphicsInfo_->dpi()},
            {"resolution", graphicsInfo_->resolution()}
        };
    }

    const QString activeRoom = roomFromConversationKey(controller_->activeConversationKey());
    const QString page = controller_->connected() ? QStringLiteral("workspace") : QStringLiteral("mode");
    return QJsonObject{
        {"connection", connection},
        {"identity", QJsonObject{{"displayName", controller_->localUserName()},
                                  {"userCode", controller_->localUserCode()},
                                  {"admin", controller_->admin()}}},
        {"navigation", QJsonObject{{"page", page},
                                    {"activeConversation", buildActiveConversation()}}},
        {"rooms", snapshotFor(controller_->roomModel())},
        {"directMessages", snapshotFor(controller_->directMessageModel())},
        {"activeMessages", snapshotFor(controller_->activeMessageModel())},
        {"members", snapshotFor(controller_->memberModel())},
        {"permissions", QJsonObject{{"activeRoomCanManage", controller_->activeRoomCanManage()}}},
        {"connectionApprovals", QJsonArray::fromVariantList(controller_->pendingConnectionApprovals())},
        {"diagnostics", QJsonObject{{"enabled", NetworkDiagnostics::enabled()},
                                     {"directory", NetworkDiagnostics::logDirectoryPath()}}},
        {"performance", performance},
        {"graphics", graphics},
        {"savedConnection", QJsonObject{{"serverIp", controller_->savedServerIp()},
                                         {"serverPort", controller_->savedServerPort()},
                                         {"username", controller_->savedUsername()},
                                         {"userCode", controller_->savedUserCode()},
                                         {"caFile", controller_->savedCaFile()}}},
        {"lanDiscovery", QJsonObject{{"scanning", lanDiscovery_->scanning()},
                                      {"hosts", lanDiscovery_->hostsJson()}}},
        {"hostDefaults", hostDefaults_}
    };
}

void ChatBridge::emitInvalidCommand(const QString& commandId, const QString& code) {
    const QJsonObject error = bridge::makeError(code, QStringLiteral("命令格式无效"), false,
                                                QStringLiteral("bridge"), commandId);
    emit commandResult(QString::fromUtf8(
        QJsonDocument(bridge::makeCommandResult(commandId, false, error))
            .toJson(QJsonDocument::Compact)));
}

void ChatBridge::completeRecallCommand(const QString& commandId, bool ok, const QString& reason) {
    const auto it = pendingRecallCommandIds_.find(commandId);
    if (it == pendingRecallCommandIds_.end()) return;
	const QString resultCommandId = it.value();
    pendingRecallCommandIds_.erase(it);
    QJsonObject error;
    if (!ok) {
        error = bridge::makeError(QStringLiteral("recall_rejected"),
                                  reason.isEmpty() ? QStringLiteral("Recall rejected") : reason,
                                  false, QStringLiteral("server"), resultCommandId);
    }
    emit commandResult(QString::fromUtf8(
        QJsonDocument(bridge::makeCommandResult(resultCommandId, ok, error)).toJson(QJsonDocument::Compact)));
}

void ChatBridge::dispatch(const QString& commandJson) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(commandJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emitInvalidCommand();
        return;
    }

    const QJsonObject command = document.object();
    const QString commandId = command.value(QStringLiteral("id")).toString();
    QString errorCode;
    if (!bridge::validateCommand(command, &errorCode)) {
        emitInvalidCommand(commandId, errorCode);
        return;
    }

    const QString type = command.value(QStringLiteral("type")).toString();
    const QJsonObject payload = command.value(QStringLiteral("payload")).toObject();
    bool awaitRecallResult = false;
    bool awaitMlsResult = false;
    if (type == QStringLiteral("session.connectRemote")) {
        const QString tlsServerName = payload.value("tlsServerName").toString();
        if (tlsServerName == QStringLiteral("localhost")) {
            controller_->connectToServerWithTlsName(
                payload.value("serverIp").toString(), payload.value("serverPort").toInt(),
                payload.value("username").toString(), payload.value("userCode").toString(),
                payload.value("caFile").toString(), tlsServerName);
        } else {
            controller_->connectToServer(payload.value("serverIp").toString(), payload.value("serverPort").toInt(),
                                         payload.value("username").toString(), payload.value("userCode").toString(),
                                         payload.value("caFile").toString());
        }
    } else if (type == QStringLiteral("session.discoverLanHosts")) {
        QString discoveryError;
        if (!lanDiscovery_->refresh(&discoveryError)) {
            const QJsonObject error = bridge::makeError(QStringLiteral("discovery_unavailable"), discoveryError,
                                                        true, QStringLiteral("bridge"), commandId);
            emit commandResult(QString::fromUtf8(
                QJsonDocument(bridge::makeCommandResult(commandId, false, error)).toJson(QJsonDocument::Compact)));
            scheduleStateUpdate();
            return;
        }
    } else if (type == QStringLiteral("session.connectDiscoveredHost")) {
        LanDiscoveredHost host;
        QString discoveryError;
        if (!lanDiscovery_->connectData(payload.value("hostId").toString(), &host, &discoveryError)) {
            const QJsonObject error = bridge::makeError(QStringLiteral("discovery_host_unavailable"), discoveryError,
                                                        true, QStringLiteral("bridge"), commandId);
            emit commandResult(QString::fromUtf8(
                QJsonDocument(bridge::makeCommandResult(commandId, false, error)).toJson(QJsonDocument::Compact)));
            scheduleStateUpdate();
            return;
        }
        const QString certificatePath = lanDiscovery_->persistCertificate(host, &discoveryError);
        if (certificatePath.isEmpty()) {
            const QJsonObject error = bridge::makeError(QStringLiteral("discovery_certificate_failed"), discoveryError,
                                                        false, QStringLiteral("bridge"), commandId);
            emit commandResult(QString::fromUtf8(
                QJsonDocument(bridge::makeCommandResult(commandId, false, error)).toJson(QJsonDocument::Compact)));
            scheduleStateUpdate();
            return;
        }
        // Discovery pins the exact self-signed certificate. Its stable DNS
        // identity remains "localhost" while the transport IPv4 may change.
        controller_->connectToServerWithTlsName(host.serverIp, host.serverPort,
                                                payload.value("username").toString(),
                                                payload.value("userCode").toString(),
                                                certificatePath, QStringLiteral("localhost"));
    } else if (type == QStringLiteral("session.connectLocalHost")) {
        controller_->connectToLocalHost(payload.value("serverExe").toString(), payload.value("certFile").toString(),
                                        payload.value("keyFile").toString(), payload.value("dbFile").toString(),
                                        payload.value("username").toString(), payload.value("userCode").toString());
    } else if (type == QStringLiteral("mls.keyPackage.fetch")) {
        controller_->fetchMlsKeyPackage(payload.value("room").toString(),
                                        payload.value("targetUserCode").toString(), commandId);
        awaitMlsResult = true;
    } else if (type == QStringLiteral("mls.group.add")) {
        controller_->addMlsMember(payload.value("room").toString(), payload.value("groupId").toString(),
                                  payload.value("targetUserCode").toString(), commandId);
        awaitMlsResult = true;
    } else if (type == QStringLiteral("mls.group.remove")) {
        controller_->removeMlsMember(payload.value("room").toString(), payload.value("groupId").toString(),
                                     payload.value("targetUserCode").toString(), commandId);
        awaitMlsResult = true;
    } else if (type == QStringLiteral("session.disconnect")) {
        controller_->disconnectFromServer();
    } else if (type == QStringLiteral("chat.sendRoom")) {
        controller_->sendRoomMessage(payload.value("content").toString(), payload.value("room").toString());
    } else if (type == QStringLiteral("chat.sendPrivate")) {
        controller_->sendPrivateMessage(payload.value("content").toString(), payload.value("targetUserCode").toString());
    } else if (type == QStringLiteral("history.search")) {
        controller_->searchActiveHistory(payload.value("query").toString());
    } else if (type == QStringLiteral("conversation.selectRoom")) {
        controller_->selectRoom(payload.value("room").toString());
    } else if (type == QStringLiteral("conversation.selectDirect")) {
        controller_->selectDirectMessage(payload.value("userCode").toString());
    } else if (type == QStringLiteral("conversation.openPrivate")) {
        controller_->openPrivateChat(payload.value("displayName").toString(), payload.value("userCode").toString());
    } else if (type == QStringLiteral("directory.refreshUsers")) {
        controller_->requestUsers();
    } else if (type == QStringLiteral("directory.refreshRooms")) {
        controller_->requestRooms();
    } else if (type == QStringLiteral("room.create")) {
        controller_->createRoom(payload.value("room").toString(), payload.value("isPrivate").toBool());
    } else if (type == QStringLiteral("room.action")) {
        controller_->sendRoomAction(payload.value("action").toString(), payload.value("room").toString(),
                                    payload.value("targetUserCode").toString());
    } else if (type == QStringLiteral("admin.action")) {
        controller_->sendAdminAction(payload.value("action").toString(), payload.value("targetUserCode").toString(),
                                     payload.value("messageId").toString());
    } else if (type == QStringLiteral("message.copy")) {
        controller_->copyText(payload.value("text").toString());
    } else if (type == QStringLiteral("message.removeLocal")) {
        controller_->removeLocalMessage(payload.value("messageId").toString());
    } else if (type == QStringLiteral("message.retry")) {
        controller_->retryMessage(payload.value("messageId").toString());
    } else if (type == QStringLiteral("message.recall")) {
        const QString messageId = payload.value("messageId").toString();
        pendingRecallCommandIds_.insert(commandId, commandId);
        if (!controller_->recallMessage(messageId, commandId)) {
            pendingRecallCommandIds_.remove(commandId);
            const QJsonObject error = bridge::makeError(QStringLiteral("permission_denied"),
                                                        QStringLiteral("Recall is only available to the message author or an administrator"),
                                                        false, QStringLiteral("controller"), commandId);
            emit commandResult(QString::fromUtf8(
                QJsonDocument(bridge::makeCommandResult(commandId, false, error)).toJson(QJsonDocument::Compact)));
            return;
        }
        awaitRecallResult = true;
    } else if (type == QStringLiteral("settings.setPerformanceMode")) {
        if (!performanceProfile_) {
            const QJsonObject error = bridge::makeError(
                QStringLiteral("capability_unavailable"),
                QStringLiteral("性能设置当前不可用"), false, QStringLiteral("bridge"), commandId);
            emit commandResult(QString::fromUtf8(
                QJsonDocument(bridge::makeCommandResult(commandId, false, error)).toJson(QJsonDocument::Compact)));
            return;
        }
        performanceProfile_->setMode(payload.value("mode").toString());
    } else if (type == QStringLiteral("settings.setConnectionLogging")) {
        NetworkDiagnostics::setEnabled(payload.value("enabled").toBool());
        scheduleStateUpdate();
    } else if (type == QStringLiteral("performance.reportFrameTimes")) {
        if (performanceProfile_) {
            const QJsonArray frameTimes = payload.value(QStringLiteral("frameTimesMs")).toArray();
            for (const QJsonValue& value : frameTimes) {
                performanceProfile_->observeFrameTime(value.toDouble());
            }
            scheduleStateUpdate();
        }
        return;
    }

    if (awaitRecallResult) {
        scheduleStateUpdate();
        return;
    }
    if (awaitMlsResult) {
        scheduleStateUpdate();
        return;
    }
    emit commandResult(QString::fromUtf8(
        QJsonDocument(bridge::makeCommandResult(commandId, true)).toJson(QJsonDocument::Compact)));
    scheduleStateUpdate();
}
