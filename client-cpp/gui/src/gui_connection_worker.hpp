#pragma once

#include "connection.hpp"
#include "reconnect_policy.hpp"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <map>
#include <memory>
#include <thread>

#ifdef LAN_CHAT_ENABLE_MLSPP
#include "crypto/mls_client.hpp"
#include <QByteArray>
#include <QSet>
#endif

class GuiConnectionWorker final : public QObject {
    Q_OBJECT

public:
    explicit GuiConnectionWorker(QObject* parent = nullptr);
    ~GuiConnectionWorker() override;

public slots:
    void connectToServer(const QString& serverIp,
                         int serverPort,
                         const QString& username,
                         const QString& userCode,
                         const QString& caFile,
                         const QString& tlsServerName = {});
    void connectToLocalHost(const QString& serverExe,
                            const QString& certFile,
                            const QString& keyFile,
                            const QString& dbFile,
                            const QString& username,
                            const QString& userCode);
    void disconnectFromServer();
    void scheduleReconnect();
    void sendChat(const QString& content, const QString& messageId = {});
    void sendChatToRoom(const QString& content, const QString& room, const QString& messageId = {});
    void sendPrivate(const QString& content, const QString& targetUserCode, const QString& messageId = {});
    void joinRoom(const QString& room);
    void createRoom(const QString& room, bool isPrivate);
    void sendRoomAction(const QString& action, const QString& room, const QString& targetUserCode = {});
    void requestUsers();
    void requestRooms();
    void requestHistory(const QString& room, const QString& targetUserCode,
                        bool isPrivate, const QString& beforeMessageId = {}, int limit = 50,
                        const QString& searchQuery = {});
    void sendAdminAction(const QString& action, const QString& targetUserCode, const QString& messageId = {}, const QString& commandId = {});
    void fetchMlsKeyPackage(const QString& room, const QString& targetUserCode,
                            const QString& commandId = {});
    void addMlsMember(const QString& room, const QString& groupId,
                      const QString& targetUserCode, const QString& commandId = {});
    void removeMlsMember(const QString& room, const QString& groupId,
                         const QString& targetUserCode, const QString& commandId = {});
    void inspectMlsGroup(const QString& groupId, const QString& commandId = {});
    void protectMls(const QString& groupId, const QByteArray& plaintext, const QString& commandId = {});
    void unprotectMls(const QString& groupId, const QByteArray& ciphertext, const QString& commandId = {});

signals:
    void disconnected();
    void connected(bool isAdmin);
    void connectionFailed(const QString& reason);
    void connectionLost(const QString& reason);
    void reconnectScheduled(int attempt, int delayMs);
    void reconnectAttempt(int attempt);
    void reconnectFailed(const QString& reason);
    void messageDeliveryFailed(const QString& messageId);
    void messageReceived(const QString& type,
                         const QString& messageId,
						 const QString& commandId,
                         const QString& username,
                         const QString& userCode,
                         const QString& content,
                         const QString& room,
                         const QString& targetUserCode,
                         const QString& deliveryState,
                         const QStringList& users,
                         const QStringList& rooms,
                         const QVariantList& userDetails,
                         const QVariantList& roomDetails,
                         bool isAdmin);
    void historyReceived(const QString& room, const QString& targetUserCode,
                         bool isPrivate, const QVariantList& messages, bool hasMore,
                         const QString& searchQuery);
    void mlsCommandResult(const QString& commandId, bool ok,
                          const QString& code, const QString& message);
    void mlsGroupState(const QString& commandId, bool ok, const QString& groupId,
                       quint64 epoch, const QString& message);
    void mlsDataResult(const QString& commandId, bool ok, const QByteArray& data,
                       const QString& message);

private:
    bool connectToServerWithRetries(const QString& serverIp,
                                    int serverPort,
                                    const QString& username,
                                    const QString& userCode,
                                    const QString& caFile,
                                    const QString& tlsServerName,
                                    int attempts,
                                    bool reportFailure = true);
    void retrySavedConnection();
    bool isLocalServerListening(int timeoutMs) const;
    void stopHostedServer();
    void receiveLoop();
    void stopReceiveLoop();
    void resetMlsState();
    void publishMlsKeyPackage();
    void processMlsMessage(const message::Message& incoming);

    std::unique_ptr<connection::ConnectionState> connection_;
    std::unique_ptr<QProcess> hostProcess_;
    std::thread receiveThread_;
    std::atomic<bool> running_{false};
    struct SavedConnection {
        QString serverIp;
        int serverPort = 0;
        QString username;
        QString userCode;
        QString caFile;
        QString tlsServerName;
        bool valid = false;
    } savedConnection_;
    ReconnectPolicy reconnectPolicy_;
    bool reconnectTimerActive_ = false;
    QString lastConnectionFailure_;
    bool explicitDisconnect_ = false;

#ifdef LAN_CHAT_ENABLE_MLSPP
    struct MlsGroupState {
        std::shared_ptr<MlsClient> client;
        QString room;
        quint64 epoch = 0;
        std::map<quint64, QByteArray> commits;
        std::map<quint64, QByteArray> welcomes;
    };
    struct PendingMlsOperation {
        enum class Phase { Proposal, Commit, Welcome } phase = Phase::Proposal;
        QString room;
        QString group;
        QString target;
        quint64 epoch = 0;
        QByteArray commit;
        QByteArray welcome;
        bool add = false;
    };
    std::shared_ptr<MlsClient> mlsClient_;
    std::map<QString, QByteArray> mlsKeyPackages_;
    std::map<QString, MlsGroupState> mlsGroups_;
    std::map<QString, PendingMlsOperation> pendingMlsOperations_;
    QSet<QString> pendingMlsCommands_;
#endif
};
