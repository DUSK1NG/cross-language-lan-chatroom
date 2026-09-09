#pragma once

#include "connection.hpp"
#include "reconnect_policy.hpp"
#include "attachments/attachment_transfer_state.hpp"
#include "attachments/transfer_client.hpp"
#include "attachments/attachment_manifest.hpp"
#include "attachments/attachment_download_assembler.hpp"

#include <QObject>
#include <QByteArray>
#include <QFile>
#include <QList>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <atomic>
#include <map>
#include <memory>
#include <thread>

#ifdef LAN_CHAT_ENABLE_MLSPP
#include "crypto/mls_client.hpp"
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
    void sendAttachmentInit(const QString& room, qint64 logicalSize, const QString& commandId = {});
    void startAttachmentUpload(const QString& room, const QString& filePath,
                               const QStringList& targetUsers, const QString& commandId = {});
    void sendAttachmentChunk(const QString& uploadId, qint64 chunkIndex,
                             const QByteArray& ciphertext, const QByteArray& cipherSha256,
                             const QString& commandId = {});
    void resumeAttachment(const QString& uploadId, const QString& commandId = {});
    void sendAttachmentCommit(const QString& uploadId, const QString& commandId = {});
    void sendAttachmentDownload(const QString& attachmentId, qint64 chunkIndex,
                                const QString& commandId = {});
    void startAttachmentDownload(const QString& attachmentId, const QString& outputPath,
                                 const QString& commandId = {});
    void fetchMlsKeyPackage(const QString& room, const QString& targetUserCode,
                            const QString& commandId = {});
    void addMlsMember(const QString& room, const QString& groupId,
                      const QString& targetUserCode, const QString& commandId = {});
    void removeMlsMember(const QString& room, const QString& groupId,
                         const QString& targetUserCode, const QString& commandId = {});
    void inspectMlsGroup(const QString& groupId, const QString& commandId = {});
    void protectMls(const QString& groupId, const QByteArray& plaintext, const QString& commandId = {});
    void unprotectMls(const QString& groupId, const QByteArray& ciphertext, const QString& commandId = {});
#if defined(LAN_CHAT_ENABLE_MLSPP) && defined(LAN_CHAT_ENABLE_TEST_HOOKS)
    void enableDropNextMlsCommitForTesting();
#endif

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
    void attachmentEvent(const QString& type, const QString& attachmentId,
                         const QString& uploadId, const QString& commandId,
                         qint64 chunkSize, qint64 chunkIndex,
                         const QByteArray& ciphertext, const QByteArray& cipherSha256,
                         const QString& expiresAt, const QList<qint64>& receivedIndexes,
                         const QString& content, qint64 logicalSize = 0);
    void mlsCommandResult(const QString& commandId, bool ok,
                          const QString& code, const QString& message);
    // Metadata-only observations; never carry key package, welcome, commit,
    // or application plaintext bytes.
    void mlsKeyPackageAvailable(const QString& targetUserCode, const QString& digest);
    void mlsWelcomeEvent(const QString& groupId, quint64 epoch, const QString& phase);
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
                                    bool reportFailure = true,
                                    bool preserveMlsState = false);
    void retrySavedConnection();
    bool isLocalServerListening(int timeoutMs) const;
    void stopHostedServer();
    void receiveLoop();
    void stopReceiveLoop();
    void resetMlsState();
    void publishMlsKeyPackage();
    void resumePendingMlsOperations();
    void processMlsMessage(const message::Message& incoming);
    void processAttachmentMessage(const message::Message& incoming);
    void sendNextAttachmentChunk(const QString& uploadId);
    QString attachmentEventCommandId(const message::Message& incoming) const;
#ifdef LAN_CHAT_ENABLE_MLSPP
    void sendAttachmentManifest(const attachments::AttachmentManifest& manifest);
    void startNextAttachmentMlsSetup(const QString& attachmentCommandId);
    void failAttachmentMlsSetup(const QString& attachmentCommandId, const QString& reason);
#endif

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
    std::map<QString, attachments::AttachmentTransferState> attachmentTransfers_;
    std::map<QString, attachments::AttachmentTransferState> pendingAttachmentInits_;
    struct PendingAttachmentFile {
        QString room;
        QString groupId;
        QString filePath;
        attachments::AttachmentCrypto::Key key{};
        qint64 logicalSize = 0;
    };
    struct AttachmentUploadJob {
        QString room;
        QString fileName;
        QString baseCommand;
        attachments::AttachmentCrypto::Key key{};
        attachments::ChunkContext context;
        std::shared_ptr<QFile> input;
        bool lastChunkSent = false;
        int inFlightChunks = 0;
        static constexpr int WindowSize = 8;
    };
    std::map<QString, PendingAttachmentFile> pendingAttachmentFiles_;
    std::map<QString, std::pair<QString, qint64>> pendingAttachmentChunks_;
    std::map<QString, AttachmentUploadJob> attachmentUploadJobs_;
#ifdef LAN_CHAT_ENABLE_MLSPP
    struct PendingAttachmentMlsSetup {
        QString room;
        QString filePath;
        QString groupId;
        QString commandId;
        QStringList targetUsers;
        int nextTarget = 0;
    };
    std::map<QString, PendingAttachmentMlsSetup> pendingAttachmentMlsSetups_;
    std::map<QString, QString> pendingAttachmentMlsFetches_;
    std::map<QString, QString> pendingAttachmentMlsAdds_;
    std::map<QString, attachments::AttachmentManifest> pendingAttachmentManifests_;
    std::map<QString, attachments::AttachmentManifest> receivedAttachmentManifests_;
    struct AttachmentDownloadJob {
        attachments::AttachmentManifest manifest;
        attachments::AttachmentCrypto::Key manifestKey{};
        attachments::AttachmentDownloadAssembler assembler;
        QString commandId;
    };
    std::map<QString, AttachmentDownloadJob> attachmentDownloadJobs_;
#endif

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
        QByteArray proposal;
        QByteArray commit;
        QByteArray welcome;
        bool add = false;
    };
    std::shared_ptr<MlsClient> mlsClient_;
    std::map<QString, QByteArray> mlsKeyPackages_;
    std::map<QString, MlsGroupState> mlsGroups_;
    std::map<QString, PendingMlsOperation> pendingMlsOperations_;
    QSet<QString> pendingMlsCommands_;
#if defined(LAN_CHAT_ENABLE_TEST_HOOKS)
    bool dropNextMlsCommitForTesting_ = false;
#endif
#endif
};
