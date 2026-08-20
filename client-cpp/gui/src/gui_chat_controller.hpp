#pragma once

#include "chat_model.hpp"

#include <QHash>
#include <QObject>
#include <QThread>
#include <QTimer>

class GuiConnectionWorker;

class GuiChatController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(ChatListModel* roomModel READ roomModel CONSTANT)
    Q_PROPERTY(ChatListModel* directMessageModel READ directMessageModel CONSTANT)
    Q_PROPERTY(ChatListModel* messageModel READ messageModel CONSTANT)
    Q_PROPERTY(ChatListModel* activeMessageModel READ activeMessageModel NOTIFY activeMessageModelChanged)
    Q_PROPERTY(ChatListModel* memberModel READ memberModel CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool admin READ admin NOTIFY adminChanged)
    Q_PROPERTY(QString localUserName READ localUserName NOTIFY localIdentityChanged)
    Q_PROPERTY(QString localUserCode READ localUserCode NOTIFY localIdentityChanged)
    Q_PROPERTY(int onlineMemberCount READ onlineMemberCount NOTIFY onlineMemberCountChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit GuiChatController(QObject* parent = nullptr);
    ~GuiChatController() override;

    ChatListModel* roomModel() const { return roomModel_; }
    ChatListModel* directMessageModel() const { return directMessageModel_; }
    ChatListModel* messageModel() const { return messageModel_; }
    ChatListModel* activeMessageModel() const { return messageModel_; }
    ChatListModel* memberModel() const { return memberModel_; }
    bool connected() const { return connected_; }
    bool admin() const { return admin_; }
    QString localUserName() const { return localUserName_; }
    QString localUserCode() const { return localUserCode_; }
    int onlineMemberCount() const { return onlineMemberCount_; }
    QString statusText() const { return statusText_; }

    Q_INVOKABLE void connectToServer(const QString& serverIp, int serverPort,
                                     const QString& username, const QString& userCode,
                                     const QString& password, const QString& caFile = {},
                                     bool registerAccount = false);
    Q_INVOKABLE void connectToLocalHost(const QString& serverExe,
                                        const QString& certFile,
                                        const QString& keyFile,
                                        const QString& dbFile,
                                        const QString& username,
                                        const QString& userCode);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void sendChatMessage(const QString& content);
    Q_INVOKABLE void sendRoomMessage(const QString& content, const QString& room);
    Q_INVOKABLE void sendPrivateMessage(const QString& content, const QString& targetUserCode);
    Q_INVOKABLE void requestUsers();
    Q_INVOKABLE void requestRooms();
    Q_INVOKABLE void selectRoom(const QString& room);
    Q_INVOKABLE void selectDirectMessage(const QString& userCode);
    Q_INVOKABLE void openPrivateChat(const QString& displayName, const QString& userCode);
    Q_INVOKABLE void sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId = {});
    Q_INVOKABLE void copyText(const QString& text);
    Q_INVOKABLE void removeLocalMessage(const QString& messageId);
    Q_INVOKABLE void recallMessage(const QString& messageId);

signals:
    void connectedChanged();
    void adminChanged();
    void localIdentityChanged();
    void onlineMemberCountChanged();
    void statusTextChanged();
    void activeMessageModelChanged();

private slots:
    void handleConnected(bool isAdmin);
    void handleConnectionFailed(const QString& reason);
    void handleConnectionLost(const QString& reason);
    void handleMessage(const QString& type, const QString& messageId, const QString& username,
                       const QString& userCode, const QString& content,
                       const QString& room, const QString& targetUserCode,
                       const QStringList& users, const QStringList& rooms, bool isAdmin);

private:
    void setStatus(const QString& status);
    void appendSystemMessage(const QString& content);
    void appendSystemMessageToModel(ChatListModel* model, const QString& content);
    void incrementUnreadForConversation(const QString& key, const QString& username,
                                        const QString& userCode);
    ChatListModel* ensureConversationModel(const QString& key);
    void resetSessionData();

    ChatListModel* roomModel_;
    ChatListModel* directMessageModel_;
    ChatListModel* messageModel_;
    ChatListModel* memberModel_;
    QHash<QString, ChatListModel*> conversationModels_;
    QHash<QString, int> roomMemberCounts_;
    QString activeConversationKey_ = QStringLiteral("room:lobby");
    QThread workerThread_;
    QTimer refreshTimer_;
    GuiConnectionWorker* worker_ = nullptr;
    bool connected_ = false;
    bool admin_ = false;
    QString localUserName_ = QStringLiteral("Alice");
    QString statusText_ = QStringLiteral("未连接");
    QString localUserCode_ = QStringLiteral("A001");
    QString joinedRoom_ = QStringLiteral("lobby");
    int onlineMemberCount_ = 0;
    int localMessageCounter_ = 0;
};
