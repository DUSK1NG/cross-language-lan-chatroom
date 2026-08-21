#include "chat_bridge.hpp"

#include "bridge_protocol.hpp"
#include "gui_chat_controller.hpp"

#include <QAbstractItemModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>

namespace {
constexpr int kMaxSerializedRows = 500;

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
    : QObject(parent), controller_(controller) {
    Q_ASSERT(controller_);
    stateTimer_.setSingleShot(true);
    stateTimer_.setInterval(16);
    connect(&stateTimer_, &QTimer::timeout, this, &ChatBridge::publishState);

    connect(controller_, &GuiChatController::connectedChanged, this, [this]() {
        if (controller_->connected()) {
            lastError_ = {};
        }
        scheduleStateUpdate();
    });
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
    connect(controller_, &GuiChatController::connectionFailed, this, [this](const QString& reason) {
        handleConnectionError(QStringLiteral("connection_failed"), reason, true);
    });
    connect(controller_, &GuiChatController::connectionLost, this, [this](const QString& reason) {
        handleConnectionError(QStringLiteral("connection_lost"), reason, true);
    });

    connectModel(controller_->roomModel());
    connectModel(controller_->directMessageModel());
    connectModel(controller_->messageModel());
    connectModel(controller_->memberModel());
    connectModel(controller_->activeMessageModel());
    latestStateJson_ = bridge::serializeState(buildState());
}

void ChatBridge::connectModel(QAbstractItemModel* model) {
    if (!model) {
        return;
    }
    connect(model, &QAbstractItemModel::dataChanged, this, &ChatBridge::scheduleStateUpdate,
            Qt::UniqueConnection);
    connect(model, &QAbstractItemModel::rowsInserted, this, &ChatBridge::scheduleStateUpdate,
            Qt::UniqueConnection);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &ChatBridge::scheduleStateUpdate,
            Qt::UniqueConnection);
    connect(model, &QAbstractItemModel::modelReset, this, &ChatBridge::scheduleStateUpdate,
            Qt::UniqueConnection);
}

void ChatBridge::scheduleStateUpdate() {
    if (!stateTimer_.isActive()) {
        stateTimer_.start();
    }
}

void ChatBridge::publishState() {
    latestStateJson_ = bridge::serializeState(buildState());
    emit stateChanged(latestStateJson_);
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

QJsonObject ChatBridge::buildState() const {
    QString phase = QStringLiteral("idle");
    if (controller_->connected()) {
        phase = QStringLiteral("connected");
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

    const QString activeRoom = roomFromConversationKey(controller_->activeConversationKey());
    const QString page = controller_->connected() ? QStringLiteral("workspace") : QStringLiteral("mode");
    return QJsonObject{
        {"connection", connection},
        {"identity", QJsonObject{{"displayName", controller_->localUserName()},
                                  {"userCode", controller_->localUserCode()},
                                  {"admin", controller_->admin()}}},
        {"navigation", QJsonObject{{"page", page},
                                    {"activeConversation", buildActiveConversation()}}},
        {"rooms", serializeModel(controller_->roomModel())},
        {"directMessages", serializeModel(controller_->directMessageModel())},
        {"activeMessages", serializeModel(controller_->activeMessageModel())},
        {"members", serializeModel(controller_->memberModel())},
        {"permissions", QJsonObject{{"activeRoomCanManage", controller_->activeRoomCanManage()}}},
        {"savedConnection", QJsonObject{{"serverIp", controller_->savedServerIp()},
                                         {"serverPort", controller_->savedServerPort()},
                                         {"username", controller_->savedUsername()},
                                         {"userCode", controller_->savedUserCode()},
                                         {"caFile", controller_->savedCaFile()}}}
    };
}

void ChatBridge::emitInvalidCommand(const QString& commandId, const QString& code) {
    const QJsonObject error = bridge::makeError(code, QStringLiteral("命令格式无效"), false,
                                                QStringLiteral("bridge"), commandId);
    emit commandResult(QString::fromUtf8(
        QJsonDocument(bridge::makeCommandResult(commandId, false, error))
            .toJson(QJsonDocument::Compact)));
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
    if (type == QStringLiteral("session.connectRemote")) {
        controller_->connectToServer(payload.value("serverIp").toString(), payload.value("serverPort").toInt(),
                                     payload.value("username").toString(), payload.value("userCode").toString(),
                                     payload.value("password").toString(), payload.value("caFile").toString(),
                                     payload.value("registerAccount").toBool());
    } else if (type == QStringLiteral("session.connectLocalHost")) {
        controller_->connectToLocalHost(payload.value("serverExe").toString(), payload.value("certFile").toString(),
                                        payload.value("keyFile").toString(), payload.value("dbFile").toString(),
                                        payload.value("username").toString(), payload.value("userCode").toString());
    } else if (type == QStringLiteral("session.disconnect")) {
        controller_->disconnectFromServer();
    } else if (type == QStringLiteral("chat.sendRoom")) {
        controller_->sendRoomMessage(payload.value("content").toString(), payload.value("room").toString());
    } else if (type == QStringLiteral("chat.sendPrivate")) {
        controller_->sendPrivateMessage(payload.value("content").toString(), payload.value("targetUserCode").toString());
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
    } else if (type == QStringLiteral("message.recall")) {
        controller_->recallMessage(payload.value("messageId").toString());
    }

    emit commandResult(QString::fromUtf8(
        QJsonDocument(bridge::makeCommandResult(commandId, true)).toJson(QJsonDocument::Compact)));
    scheduleStateUpdate();
}
