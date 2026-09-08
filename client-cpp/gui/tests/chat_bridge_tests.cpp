#include "chat_bridge.hpp"
#include "gui_chat_controller.hpp"
#include "gui_connection_worker.hpp"
#include "graphics_info.hpp"
#include "host_path_resolver.hpp"
#include "performance_profile.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>

namespace {
class WinsockScope final {
public:
    WinsockScope() : result_(WSAStartup(MAKEWORD(2, 2), &data_)) {}
    ~WinsockScope() {
        if (result_ == 0) WSACleanup();
    }

    int result() const { return result_; }

private:
    WSADATA data_{};
    int result_ = SOCKET_ERROR;
};

QString opensslError(const char* operation) {
    std::array<char, 256> buffer{};
    const unsigned long errorCode = ERR_get_error();
    if (errorCode == 0) return QString::fromLatin1(operation);
    ERR_error_string_n(errorCode, buffer.data(), buffer.size());
    return QString::fromLatin1(operation) + QStringLiteral(": ") + QString::fromLatin1(buffer.data());
}

class RawTlsClient final {
public:
    RawTlsClient() {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS | OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    }

    ~RawTlsClient() { close(); }

    RawTlsClient(const RawTlsClient&) = delete;
    RawTlsClient& operator=(const RawTlsClient&) = delete;

    bool connectToHost(const QString& address, const quint16 port,
                       const QString& trustedCertificate, const QString& serverName) {
        close();
        error_.clear();

        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_port = htons(port);
        if (InetPtonW(AF_INET, reinterpret_cast<const wchar_t*>(address.utf16()), &endpoint.sin_addr) != 1) {
            error_ = QStringLiteral("invalid raw TLS test address");
            return false;
        }

        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET) {
            error_ = QStringLiteral("raw TLS socket creation failed");
            return false;
        }
        const int timeoutMs = 10000;
        setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        if (connect(socket_, reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR) {
            error_ = QStringLiteral("raw TLS TCP connection failed");
            close();
            return false;
        }

        context_ = SSL_CTX_new(TLS_client_method());
        if (context_ == nullptr) {
            error_ = opensslError("raw TLS context creation failed");
            close();
            return false;
        }
        SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION);
        SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
        const QByteArray certificatePath = trustedCertificate.toUtf8();
        if (certificatePath.isEmpty() ||
            SSL_CTX_load_verify_locations(context_, certificatePath.constData(), nullptr) != 1) {
            error_ = opensslError("raw TLS trust setup failed");
            close();
            return false;
        }

        ssl_ = SSL_new(context_);
        const QByteArray tlsName = serverName.toUtf8();
        if (ssl_ == nullptr || SSL_set_fd(ssl_, static_cast<int>(socket_)) != 1 ||
            tlsName.isEmpty() || SSL_set1_host(ssl_, tlsName.constData()) != 1 ||
            SSL_set_tlsext_host_name(ssl_, tlsName.constData()) != 1) {
            error_ = opensslError("raw TLS setup failed");
            close();
            return false;
        }
        if (SSL_connect(ssl_) != 1) {
            error_ = opensslError("raw TLS handshake failed");
            close();
            return false;
        }
        if (SSL_get_verify_result(ssl_) != X509_V_OK) {
            error_ = QStringLiteral("raw TLS certificate verification failed");
            close();
            return false;
        }
        return true;
    }

    bool writeAll(const QByteArray& bytes, const int timeoutMs) {
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            size_t written = 0;
            if (SSL_write_ex(ssl_, bytes.constData() + offset,
                             static_cast<size_t>(bytes.size() - offset), &written) == 1) {
                offset += static_cast<qsizetype>(written);
                continue;
            }
            if (!waitForSslRetry(SSL_get_error(ssl_, 0), timeoutMs, QStringLiteral("write"))) return false;
        }
        return true;
    }

    bool readExact(const qsizetype size, QByteArray* result, const int timeoutMs) {
        if (result == nullptr || size < 0) return false;
        result->clear();
        result->resize(size);
        qsizetype offset = 0;
        while (offset < size) {
            size_t read = 0;
            if (SSL_read_ex(ssl_, result->data() + offset,
                            static_cast<size_t>(size - offset), &read) == 1) {
                offset += static_cast<qsizetype>(read);
                continue;
            }
            if (!waitForSslRetry(SSL_get_error(ssl_, 0), timeoutMs, QStringLiteral("read"))) return false;
        }
        return true;
    }

    void disconnect() { close(); }

    QString errorString() const { return error_; }

private:
    bool waitForSslRetry(const int sslError, const int timeoutMs, const QString& operation) {
        if (sslError != SSL_ERROR_WANT_READ && sslError != SSL_ERROR_WANT_WRITE) {
            error_ = opensslError(qPrintable(QStringLiteral("raw TLS %1 failed").arg(operation)));
            return false;
        }
        fd_set sockets;
        FD_ZERO(&sockets);
        FD_SET(socket_, &sockets);
        timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
        const int result = select(0,
                                  sslError == SSL_ERROR_WANT_READ ? &sockets : nullptr,
                                  sslError == SSL_ERROR_WANT_WRITE ? &sockets : nullptr,
                                  nullptr, &timeout);
        if (result > 0) return true;
        error_ = result == 0
            ? QStringLiteral("raw TLS %1 timed out").arg(operation)
            : QStringLiteral("raw TLS %1 socket wait failed").arg(operation);
        return false;
    }

    void close() {
        if (ssl_ != nullptr) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        if (context_ != nullptr) {
            SSL_CTX_free(context_);
            context_ = nullptr;
        }
        if (socket_ != INVALID_SOCKET) {
            shutdown(socket_, SD_BOTH);
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

    SOCKET socket_ = INVALID_SOCKET;
    SSL_CTX* context_ = nullptr;
    SSL* ssl_ = nullptr;
    QString error_;
};

bool sendRawTlsFrame(RawTlsClient& socket, const QJsonObject& object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > 64 * 1024) return false;
    QByteArray frame(4, Qt::Uninitialized);
    const auto size = static_cast<quint32>(payload.size());
    frame[0] = static_cast<char>((size >> 24) & 0xff);
    frame[1] = static_cast<char>((size >> 16) & 0xff);
    frame[2] = static_cast<char>((size >> 8) & 0xff);
    frame[3] = static_cast<char>(size & 0xff);
    frame.append(payload);
    return socket.writeAll(frame, 2000);
}

bool readRawTlsFrame(RawTlsClient& socket, QJsonObject* object, int timeoutMs) {
    QByteArray header;
    if (!socket.readExact(4, &header, timeoutMs)) return false;
    const auto size = (static_cast<quint32>(static_cast<unsigned char>(header[0])) << 24) |
                      (static_cast<quint32>(static_cast<unsigned char>(header[1])) << 16) |
                      (static_cast<quint32>(static_cast<unsigned char>(header[2])) << 8) |
                      static_cast<quint32>(static_cast<unsigned char>(header[3]));
    if (size == 0 || size > 64 * 1024) return false;
    QByteArray payload;
    if (!socket.readExact(static_cast<qsizetype>(size), &payload, timeoutMs)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) return false;
    *object = document.object();
    return true;
}

bool receiveRawTlsType(RawTlsClient& socket, const QString& type, QJsonObject* object, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QJsonObject message;
        if (!readRawTlsFrame(socket, &message, qMax(1, timeoutMs - static_cast<int>(timer.elapsed())))) return false;
        if (message.value(QStringLiteral("type")).toString() == type) {
            if (object) *object = message;
            return true;
        }
    }
    return false;
}

bool waitForLocalHostPortClosed(const int timeoutMs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QTcpSocket probe;
        probe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
        const bool connected = probe.waitForConnected(100);
        probe.abort();
        if (!connected) return true;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QTest::qWait(50);
    }
    return false;
}
}  // namespace

class ChatBridgeTests final : public QObject {
    Q_OBJECT

private slots:
    void attachmentPipelineTransfersPastChatBurstLimit();
    void oversizedAttachmentFailsBeforeConnectionSetup();
    void controllerConstructs();
    void initialSnapshotHasSchemaAndDisconnectedState();
    void snapshotMarksMemberPackageHostAsUnavailable();
    void snapshotContainsPerformanceAndGraphicsInfo();
    void performanceModeCommandUpdatesSnapshot();
    void frameTelemetryUpdatesPerformanceProfileWithoutCommandResult();
    void snapshotContainsRoomAndMemberRoles();
    void dispatchSelectRoomCallsController();
    void inactiveConversationModelsStayBounded();
    void evictedConversationModelsReleaseBridgeSnapshots();
    void invalidJsonEmitsCommandResultWithoutCallingController();
    void passwordDoesNotAppearInSnapshotOrResult();
    void modelChangesAreCoalescedIntoOneStateChangedSignal();
    void burstModelChangesDoNotPushStateAtFrameRate();
    void stateSamplingReportsDirtyModelCount();
    void workerDisconnectRunsOnWorkerThread();
    void attachmentUploadEventsCarrySizeAndAcknowledgedIndexes();
    void serverConnectionCompletesWithoutMessageLifetimeCorruption();
    void localHostConnectionCompletesWithoutMessageLifetimeCorruption();
    void approvedLanMemberConnectionCompletesAfterLoginPending();
    void authenticatedRawTlsMLSFramesAreRejected();
    void mlsControlRoundTripUsesProposalCommitWelcomeOrder();
    void usersResponseUsesBulkModelUpdates();
    void roomsResponseUsesBulkModelUpdates();
    void connectionApprovalStateIsExposedAndCleared();
    void successfulConnectionClearsPreviousError();
    void reconnectingSnapshotPreservesTimelineAndReportsRecovery();
    void serverRejectionMarksOriginalMessageFailedWithoutTimelineError();
    void searchHistoryMergesWithoutDiscardingRealtimeRows();
    void recallRejectsAnUnrelatedMessageBeforeReportingSuccess();
    void recallReportsServerAcceptanceOrRejectionInsteadOfDispatchSuccess();
};

void ChatBridgeTests::controllerConstructs() {
    GuiChatController controller;
    QVERIFY(!controller.connected());
}

void ChatBridgeTests::initialSnapshotHasSchemaAndDisconnectedState() {
    GuiChatController controller;
    ChatBridge bridge(&controller);

    const QJsonObject state = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object();
    QCOMPARE(state.value("schemaVersion").toInt(), 1);
    QCOMPARE(state.value("connection").toObject().value("phase").toString(),
             QStringLiteral("idle"));
    QCOMPARE(state.value("identity").toObject().value("displayName").toString(),
             QStringLiteral("Alice"));
    QCOMPARE(state.value("identity").toObject().value("userCode").toString(),
             QStringLiteral("A001"));
    QCOMPARE(state.value("navigation").toObject().value("activeConversation")
                 .toObject().value("kind").toString(), QStringLiteral("room"));
}

void ChatBridgeTests::snapshotMarksMemberPackageHostAsUnavailable() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    bridge.setHostDefaults("C:/member/server-go/chat-server.exe",
                           "C:/member/server-go/certs/server-lan.crt",
                           "C:/member/server-go/certs/server-lan.key",
                           "C:/member/server-go/chat.db", false,
                           QStringLiteral("成员端不包含本地服务端"));

    const QJsonObject hostDefaults = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object().value("hostDefaults").toObject();
    QCOMPARE(hostDefaults.value("available").toBool(), false);
    QCOMPARE(hostDefaults.value("unavailableReason").toString(),
             QStringLiteral("成员端不包含本地服务端"));
}

void ChatBridgeTests::snapshotContainsPerformanceAndGraphicsInfo() {
    GuiChatController controller;
    GraphicsInfo graphicsInfo;
    PerformanceProfile performanceProfile;
    ChatBridge bridge(&controller, &performanceProfile, &graphicsInfo);

    const QJsonObject state = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object();
    const QJsonObject performance = state.value("performance").toObject();
    const QJsonObject graphics = state.value("graphics").toObject();
    QCOMPARE(performance.value("mode").toString(), performanceProfile.mode());
    QCOMPARE(performance.value("effectiveMode").toString(), performanceProfile.effectiveMode());
    QCOMPARE(performance.value("automaticReason").toString(), performanceProfile.automaticReason());
    QCOMPARE(graphics.value("graphicsApi").toString(), graphicsInfo.graphicsApi());
    QCOMPARE(graphics.value("renderer").toString(), graphicsInfo.renderer());
    QCOMPARE(graphics.value("vendor").toString(), graphicsInfo.vendor());
}

void ChatBridgeTests::performanceModeCommandUpdatesSnapshot() {
    GuiChatController controller;
    GraphicsInfo graphicsInfo;
    PerformanceProfile performanceProfile;
    ChatBridge bridge(&controller, &performanceProfile, &graphicsInfo);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral(
        R"({"id":"cmd-performance","type":"settings.setPerformanceMode","payload":{"mode":"Power Saving"}})"));

    QCOMPARE(performanceProfile.mode(), QStringLiteral("Power Saving"));
    QCOMPARE(resultSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(QJsonDocument::fromJson(bridge.currentStateJson().toUtf8()).object()
                                  .value("performance").toObject().value("mode").toString(),
                              QStringLiteral("Power Saving"), 500);
}

void ChatBridgeTests::frameTelemetryUpdatesPerformanceProfileWithoutCommandResult() {
    GuiChatController controller;
    PerformanceProfile performanceProfile;
    ChatBridge bridge(&controller, &performanceProfile, nullptr);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral(
        R"({"id":"cmd-frame-times","type":"performance.reportFrameTimes","payload":{"frameTimesMs":[16.0,17.0,32.0]}})"));

    QCOMPARE(performanceProfile.observedFrameCount(), 3);
    QCOMPARE(performanceProfile.observedP95FrameMs(), 32.0);
    QCOMPARE(resultSpy.count(), 0);
}

void ChatBridgeTests::snapshotContainsRoomAndMemberRoles() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    controller.roomModel()->append({{"roomName", "study"}, {"memberCount", 2},
                                    {"unreadCount", 1}, {"private", false},
                                    {"canManage", true}});
    controller.memberModel()->append({{"displayName", "Bob"}, {"userCode", "B001"},
                                      {"online", true}, {"admin", false}});

    QTRY_VERIFY(bridge.currentStateJson().contains(QStringLiteral("study")));
    const QJsonObject state = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object();
    const QJsonObject room = state.value("rooms").toArray().at(0).toObject();
    const QJsonObject member = state.value("members").toArray().at(0).toObject();
    QCOMPARE(room.value("roomName").toString(), QStringLiteral("study"));
    QCOMPARE(room.value("memberCount").toInt(), 2);
    QCOMPARE(room.value("canManage").toBool(), true);
    QCOMPARE(member.value("displayName").toString(), QStringLiteral("Bob"));
    QCOMPARE(member.value("userCode").toString(), QStringLiteral("B001"));
}

void ChatBridgeTests::dispatchSelectRoomCallsController() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy activeModelSpy(&controller, &GuiChatController::activeMessageModelChanged);

    bridge.dispatch(QStringLiteral(
        R"({"id":"cmd-1","type":"conversation.selectRoom","payload":{"room":"study"}})"));

    QVERIFY(activeModelSpy.count() > 0);
    QVERIFY(controller.activeMessageModel() != nullptr);
}

void ChatBridgeTests::inactiveConversationModelsStayBounded() {
    GuiChatController controller;

    for (int index = 0; index < 12; ++index) {
        controller.selectRoom(QStringLiteral("room-%1").arg(index));
    }

    QCOMPARE(controller.activeConversationKey(), QStringLiteral("room:room-11"));
    QVERIFY(controller.activeMessageModel() != nullptr);
    QCOMPARE(controller.cachedConversationModelCount(), 8);
}

void ChatBridgeTests::evictedConversationModelsReleaseBridgeSnapshots() {
    GuiChatController controller;
    ChatBridge bridge(&controller);

    for (int index = 0; index < 12; ++index) {
        controller.selectRoom(QStringLiteral("archive-%1").arg(index));
        QTest::qWait(110);
    }

    QCOMPARE(controller.cachedConversationModelCount(), 8);
    QVERIFY(bridge.cachedSerializedModelCount() <= 11);
}

void ChatBridgeTests::invalidJsonEmitsCommandResultWithoutCallingController() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral("not-json"));

    QCOMPARE(resultSpy.count(), 1);
    const QJsonObject result = QJsonDocument::fromJson(
        resultSpy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(result.value("id").toString(), QString());
    QCOMPARE(result.value("ok").toBool(), false);
    QCOMPARE(result.value("error").toObject().value("code").toString(),
             QStringLiteral("invalid_command"));
}

void ChatBridgeTests::passwordDoesNotAppearInSnapshotOrResult() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral(
        R"({"id":"cmd-2","type":"session.connectRemote","payload":{"serverIp":"127.0.0.1","serverPort":8888,"username":"Alice","userCode":"A001","caFile":""}})"));

    QVERIFY(!bridge.currentStateJson().contains(QStringLiteral("secret")));
    QVERIFY(resultSpy.count() <= 1);
    if (resultSpy.count() == 1) {
        QVERIFY(!resultSpy.at(0).at(0).toString().contains(QStringLiteral("secret")));
    }
}

void ChatBridgeTests::modelChangesAreCoalescedIntoOneStateChangedSignal() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy stateSpy(&bridge, &ChatBridge::stateChanged);

    controller.roomModel()->append({{"roomName", "one"}});
    controller.roomModel()->append({{"roomName", "two"}});

    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 500);
}

void ChatBridgeTests::burstModelChangesDoNotPushStateAtFrameRate() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy stateSpy(&bridge, &ChatBridge::stateChanged);

    controller.roomModel()->append({{"roomName", "one"}});
    controller.roomModel()->append({{"roomName", "two"}});

    QTest::qWait(60);
    QCOMPARE(stateSpy.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 500);
}

void ChatBridgeTests::stateSamplingReportsDirtyModelCount() {
    GuiChatController controller;
    for (int row = 0; row < 500; ++row) {
        controller.roomModel()->append({{"roomName", QStringLiteral("room-%1").arg(row)},
                                        {"memberCount", row % 7},
                                        {"unreadCount", row % 3}});
    }

    ChatBridge bridge(&controller);
    QSignalSpy stateSpy(&bridge, &ChatBridge::stateChanged);
    QVERIFY(bridge.lastStateBuildDurationUs() >= 0);
    QCOMPARE(bridge.lastSerializedModelCount(), 4);

    const int initialBuilds = bridge.stateBuildCount();
    controller.roomModel()->append({{"roomName", QStringLiteral("room-dirty")}});
    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() > 0, 500);
    QTRY_VERIFY_WITH_TIMEOUT(bridge.stateBuildCount() > initialBuilds, 500);
    QCOMPARE(bridge.lastSerializedModelCount(), 1);

    qInfo().nospace() << "ChatBridge sample: build_us=" << bridge.lastStateBuildDurationUs()
                      << ", dirty_models=" << bridge.lastSerializedModelCount()
                      << ", total_builds=" << bridge.stateBuildCount();
}

void ChatBridgeTests::workerDisconnectRunsOnWorkerThread() {
    auto* worker = new GuiConnectionWorker;
    QThread workerThread;
    worker->moveToThread(&workerThread);

    QSignalSpy disconnectedSpy(worker, &GuiConnectionWorker::disconnected);
    QThread* executingThread = nullptr;
    connect(worker, &GuiConnectionWorker::disconnected, this, [&executingThread]() {
        executingThread = QThread::currentThread();
    }, Qt::DirectConnection);

    workerThread.start();
    QVERIFY(QMetaObject::invokeMethod(worker, "disconnectFromServer", Qt::QueuedConnection));
    QTRY_COMPARE_WITH_TIMEOUT(disconnectedSpy.count(), 1, 500);
    QCOMPARE(executingThread, &workerThread);

    QVERIFY(QMetaObject::invokeMethod(worker, [worker]() { delete worker; }, Qt::BlockingQueuedConnection));
    worker = nullptr;
    workerThread.quit();
    QVERIFY(workerThread.wait(1000));
}

void ChatBridgeTests::attachmentPipelineTransfersPastChatBurstLimit() {
#ifndef LAN_CHAT_ENABLE_MLSPP
    QSKIP("Attachment encryption support is disabled for this build");
#else
    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) QSKIP("Local port 8888 is occupied by an interactive host");
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto paths = HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!paths.available()) QSKIP("Local Host executable is unavailable");
    WinsockScope winsock;
    QVERIFY(winsock.result() == 0);
    GuiChatController host;
    QSignalSpy hostEvents(&host, &GuiChatController::attachmentEvent);
    host.connectToLocalHost(paths.serverExe, temporary.filePath("server.crt"),
        temporary.filePath("server.key"), temporary.filePath("chat.db"), "Alice", "A001");
    QTRY_VERIFY_WITH_TIMEOUT(host.connected(), 12000);

    GuiChatController sender;
    QSignalSpy senderConnected(&sender, &GuiChatController::connectedChanged);
    QSignalSpy senderFailed(&sender, &GuiChatController::connectionFailed);
    QSignalSpy senderEvents(&sender, &GuiChatController::attachmentEvent);
    const auto cleanup = qScopeGuard([&] {
        sender.disconnectFromServer();
        host.disconnectFromServer();
        waitForLocalHostPortClosed();
    });
    sender.connectToServerWithTlsName("127.0.0.1", 8888, "Bob", "B001",
                                      temporary.filePath("server.crt"), "localhost");
    QVariantMap approval;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const QVariant& pending : host.pendingConnectionApprovals()) {
            const QVariantMap candidate = pending.toMap();
            if (candidate.value("userCode").toString() == QStringLiteral("B001")) {
                approval = candidate;
                return true;
            }
        }
        return false;
    }(), 5000);
    host.sendAdminAction("approve_connection", approval.value("userCode").toString(), approval.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(senderConnected.count() > 0 || senderFailed.count() > 0, 12000);
    QVERIFY2(sender.connected(), qPrintable(sender.statusText()));

    const QByteArray plaintext(static_cast<int>(attachments::TransferClient::ChunkSize * 128 + 17), 'x');
    const QString filePath = temporary.filePath("pipeline.bin");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(plaintext), plaintext.size());
    file.close();
    QElapsedTimer elapsed;
    elapsed.start();
    sender.startAttachmentUpload("lobby", filePath, {"A001"}, "pipeline");
    const auto terminal = [&]() {
        for (const auto& event : senderEvents) {
            if (event.at(0).toString() == "attachment.commit" || event.at(0).toString() == "error") return true;
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(terminal(), 30000);
    int acknowledgements = 0;
    bool committed = false;
    for (const auto& event : senderEvents) {
        QVERIFY2(event.at(0).toString() != "error", qPrintable(event.at(10).toString()));
        if (event.at(0).toString() == "attachment.chunk") ++acknowledgements;
        if (event.at(0).toString() == "attachment.commit") {
            QCOMPARE(acknowledgements, 129);
            committed = true;
        }
    }
    QVERIFY(committed);
    qInfo("Pipeline uploaded %lld bytes in %lld ms (129 acknowledgements)",
          static_cast<long long>(plaintext.size()), static_cast<long long>(elapsed.elapsed()));
    const auto attachmentCard = [](ChatListModel* model) {
        for (int row = 0; row < model->rowCount(); ++row) {
            const QVariantMap attachment = model->valueAt(row, "attachment").toMap();
            if (!attachment.value("attachmentId").toString().isEmpty()) return attachment;
        }
        return QVariantMap{};
    };
    QTRY_VERIFY_WITH_TIMEOUT(!attachmentCard(sender.messageModel()).isEmpty(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!attachmentCard(host.messageModel()).isEmpty(), 5000);
    const QVariantMap senderCard = attachmentCard(sender.messageModel());
    const QVariantMap hostCard = attachmentCard(host.messageModel());
    QCOMPARE(senderCard.value("attachmentId"), hostCard.value("attachmentId"));
    QCOMPARE(hostCard.value("fileName").toString(), QStringLiteral("pipeline.bin"));
    QCOMPARE(hostCard.value("logicalSize").toLongLong(), static_cast<qint64>(plaintext.size()));
    QCOMPARE(hostCard.value("status").toString(), QStringLiteral("available"));

    const QString downloadPath = temporary.filePath("downloaded-pipeline.bin");
    host.startAttachmentDownload(hostCard.value("attachmentId").toString(), downloadPath,
                                 QStringLiteral("pipeline-download"));
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const auto& event : hostEvents) {
            if (event.at(0).toString() == QStringLiteral("completed") &&
                event.at(3).toString() == QStringLiteral("pipeline-download")) return true;
        }
        return false;
    }(), 30000);
    QFile downloaded(downloadPath);
    QVERIFY(downloaded.open(QIODevice::ReadOnly));
    const QByteArray downloadedHash = QCryptographicHash::hash(downloaded.readAll(), QCryptographicHash::Sha256);
    QCOMPARE(downloadedHash, QCryptographicHash::hash(plaintext, QCryptographicHash::Sha256));
#endif
}

void ChatBridgeTests::oversizedAttachmentFailsBeforeConnectionSetup() {
    GuiConnectionWorker worker;
    QSignalSpy events(&worker, &GuiConnectionWorker::attachmentEvent);
    worker.sendAttachmentInit(QStringLiteral("lobby"), 5LL * 1024 * 1024 * 1024 + 1,
                              QStringLiteral("too-large"));
    QCOMPARE(events.count(), 1);
    QCOMPARE(events.first().at(3).toString(), QStringLiteral("too-large"));
    QCOMPARE(events.first().at(10).toString(), QStringLiteral("文件超过 5 GiB 上限"));
}

void ChatBridgeTests::attachmentUploadEventsCarrySizeAndAcknowledgedIndexes() {
#ifndef LAN_CHAT_ENABLE_MLSPP
    QSKIP("Attachment encryption support is disabled for this build");
#else
    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) QSKIP("Local port 8888 is occupied by an interactive host");

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto hostPaths = HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available()) QSKIP("Local Host executable is not available in this checkout");
    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    GuiChatController controller;
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);
    QSignalSpy failedSpy(&controller, &GuiChatController::connectionFailed);
    controller.connectToLocalHost(hostPaths.serverExe, temporary.filePath("server.crt"),
                                  temporary.filePath("server.key"), temporary.filePath("chat.db"),
                                  "Progress Test", "PROGRESS001");
    QTRY_VERIFY_WITH_TIMEOUT(controller.connected() || !failedSpy.isEmpty(), 12000);
    QVERIFY2(controller.connected(), qPrintable(controller.statusText()));

    const auto eventPayload = [&](const QString& type, const QString& id) {
        for (const auto& arguments : resultSpy) {
            const auto event = QJsonDocument::fromJson(arguments.at(0).toString().toUtf8()).object();
            const auto payload = event.value("payload").toObject();
            if (event.value("type").toString() == "attachment.event" &&
                event.value("id").toString() == id && payload.value("type").toString() == type) {
                return payload;
            }
        }
        return QJsonObject{};
    };
    const QString commandId = QStringLiteral("progress-upload");
    const qint64 chunkSize = attachments::TransferClient::ChunkSize;
    const qint64 logicalSize = chunkSize * 5 + 17;
    controller.sendAttachmentInit("lobby", logicalSize, commandId);
    QTRY_VERIFY_WITH_TIMEOUT(!eventPayload("attachment.init", commandId).isEmpty(), 5000);
    const auto init = eventPayload("attachment.init", commandId);
    QCOMPARE(init.value("logicalSize").toInteger(), logicalSize);
    QCOMPARE(init.value("chunkSize").toInteger(), chunkSize);
    QCOMPARE((init.value("logicalSize").toInteger() + chunkSize - 1) / chunkSize, 6);

    // Exercise real TLS chunk acknowledgements with generated plaintext and a
    // test-only key; no user file or recipient MLS session is involved.
    const QString uploadId = init.value("uploadId").toString();
    QBuffer input;
    input.setData(QByteArray(static_cast<int>(logicalSize), 'p'));
    QVERIFY(input.open(QIODevice::ReadOnly));
    attachments::AttachmentCrypto::Key key{};
    QVERIFY(attachments::AttachmentCrypto::generate_key(key));
    attachments::ChunkContext context{init.value("attachmentId").toString().toStdString(),
                                      "lobby", "progress-test-group", logicalSize, chunkSize, 0};
    for (qint64 index = 0; index < 6; ++index) {
        context.chunk_index = index;
        attachments::TransferChunk chunk;
        bool endOfFile = false;
        QString error;
        QVERIFY2(attachments::TransferClient::encrypt_next_chunk(input, key, context, chunk, endOfFile, &error),
                 qPrintable(error));
        QCOMPARE(endOfFile, index == 5);
        const QString chunkCommand = commandId + QStringLiteral("-chunk-%1").arg(index);
        controller.sendAttachmentChunk(uploadId, index, chunk.ciphertext, chunk.cipher_sha256, chunkCommand);
        QTRY_VERIFY_WITH_TIMEOUT(!eventPayload("attachment.chunk", chunkCommand).isEmpty(), 5000);
        QCOMPARE(eventPayload("attachment.chunk", chunkCommand).value("chunkIndex").toInteger(), index);
        if (index == 4) {
            const QString resumeCommand = commandId + QStringLiteral("-resume");
            controller.resumeAttachment(uploadId, resumeCommand);
            QTRY_VERIFY_WITH_TIMEOUT(!eventPayload("attachment.resume", resumeCommand).isEmpty(), 5000);
            QCOMPARE(eventPayload("attachment.resume", resumeCommand).value("receivedIndexes").toArray(),
                     QJsonArray({0, 1, 2, 3, 4}));
        }
    }
    const QString commitCommand = commandId + QStringLiteral("-commit");
    controller.sendAttachmentCommit(uploadId, commitCommand);
    QTRY_VERIFY_WITH_TIMEOUT(!eventPayload("attachment.commit", commitCommand).isEmpty(), 5000);
    controller.disconnectFromServer();
    QVERIFY2(waitForLocalHostPortClosed(), "test host remained listening after disconnect");
#endif
}

void ChatBridgeTests::serverConnectionCompletesWithoutMessageLifetimeCorruption() {
    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) {
        QSKIP("Local port 8888 is occupied by an interactive host");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available()) {
        QSKIP("Local Host executable is not available in this checkout");
    }
    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    const QString certFile = temporary.filePath(QStringLiteral("certs/server-lan.crt"));
    const QString keyFile = temporary.filePath(QStringLiteral("certs/server-lan.key"));
    const QString dbFile = temporary.filePath(QStringLiteral("chat.db"));
    GuiChatController host;
    QSignalSpy hostConnectedSpy(&host, &GuiChatController::connectedChanged);
    QSignalSpy hostFailedSpy(&host, &GuiChatController::connectionFailed);
    host.connectToLocalHost(hostPaths.serverExe, certFile, keyFile, dbFile, "Alice", "A001");
    QTRY_VERIFY_WITH_TIMEOUT(hostConnectedSpy.count() > 0 || hostFailedSpy.count() > 0, 12000);
    QVERIFY2(host.connected(), qPrintable(host.statusText()));

    GuiChatController controller;
    QSignalSpy connectedSpy(&controller, &GuiChatController::connectedChanged);
    QSignalSpy failedSpy(&controller, &GuiChatController::connectionFailed);
    controller.connectToServerWithTlsName("127.0.0.1", 8888, "Bob", "B001", certFile, "localhost");

    QVariantMap approval;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        for (const QVariant& pending : host.pendingConnectionApprovals()) {
            const QVariantMap candidate = pending.toMap();
            if (candidate.value("userCode").toString() == QStringLiteral("B001")) {
                approval = candidate;
                return true;
            }
        }
        return false;
    }(), 5000);
    host.sendAdminAction("approve_connection", approval.value("userCode").toString(), approval.value("id").toString());

    QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() > 0 || failedSpy.count() > 0, 10000);
    QVERIFY2(controller.connected(), qPrintable(controller.statusText()));
    controller.disconnectFromServer();
    host.disconnectFromServer();
    QVERIFY2(waitForLocalHostPortClosed(), "test host remained listening after disconnect");
}

void ChatBridgeTests::localHostConnectionCompletesWithoutMessageLifetimeCorruption() {
    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) {
        QSKIP("Local port 8888 is occupied by an interactive host");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available()) {
        QSKIP("Local Host executable is not available in this checkout");
    }

    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    GuiChatController controller;
    QSignalSpy connectedSpy(&controller, &GuiChatController::connectedChanged);
    QSignalSpy failedSpy(&controller, &GuiChatController::connectionFailed);
    controller.connectToLocalHost(hostPaths.serverExe, temporary.filePath(QStringLiteral("certs/server-lan.crt")),
                                  temporary.filePath(QStringLiteral("certs/server-lan.key")),
                                  temporary.filePath(QStringLiteral("chat.db")), "Alice", "A001");

    QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() > 0 || failedSpy.count() > 0, 12000);
    QVERIFY2(controller.connected(), qPrintable(controller.statusText()));
    controller.disconnectFromServer();
    QVERIFY2(waitForLocalHostPortClosed(), "test host remained listening after disconnect");
}

void ChatBridgeTests::approvedLanMemberConnectionCompletesAfterLoginPending() {
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available()) {
        QSKIP("Local Host executable is not available in this checkout");
    }

    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) {
        QSKIP("Local port 8888 is occupied by an interactive host");
    }

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString certFile = temporary.filePath(QStringLiteral("certs/server-lan.crt"));
    const QString keyFile = temporary.filePath(QStringLiteral("certs/server-lan.key"));
    const QString dbFile = temporary.filePath(QStringLiteral("chat.db"));
    QVERIFY(QDir().mkpath(QFileInfo(certFile).absolutePath()));

    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    GuiChatController host;
    QSignalSpy hostConnectedSpy(&host, &GuiChatController::connectedChanged);
    QSignalSpy hostFailedSpy(&host, &GuiChatController::connectionFailed);
    host.connectToLocalHost(hostPaths.serverExe, certFile, keyFile, dbFile, "Alice", "A001");
    QTRY_VERIFY_WITH_TIMEOUT(hostConnectedSpy.count() > 0 || hostFailedSpy.count() > 0, 12000);
    QVERIFY2(host.connected(), qPrintable(host.statusText()));
    QVERIFY(host.admin());

    GuiChatController member;
    QSignalSpy memberConnectedSpy(&member, &GuiChatController::connectedChanged);
    QSignalSpy memberFailedSpy(&member, &GuiChatController::connectionFailed);
    member.connectToServerWithTlsName("127.0.0.1", 8888, "Bob", "B001", certFile, "localhost");

    QTRY_VERIFY_WITH_TIMEOUT(!host.pendingConnectionApprovals().isEmpty(), 5000);
    const QVariantMap request = host.pendingConnectionApprovals().front().toMap();
    QCOMPARE(request.value("displayName").toString(), QStringLiteral("Bob"));
    QCOMPARE(request.value("userCode").toString(), QStringLiteral("B001"));
    QVERIFY(!request.value("id").toString().isEmpty());

    host.sendAdminAction("approve_connection", request.value("userCode").toString(),
                         request.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(memberConnectedSpy.count() > 0 || memberFailedSpy.count() > 0, 12000);
    QVERIFY2(member.connected(), qPrintable(member.statusText()));
    member.disconnectFromServer();
    host.disconnectFromServer();
    QVERIFY2(waitForLocalHostPortClosed(), "local host remained listening after disconnect");
}

void ChatBridgeTests::authenticatedRawTlsMLSFramesAreRejected() {
#ifndef LAN_CHAT_ENABLE_MLSPP
    QSKIP("MLS++ support is disabled for this build");
#else
    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) {
        QSKIP("Local port 8888 is occupied by an interactive host");
    }

    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    qputenv("LAN_CHAT_TEST_HOST_DATA_ROOT", temporary.path().toUtf8());
    const auto clearTestRoot = qScopeGuard([]() { qunsetenv("LAN_CHAT_TEST_HOST_DATA_ROOT"); });
    const auto hostPaths = HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available()) {
        QSKIP("Local Host executable is not available in this checkout");
    }

    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    GuiChatController host;
    QSignalSpy hostConnectedSpy(&host, &GuiChatController::connectedChanged);
    QSignalSpy hostFailedSpy(&host, &GuiChatController::connectionFailed);
    QSignalSpy hostMlsSpy(&host, &GuiChatController::mlsCommandResult);
    QSignalSpy hostGroupSpy(&host, &GuiChatController::mlsGroupState);
    host.connectToLocalHost(hostPaths.serverExe, hostPaths.certFile, hostPaths.keyFile,
                            temporary.filePath(QStringLiteral("chat.db")), "Alice", "A001");
    QTRY_VERIFY_WITH_TIMEOUT(hostConnectedSpy.count() > 0 || hostFailedSpy.count() > 0, 12000);
    QVERIFY2(host.connected(), qPrintable(host.statusText()));
    QVERIFY(host.admin());

    GuiChatController member;
    QSignalSpy memberConnectedSpy(&member, &GuiChatController::connectedChanged);
    QSignalSpy memberFailedSpy(&member, &GuiChatController::connectionFailed);
    QSignalSpy memberGroupSpy(&member, &GuiChatController::mlsGroupState);
    member.connectToServerWithTlsName("127.0.0.1", 8888, "Bob", "B001", hostPaths.certFile, "localhost");
    QVariantMap memberRequest;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            const auto request = value.toMap();
            if (request.value("userCode").toString().compare(QStringLiteral("B001"), Qt::CaseInsensitive) == 0) {
                memberRequest = request;
                return true;
            }
        }
        return false;
    }(), 5000);
    QVERIFY(!memberRequest.isEmpty());
    host.sendAdminAction("approve_connection", memberRequest.value("userCode").toString(),
                         memberRequest.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(memberConnectedSpy.count() > 0 || memberFailedSpy.count() > 0, 12000);
    QVERIFY2(member.connected(), qPrintable(member.statusText()));

    QSignalSpy hostMlsResultSpy(&host, &GuiChatController::mlsCommandResult);
    host.fetchMlsKeyPackage(QStringLiteral("lobby"), QStringLiteral("B001"), QStringLiteral("raw-fetch-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsResultSpy.count() > 0, 5000);
    QVERIFY2(hostMlsResultSpy.at(0).at(1).toBool(), qPrintable(QStringLiteral("fetch-bob failed: %1")
                                                                 .arg(hostMlsResultSpy.at(0).at(3).toString())));
    host.addMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                      QStringLiteral("B001"), QStringLiteral("raw-add-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsResultSpy.count() > 1, 8000);
    QVERIFY2(hostMlsResultSpy.at(1).at(1).toBool(), qPrintable(QStringLiteral("add-bob failed: %1")
                                                                 .arg(hostMlsResultSpy.at(1).at(3).toString())));
    member.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("raw-bob-ready"));
    QTRY_VERIFY_WITH_TIMEOUT(memberGroupSpy.count() > 0, 5000);
    QVERIFY2(memberGroupSpy.back().at(1).toBool(), "Bob did not complete the welcome before raw takeover");
    QCOMPARE(memberGroupSpy.back().at(3).toULongLong(), quint64(1));

    GuiChatController newMember;
    QSignalSpy newMemberConnectedSpy(&newMember, &GuiChatController::connectedChanged);
    QSignalSpy newMemberFailedSpy(&newMember, &GuiChatController::connectionFailed);
    QSignalSpy newMemberMlsSpy(&newMember, &GuiChatController::mlsCommandResult);
    QSignalSpy newMemberGroupSpy(&newMember, &GuiChatController::mlsGroupState);
    newMember.connectToServerWithTlsName("127.0.0.1", 8888, "Carol", "C001", hostPaths.certFile, "localhost");
    QVariantMap newMemberRequest;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            const auto request = value.toMap();
            if (request.value("userCode").toString().compare(QStringLiteral("C001"), Qt::CaseInsensitive) == 0) {
                newMemberRequest = request;
                return true;
            }
        }
        return false;
    }(), 5000);
    QVERIFY(!newMemberRequest.isEmpty());
    host.sendAdminAction("approve_connection", newMemberRequest.value("userCode").toString(),
                         newMemberRequest.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(newMemberConnectedSpy.count() > 0 || newMemberFailedSpy.count() > 0, 12000);
    QVERIFY2(newMember.connected(), qPrintable(newMember.statusText()));
    const int fetchCountBeforeCarol = hostMlsResultSpy.count();
    host.fetchMlsKeyPackage(QStringLiteral("lobby"), QStringLiteral("C001"), QStringLiteral("raw-fetch-carol"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsResultSpy.count() > fetchCountBeforeCarol, 5000);
    QVERIFY2(hostMlsResultSpy.at(fetchCountBeforeCarol).at(1).toBool(),
             qPrintable(QStringLiteral("fetch-carol failed: %1").arg(hostMlsResultSpy.at(fetchCountBeforeCarol).at(3).toString())));
    QVERIFY(QMetaObject::invokeMethod(&newMember, "handleConnectionLost", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("raw tampered welcome setup"))));
    QTRY_VERIFY_WITH_TIMEOUT(newMember.reconnecting(), 1000);

    // Bob's controller is deliberately closed before the raw authenticated
    // client takes the same account. This leaves the real host as the only
    // legal MLS member receiving the opaque attack frames.
    member.disconnectFromServer();
    QTest::qWait(300);

    auto approve = [&](const QString& userCode) {
        QVariantMap request;
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            const auto candidate = value.toMap();
            if (candidate.value("userCode").toString().compare(userCode, Qt::CaseInsensitive) == 0) {
                request = candidate;
                break;
            }
        }
        if (request.isEmpty()) return false;
        host.sendAdminAction("approve_connection", request.value("userCode").toString(),
                             request.value("id").toString());
        return true;
    };
    auto hasPending = [&](const QString& userCode) {
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            if (value.toMap().value("userCode").toString().compare(userCode, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    auto pendingCodes = [&]() {
        QStringList codes;
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            codes.append(value.toMap().value("userCode").toString());
        }
        return codes.join(QLatin1Char(','));
    };
    auto expectRawError = [&](RawTlsClient& socket, const QJsonObject& frame,
                              const QString& commandId, const QString& content) {
        if (!sendRawTlsFrame(socket, frame)) return false;
        QJsonObject response;
        if (!receiveRawTlsType(socket, QStringLiteral("error"), &response, 5000)) return false;
        return response.value(QStringLiteral("command_id")).toString() == commandId &&
               response.value(QStringLiteral("content")).toString() == content;
    };
    auto expectRawAck = [&](RawTlsClient& socket, const QJsonObject& frame,
                            const QString& type, const QString& commandId,
                            const QString& content) {
        if (!sendRawTlsFrame(socket, frame)) return false;
        QJsonObject response;
        if (!receiveRawTlsType(socket, type, &response, 5000)) return false;
        if (response.value(QStringLiteral("command_id")).toString() != commandId) return false;
        if (!content.isEmpty() && response.value(QStringLiteral("content")).toString() != content) return false;
        return true;
    };

    RawTlsClient mallory;
    QVERIFY2(mallory.connectToHost(QStringLiteral("127.0.0.1"), 8888, hostPaths.certFile,
                                   QStringLiteral("localhost")),
             qPrintable(mallory.errorString()));
    QVERIFY(sendRawTlsFrame(mallory, QJsonObject{{"type", "login"}, {"username", "Mallory"}, {"user_code", "M001"}}));
    QJsonObject malloryPending;
    QVERIFY(receiveRawTlsType(mallory, QStringLiteral("login_pending"), &malloryPending, 5000));
    QTRY_VERIFY_WITH_TIMEOUT(hasPending(QStringLiteral("M001")), 5000);
    QVERIFY(approve(QStringLiteral("M001")));
    QJsonObject malloryLogin;
    QVERIFY(receiveRawTlsType(mallory, QStringLiteral("login_ok"), &malloryLogin, 5000));

    QVERIFY(expectRawError(mallory,
                           QJsonObject{{"type", "mls.group.proposal"}, {"command_id", "raw-mallory-proposal"},
                                       {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                       {"proposal_id", "raw-mallory-proposal"}, {"action", "add"},
                                       {"target_user_code", "C001"}, {"proposal", "YQ=="}},
                           QStringLiteral("raw-mallory-proposal"), QStringLiteral("MLS group proposal access denied")));
    QVERIFY(expectRawError(mallory,
                           QJsonObject{{"type", "mls.group.commit"}, {"command_id", "raw-mallory-commit"},
                                       {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                       {"proposal_id", "raw-mallory-proposal"}, {"commit", "Yg=="}},
                           QStringLiteral("raw-mallory-commit"), QStringLiteral("MLS group commit access denied")));
    QVERIFY(expectRawError(mallory,
                           QJsonObject{{"type", "mls.group.welcome.accept"}, {"command_id", "raw-mallory-accept"},
                                       {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 1},
                                       {"proposal_id", "raw-mallory-proposal"}, {"target_user_code", "M001"},
                                       {"welcome_digest", QString(64, QLatin1Char('0'))}},
                           QStringLiteral("raw-mallory-accept"), QStringLiteral("MLS welcome accept conflict")));
    const QJsonObject publishMallory{{"type", "mls.key_package.publish"}, {"command_id", "raw-publish-1"},
                                     {"key_package", "YQ=="}};
    QVERIFY(expectRawAck(mallory, publishMallory, QStringLiteral("mls.key_package.publish"),
                         QStringLiteral("raw-publish-1"), QStringLiteral("stored")));
    QVERIFY(expectRawAck(mallory, publishMallory, QStringLiteral("mls.key_package.publish"),
                         QStringLiteral("raw-publish-1"), QStringLiteral("stored")));
    mallory.disconnect();

    RawTlsClient rawBob;
    QVERIFY2(rawBob.connectToHost(QStringLiteral("127.0.0.1"), 8888, hostPaths.certFile,
                                  QStringLiteral("localhost")),
             qPrintable(rawBob.errorString()));
    QVERIFY(sendRawTlsFrame(rawBob, QJsonObject{{"type", "login"}, {"username", "Bob"}, {"user_code", "B001"}}));
    QJsonObject bobPending;
    QVERIFY(receiveRawTlsType(rawBob, QStringLiteral("login_pending"), &bobPending, 5000));
    QTRY_VERIFY_WITH_TIMEOUT(hasPending(QStringLiteral("B001")), 5000);
    QVERIFY2(approve(QStringLiteral("B001")), qPrintable(QStringLiteral("pending approvals: %1").arg(pendingCodes())));
    QJsonObject bobLogin;
    QVERIFY(receiveRawTlsType(rawBob, QStringLiteral("login_ok"), &bobLogin, 5000));

    // A valid member can persist a proposal without advancing the group. A
    // commit that skips the next epoch must still be rejected by the server.
    const QJsonObject acceptedProposalWithoutCommit{{"type", "mls.group.proposal"},
                                                    {"command_id", "raw-no-commit-proposal"},
                                                    {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                                    {"proposal_id", "raw-no-commit-proposal"}, {"action", "add"},
                                                    {"target_user_code", "C001"}, {"proposal", "Yg=="}};
    QVERIFY(expectRawAck(rawBob, acceptedProposalWithoutCommit, QStringLiteral("mls.group.proposal"),
                         QStringLiteral("raw-no-commit-proposal"), QStringLiteral("stored")));
    QTest::qWait(300);
    const QJsonObject skippedEpochCommit{{"type", "mls.group.commit"}, {"command_id", "raw-skipped-epoch"},
                                         {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 3},
                                         {"proposal_id", "raw-no-commit-proposal"}, {"commit", "Yw=="}};
    QVERIFY(expectRawError(rawBob, skippedEpochCommit, QStringLiteral("raw-skipped-epoch"),
                           QStringLiteral("MLS group commit conflict")));
    host.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("raw-before-no-commit"));
    QTRY_VERIFY_WITH_TIMEOUT(hostGroupSpy.count() > 0, 5000);
    QCOMPARE(hostGroupSpy.at(0).at(3).toULongLong(), quint64(1));

    const QJsonObject missingProposalCommit{{"type", "mls.group.commit"}, {"command_id", "raw-missing-proposal"},
                                            {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                            {"proposal_id", "missing-proposal"}, {"commit", "Y29tbWl0"}};
    QVERIFY(expectRawError(rawBob, missingProposalCommit, QStringLiteral("raw-missing-proposal"),
                           QStringLiteral("MLS group commit requires an accepted proposal")));
    QVERIFY(expectRawError(rawBob,
                           QJsonObject{{"type", "mls.group.commit"}, {"command_id", "raw-wrong-proposal"},
                                       {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                       {"proposal_id", "wrong-proposal"}, {"commit", "Y29tbWl0"}},
                           QStringLiteral("raw-wrong-proposal"), QStringLiteral("MLS group commit requires an accepted proposal")));

    const QJsonObject tamperedProposal{{"type", "mls.group.proposal"}, {"command_id", "raw-tampered-proposal"},
                                       {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                       {"proposal_id", "raw-tampered-proposal"}, {"action", "add"},
                                       {"target_user_code", "C001"}, {"proposal", "YQ=="}};
    QVERIFY(expectRawAck(rawBob, tamperedProposal, QStringLiteral("mls.group.proposal"),
                         QStringLiteral("raw-tampered-proposal"), QStringLiteral("stored")));
    QVERIFY(expectRawAck(rawBob, tamperedProposal, QStringLiteral("mls.group.proposal"),
                         QStringLiteral("raw-tampered-proposal"), QStringLiteral("duplicate")));
    const QJsonObject tamperedCommit{{"type", "mls.group.commit"}, {"command_id", "raw-tampered-commit"},
                                     {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                     {"proposal_id", "raw-tampered-proposal"}, {"commit", "Yg=="}};
    QVERIFY(expectRawAck(rawBob, tamperedCommit, QStringLiteral("mls.group.commit"),
                         QStringLiteral("raw-tampered-commit"), QStringLiteral("stored")));
    QVERIFY(expectRawAck(rawBob, tamperedCommit, QStringLiteral("mls.group.commit"),
                         QStringLiteral("raw-tampered-commit"), QStringLiteral("duplicate")));
    const QByteArray tamperedWelcome = QByteArrayLiteral("Yw==");
    const QString tamperedDigest = QString::fromLatin1(QCryptographicHash::hash(tamperedWelcome,
                                                                                QCryptographicHash::Sha256).toHex());
    const QJsonObject tamperedWelcomeFrame{{"type", "mls.group.welcome"}, {"command_id", "raw-tampered-welcome"},
                                           {"group_id", "group-e2e"}, {"room", "lobby"}, {"epoch", 2},
                                           {"proposal_id", "raw-tampered-proposal"}, {"target_user_code", "C001"},
                                           {"welcome_digest", tamperedDigest}, {"welcome", tamperedWelcome.constData()}};
    QVERIFY(expectRawAck(rawBob, tamperedWelcomeFrame, QStringLiteral("mls.group.welcome"),
                         QStringLiteral("raw-tampered-welcome"), QStringLiteral("stored")));
    QVERIFY(expectRawAck(rawBob, tamperedWelcomeFrame, QStringLiteral("mls.group.welcome"),
                         QStringLiteral("raw-tampered-welcome"), QStringLiteral("duplicate")));
    // The target-only delivery contract must not leak Carol's pending welcome
    // to the submitting member, even though Bob is still authenticated.
    QJsonObject leakedWelcome;
    QVERIFY(!receiveRawTlsType(rawBob, QStringLiteral("mls.group.welcome"), &leakedWelcome, 300));
    rawBob.disconnect();

    // The opaque tampered commit is intentionally persisted by the server;
    // cryptographic rejection is a client responsibility. Alice's real MLS
    // session must nevertheless stay at epoch 1 after receiving it.
    host.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("raw-before"));
    QTRY_VERIFY_WITH_TIMEOUT(hostGroupSpy.count() > 0, 5000);
    QCOMPARE(hostGroupSpy.at(0).at(3).toULongLong(), quint64(1));
    QVERIFY(!hostMlsSpy.isEmpty());
    bool sawRejectedInbound = false;
    for (int index = 0; index < hostMlsSpy.count(); ++index) {
        const auto result = hostMlsSpy.at(index);
        if (result.at(0).toString().isEmpty() && !result.at(1).toBool()) {
            sawRejectedInbound = true;
            break;
        }
    }
    QVERIFY2(sawRejectedInbound, "real host did not reject the opaque tampered MLS frames");
    host.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("raw-after"));
    QTRY_VERIFY_WITH_TIMEOUT(hostGroupSpy.count() > 1, 5000);
    QCOMPARE(hostGroupSpy.at(1).at(3).toULongLong(), quint64(1));

    // Approve Carol's existing reconnect only after the pending welcome was
    // durably stored. The retained key package must reject the tampered bytes,
    // and no welcome.accept or local group may be created.
    QTRY_VERIFY_WITH_TIMEOUT(hasPending(QStringLiteral("C001")), 5000);
    QVERIFY(approve(QStringLiteral("C001")));
    QTRY_VERIFY_WITH_TIMEOUT(newMember.connected(), 12000);
    QVERIFY(!newMemberMlsSpy.isEmpty());
    bool sawWelcomeRejection = false;
    for (int index = 0; index < newMemberMlsSpy.count(); ++index) {
        const auto result = newMemberMlsSpy.at(index);
        if (!result.at(1).toBool()) {
            sawWelcomeRejection = true;
            break;
        }
    }
    QVERIFY2(sawWelcomeRejection, "tampered welcome was not rejected by the target MLS client");
    QCOMPARE(newMemberGroupSpy.count(), 0);

    newMember.disconnectFromServer();
    host.disconnectFromServer();
#endif
}

void ChatBridgeTests::mlsControlRoundTripUsesProposalCommitWelcomeOrder() {
#ifndef LAN_CHAT_ENABLE_MLSPP
    QSKIP("MLS++ support is disabled for this build");
#else
    QTcpSocket portProbe;
    portProbe.connectToHost(QStringLiteral("127.0.0.1"), 8888);
    if (portProbe.waitForConnected(100)) {
        QSKIP("Local port 8888 is occupied by an interactive host");
    }
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    qputenv("LAN_CHAT_TEST_HOST_DATA_ROOT", temporary.path().toUtf8());
    const auto clearTestRoot = qScopeGuard([]() { qunsetenv("LAN_CHAT_TEST_HOST_DATA_ROOT"); });
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available()) {
        QSKIP("Local Host executable is not available in this checkout");
    }

    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    GuiChatController host;
    QSignalSpy hostConnectedSpy(&host, &GuiChatController::connectedChanged);
    QSignalSpy hostFailedSpy(&host, &GuiChatController::connectionFailed);
    host.connectToLocalHost(hostPaths.serverExe, hostPaths.certFile, hostPaths.keyFile,
                            temporary.filePath(QStringLiteral("chat.db")), "Alice", "A001");
    QTRY_VERIFY_WITH_TIMEOUT(hostConnectedSpy.count() > 0 || hostFailedSpy.count() > 0, 12000);
    QVERIFY2(host.connected(), qPrintable(host.statusText()));
    QVERIFY(host.admin());

    GuiChatController member;
    QSignalSpy memberConnectedSpy(&member, &GuiChatController::connectedChanged);
    QSignalSpy memberFailedSpy(&member, &GuiChatController::connectionFailed);
    QSignalSpy memberMlsSpy(&member, &GuiChatController::mlsCommandResult);
    member.connectToServerWithTlsName("127.0.0.1", 8888, "Bob", "B001", hostPaths.certFile, "localhost");
    QTRY_VERIFY_WITH_TIMEOUT(!host.pendingConnectionApprovals().isEmpty(), 5000);
    const QVariantMap request = host.pendingConnectionApprovals().front().toMap();
    host.sendAdminAction("approve_connection", request.value("userCode").toString(), request.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(memberConnectedSpy.count() > 0 || memberFailedSpy.count() > 0, 12000);
    QVERIFY2(member.connected(), qPrintable(member.statusText()));

    QSignalSpy hostMlsSpy(&host, &GuiChatController::mlsCommandResult);
    QSignalSpy hostKeyPackageSpy(&host, &GuiChatController::mlsKeyPackageAvailable);
    host.fetchMlsKeyPackage(QStringLiteral("lobby"), QStringLiteral("B001"), QStringLiteral("fetch-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(hostKeyPackageSpy.count() > 0, 5000);
    const QString bobKeyPackageDigest = hostKeyPackageSpy.back().at(1).toString();
    QVERIFY(!bobKeyPackageDigest.isEmpty());
    QCOMPARE(hostMlsSpy.at(0).at(0).toString(), QStringLiteral("fetch-bob"));
    QVERIFY(hostMlsSpy.at(0).at(1).toBool());
    // The legacy environment variable must never arm a production worker.
    qputenv("LAN_CHAT_TEST_DROP_MLS_COMMIT_ONCE", "1");
    host.addMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                      QStringLiteral("B001"), QStringLiteral("add-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 1, 8000);
    QVERIFY(!host.reconnecting());
    qunsetenv("LAN_CHAT_TEST_DROP_MLS_COMMIT_ONCE");
    QCOMPARE(hostMlsSpy.at(1).at(0).toString(), QStringLiteral("add-bob"));
    QVERIFY2(hostMlsSpy.at(1).at(1).toBool(), qPrintable(QStringLiteral("add-bob failed: code=%1 detail=%2").arg(hostMlsSpy.at(1).at(2).toString(), hostMlsSpy.at(1).at(3).toString())));
    // The first proposal/commit snapshot contains only Alice. Bob is
    // activated only after this operation's welcome, so no group-unavailable
    // error is expected on the joining client.
    QTest::qWait(500);
    QCOMPARE(memberMlsSpy.count(), 0);

    QSignalSpy hostGroupSpy(&host, &GuiChatController::mlsGroupState);
    QSignalSpy memberGroupSpy(&member, &GuiChatController::mlsGroupState);
    host.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-alice"));
    member.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostGroupSpy.count() > 0 && memberGroupSpy.count() > 0, 5000);
    QVERIFY(hostGroupSpy.at(0).at(1).toBool());
    QVERIFY(memberGroupSpy.at(0).at(1).toBool());
    QCOMPARE(hostGroupSpy.at(0).at(2).toString(), QStringLiteral("group-e2e"));
    QCOMPARE(memberGroupSpy.at(0).at(2).toString(), QStringLiteral("group-e2e"));
    QCOMPARE(hostGroupSpy.at(0).at(3).toULongLong(), memberGroupSpy.at(0).at(3).toULongLong());
    QSignalSpy hostDataSpy(&host, &GuiChatController::mlsDataResult);
    QSignalSpy memberDataSpy(&member, &GuiChatController::mlsDataResult);
    host.protectMls(QStringLiteral("group-e2e"), QByteArrayLiteral("alice-to-bob"), QStringLiteral("protect-ab"));
    QTRY_VERIFY_WITH_TIMEOUT(hostDataSpy.count() > 0, 5000);
    QVERIFY(hostDataSpy.at(0).at(1).toBool());
    const QByteArray aliceCiphertext = hostDataSpy.at(0).at(2).toByteArray();
    QVERIFY(!aliceCiphertext.isEmpty());
    member.unprotectMls(QStringLiteral("group-e2e"), aliceCiphertext, QStringLiteral("unprotect-ab"));
    QTRY_VERIFY_WITH_TIMEOUT(memberDataSpy.count() > 0, 5000);
    QVERIFY(memberDataSpy.at(0).at(1).toBool());
    QCOMPARE(memberDataSpy.at(0).at(2).toByteArray(), QByteArrayLiteral("alice-to-bob"));
    memberDataSpy.clear();
    member.protectMls(QStringLiteral("group-e2e"), QByteArrayLiteral("bob-to-alice"), QStringLiteral("protect-ba"));
    QTRY_VERIFY_WITH_TIMEOUT(memberDataSpy.count() > 0, 5000);
    QVERIFY(memberDataSpy.at(0).at(1).toBool());
    host.unprotectMls(QStringLiteral("group-e2e"), memberDataSpy.at(0).at(2).toByteArray(), QStringLiteral("unprotect-ba"));
    QTRY_VERIFY_WITH_TIMEOUT(hostDataSpy.count() > 1, 5000);
    QVERIFY(hostDataSpy.at(1).at(1).toBool());
    QCOMPARE(hostDataSpy.at(1).at(2).toByteArray(), QByteArrayLiteral("bob-to-alice"));

    GuiChatController newMember;
    QSignalSpy newMemberGroupSpy(&newMember, &GuiChatController::mlsGroupState);
    QSignalSpy newMemberDataSpy(&newMember, &GuiChatController::mlsDataResult);
    QSignalSpy newMemberMlsSpy(&newMember, &GuiChatController::mlsCommandResult);
    QSignalSpy newMemberWelcomeSpy(&newMember, &GuiChatController::mlsWelcomeEvent);
    QSignalSpy newMemberConnectedSpy(&newMember, &GuiChatController::connectedChanged);
    QSignalSpy newMemberFailedSpy(&newMember, &GuiChatController::connectionFailed);
    newMember.connectToServerWithTlsName("127.0.0.1", 8888, "Carol", "C001", hostPaths.certFile, "localhost");
    QTRY_VERIFY_WITH_TIMEOUT(!host.pendingConnectionApprovals().isEmpty(), 5000);
    const QVariantMap newRequest = host.pendingConnectionApprovals().back().toMap();
    host.sendAdminAction("approve_connection", newRequest.value("userCode").toString(), newRequest.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(newMemberConnectedSpy.count() > 0 || newMemberFailedSpy.count() > 0, 12000);
    QVERIFY2(newMember.connected(), qPrintable(newMember.statusText()));

    host.fetchMlsKeyPackage(QStringLiteral("lobby"), QStringLiteral("C001"), QStringLiteral("fetch-carol"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 2, 5000);
    QCOMPARE(hostMlsSpy.at(2).at(0).toString(), QStringLiteral("fetch-carol"));
    QVERIFY2(hostMlsSpy.at(2).at(1).toBool(), qPrintable(QStringLiteral("fetch-carol failed: code=%1 detail=%2")
                                                           .arg(hostMlsSpy.at(2).at(2).toString(), hostMlsSpy.at(2).at(3).toString())));

    // Exercise an actual transient reconnect while the next welcome is
    // pending. The reconnect must retain the package-backed PendingJoin until
    // the offline welcome is consumed.
    QVERIFY(QMetaObject::invokeMethod(&newMember, "handleConnectionLost", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("test transient disconnect"))));
    QTRY_VERIFY_WITH_TIMEOUT(newMember.reconnecting(), 1000);

    // Drop the real host connection after the proposal acknowledgement and
    // before its commit send. Reconnect must resume the retained operation.
    host.enableDropNextMlsCommitForTesting();

    host.addMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                      QStringLiteral("C001"), QStringLiteral("add-carol"));
    QTRY_VERIFY_WITH_TIMEOUT(host.reconnecting(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(host.connected(), 12000);
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 3, 8000);
    QCOMPARE(hostMlsSpy.at(3).at(0).toString(), QStringLiteral("add-carol"));
    QVERIFY(hostMlsSpy.at(3).at(1).toBool());
    // The reconnect login is intentionally held at the existing approval
    // gate while the welcome is pending. Approving it after the welcome was
    // stored exercises replay through the normal authenticated write pump.
    QTRY_VERIFY_WITH_TIMEOUT(!host.pendingConnectionApprovals().isEmpty(), 5000);
    const QVariantMap reconnectRequest = host.pendingConnectionApprovals().back().toMap();
    host.sendAdminAction("approve_connection", reconnectRequest.value("userCode").toString(),
                         reconnectRequest.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(newMember.connected(), 12000);
    QTRY_COMPARE_WITH_TIMEOUT(newMemberWelcomeSpy.count(), 2, 5000);
    QCOMPARE(newMemberWelcomeSpy.at(0).at(2).toString(), QStringLiteral("received"));
    QCOMPARE(newMemberWelcomeSpy.at(1).at(2).toString(), QStringLiteral("accept_sent"));
    for (int index = 0; index < newMemberMlsSpy.count(); ++index) {
        const auto result = newMemberMlsSpy.at(index);
        QVERIFY2(result.at(1).toBool(), "offline welcome replay was rejected by the joining client");
    }
    for (int index = 0; index < memberMlsSpy.count(); ++index) {
        const auto result = memberMlsSpy.at(index);
        QVERIFY2(result.at(1).toBool(), "existing member rejected the proposal/commit chain");
    }
    QCOMPARE(memberMlsSpy.count(), 0);
    host.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-alice-2"));
    member.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-bob-2"));
    newMember.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-carol"));
    QTRY_VERIFY_WITH_TIMEOUT(hostGroupSpy.count() > 1 && memberGroupSpy.count() > 1 && newMemberGroupSpy.count() > 0, 5000);
    QCOMPARE(hostGroupSpy.at(1).at(2).toString(), QStringLiteral("group-e2e"));
    QCOMPARE(memberGroupSpy.at(1).at(2).toString(), QStringLiteral("group-e2e"));
    QCOMPARE(newMemberGroupSpy.at(0).at(2).toString(), QStringLiteral("group-e2e"));
    QCOMPARE(hostGroupSpy.at(1).at(3).toULongLong(), memberGroupSpy.at(1).at(3).toULongLong());
    QCOMPARE(hostGroupSpy.at(1).at(3).toULongLong(), newMemberGroupSpy.at(0).at(3).toULongLong());
    hostDataSpy.clear();
    host.protectMls(QStringLiteral("group-e2e"), QByteArrayLiteral("alice-to-carol"), QStringLiteral("protect-ac"));
    QTRY_VERIFY_WITH_TIMEOUT(hostDataSpy.count() > 0, 5000);
    QVERIFY(hostDataSpy.at(0).at(1).toBool());
    newMember.unprotectMls(QStringLiteral("group-e2e"), hostDataSpy.at(0).at(2).toByteArray(), QStringLiteral("unprotect-ac"));
    QTRY_VERIFY_WITH_TIMEOUT(newMemberDataSpy.count() > 0, 5000);
    QVERIFY(newMemberDataSpy.at(0).at(1).toBool());
    QCOMPARE(newMemberDataSpy.at(0).at(2).toByteArray(), QByteArrayLiteral("alice-to-carol"));

    host.removeMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                         QStringLiteral("B001"), QStringLiteral("remove-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 4, 8000);
    QCOMPARE(hostMlsSpy.at(4).at(0).toString(), QStringLiteral("remove-bob"));
    QVERIFY(hostMlsSpy.at(4).at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(memberMlsSpy.count() > 0, 5000);
    QVERIFY(!memberMlsSpy.at(memberMlsSpy.count() - 1).at(1).toBool());
    QVERIFY(!memberMlsSpy.at(memberMlsSpy.count() - 1).at(3).toString().isEmpty());
    host.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-alice-3"));
    newMember.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-carol-2"));
    QTRY_VERIFY_WITH_TIMEOUT(hostGroupSpy.count() > 2 && newMemberGroupSpy.count() > 1, 5000);
    QVERIFY(hostGroupSpy.at(2).at(1).toBool());
    QVERIFY(newMemberGroupSpy.at(1).at(1).toBool());
    QCOMPARE(hostGroupSpy.at(2).at(3).toULongLong(), newMemberGroupSpy.at(1).at(3).toULongLong());
    hostDataSpy.clear();
    newMemberDataSpy.clear();
    memberDataSpy.clear();
    host.protectMls(QStringLiteral("group-e2e"), QByteArrayLiteral("after-remove"), QStringLiteral("protect-after-remove"));
    QTRY_VERIFY_WITH_TIMEOUT(hostDataSpy.count() > 0, 5000);
    QVERIFY(hostDataSpy.at(0).at(1).toBool());
    const QByteArray postRemovalCiphertext = hostDataSpy.at(0).at(2).toByteArray();
    newMember.unprotectMls(QStringLiteral("group-e2e"), postRemovalCiphertext, QStringLiteral("carol-after-remove"));
    QTRY_VERIFY_WITH_TIMEOUT(newMemberDataSpy.count() > 0, 5000);
    QVERIFY(newMemberDataSpy.at(0).at(1).toBool());
    QCOMPARE(newMemberDataSpy.at(0).at(2).toByteArray(), QByteArrayLiteral("after-remove"));
    member.unprotectMls(QStringLiteral("group-e2e"), postRemovalCiphertext, QStringLiteral("bob-after-remove"));
    QTRY_VERIFY_WITH_TIMEOUT(memberDataSpy.count() > 0, 5000);
    QVERIFY(!memberDataSpy.at(0).at(1).toBool());

    // A second transient disconnect must replay no accepted welcome and must
    // preserve the existing Carol group/session at the same epoch.
    const int mlsResultsBeforeReconnect = newMemberMlsSpy.count();
    const int groupStatesBeforeReconnect = newMemberGroupSpy.count();
    QVERIFY(QMetaObject::invokeMethod(&newMember, "handleConnectionLost", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("second transient disconnect"))));
    QTRY_VERIFY_WITH_TIMEOUT(newMember.reconnecting(), 1000);
    QVariantMap secondReconnectRequest;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value("userCode").toString().compare(QStringLiteral("C001"), Qt::CaseInsensitive) == 0) {
                secondReconnectRequest = candidate;
                return true;
            }
        }
        return false;
    }(), 5000);
    host.sendAdminAction("approve_connection", secondReconnectRequest.value("userCode").toString(),
                         secondReconnectRequest.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(newMember.connected(), 12000);
    QTest::qWait(500);
    QCOMPARE(newMemberWelcomeSpy.count(), 2);
    QCOMPARE(newMemberMlsSpy.count(), mlsResultsBeforeReconnect);
    newMember.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-carol-reconnect"));
    QTRY_VERIFY_WITH_TIMEOUT(newMemberGroupSpy.count() > groupStatesBeforeReconnect, 5000);
    QCOMPARE(newMemberGroupSpy.back().at(3).toULongLong(), quint64(3));

    hostDataSpy.clear();
    newMemberDataSpy.clear();
    host.protectMls(QStringLiteral("group-e2e"), QByteArrayLiteral("after-reconnect"), QStringLiteral("protect-reconnect"));
    QTRY_VERIFY_WITH_TIMEOUT(hostDataSpy.count() > 0, 5000);
    QVERIFY(hostDataSpy.at(0).at(1).toBool());
    newMember.unprotectMls(QStringLiteral("group-e2e"), hostDataSpy.at(0).at(2).toByteArray(), QStringLiteral("unprotect-reconnect"));
    QTRY_VERIFY_WITH_TIMEOUT(newMemberDataSpy.count() > 0, 5000);
    QVERIFY(newMemberDataSpy.at(0).at(1).toBool());
    QCOMPARE(newMemberDataSpy.at(0).at(2).toByteArray(), QByteArrayLiteral("after-reconnect"));

    // Switching identity on the same worker must discard the old group
    // session; the new identity still publishes its own fresh key package.
    const int groupStatesBeforeIdentitySwitch = newMemberGroupSpy.count();
    newMember.connectToServerWithTlsName("127.0.0.1", 8888, "Dave", "D001", hostPaths.certFile, "localhost");
    QVariantMap identityRequest;
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        for (const QVariant& value : host.pendingConnectionApprovals()) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value("userCode").toString().compare(QStringLiteral("D001"), Qt::CaseInsensitive) == 0) {
                identityRequest = candidate;
                return true;
            }
        }
        return false;
    }(), 5000);
    host.sendAdminAction("approve_connection", identityRequest.value("userCode").toString(),
                         identityRequest.value("id").toString());
    QTRY_VERIFY_WITH_TIMEOUT(newMember.connected(), 12000);
    newMember.inspectMlsGroup(QStringLiteral("group-e2e"), QStringLiteral("state-after-identity-switch"));
    QTRY_VERIFY_WITH_TIMEOUT(newMemberGroupSpy.count() > groupStatesBeforeIdentitySwitch, 5000);
    QVERIFY(!newMemberGroupSpy.back().at(1).toBool());
    const int fetchD001Before = hostMlsSpy.count();
    const int keyPackageD001Before = hostKeyPackageSpy.count();
    host.fetchMlsKeyPackage(QStringLiteral("lobby"), QStringLiteral("D001"), QStringLiteral("fetch-dave"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > fetchD001Before, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(hostKeyPackageSpy.count() > keyPackageD001Before, 5000);
    QVERIFY2(hostMlsSpy.at(fetchD001Before).at(1).toBool(),
             qPrintable(QStringLiteral("fetch-dave failed: %1").arg(hostMlsSpy.at(fetchD001Before).at(3).toString())));
    const QString daveKeyPackageDigest = hostKeyPackageSpy.back().at(1).toString();
    QVERIFY(!daveKeyPackageDigest.isEmpty());
    QVERIFY(daveKeyPackageDigest != bobKeyPackageDigest);

    member.disconnectFromServer();
    newMember.disconnectFromServer();
    host.disconnectFromServer();
#endif
}

void ChatBridgeTests::usersResponseUsesBulkModelUpdates() {
    GuiChatController controller;
    QSignalSpy memberResetSpy(controller.memberModel(), &QAbstractItemModel::modelReset);
    QSignalSpy directResetSpy(controller.directMessageModel(), &QAbstractItemModel::modelReset);

    const QVariantList users = {
        QVariantMap{{"displayName", "Bob"}, {"userCode", "B001"}, {"room", "lobby"}},
        QVariantMap{{"displayName", "Carol"}, {"userCode", "C001"}, {"room", "lobby"}}
    };
    QVERIFY(QMetaObject::invokeMethod(
        &controller, "handleMessage", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("users_response")),
        Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
        Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
        Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
        Q_ARG(QStringList, QStringList()), Q_ARG(QVariantList, users),
        Q_ARG(QVariantList, QVariantList()), Q_ARG(bool, false)));

    QCOMPARE(memberResetSpy.count(), 1);
    QCOMPARE(directResetSpy.count(), 1);
    QCOMPARE(controller.memberModel()->rowCount(), 2);
    QCOMPARE(controller.directMessageModel()->rowCount(), 2);
}

void ChatBridgeTests::roomsResponseUsesBulkModelUpdates() {
    GuiChatController controller;
    QSignalSpy roomResetSpy(controller.roomModel(), &QAbstractItemModel::modelReset);

    const QVariantList rooms = {
        QVariantMap{{"roomName", "lobby"}, {"ownerCode", "A001"}, {"private", false}, {"canManage", true}},
        QVariantMap{{"roomName", "study"}, {"ownerCode", "B001"}, {"private", false}, {"canManage", false}}
    };
    QVERIFY(QMetaObject::invokeMethod(
        &controller, "handleMessage", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("rooms_response")),
        Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
        Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
        Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
        Q_ARG(QStringList, QStringList()), Q_ARG(QVariantList, QVariantList()),
        Q_ARG(QVariantList, rooms), Q_ARG(bool, false)));

    QCOMPARE(roomResetSpy.count(), 1);
    QCOMPARE(controller.roomModel()->rowCount(), 2);
    QCOMPARE(controller.roomModel()->valueAt(1, "roomName").toString(), QStringLiteral("study"));
}

void ChatBridgeTests::connectionApprovalStateIsExposedAndCleared() {
    GuiChatController controller;
    ChatBridge bridge(&controller);

    QVERIFY(QMetaObject::invokeMethod(
        &controller, "handleMessage", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("connection_approval_request")), Q_ARG(QString, QStringLiteral("42")),
        Q_ARG(QString, QString()), Q_ARG(QString, QStringLiteral("Cara")), Q_ARG(QString, QStringLiteral("C003")),
        Q_ARG(QString, QStringLiteral("2026-08-24T10:00:00Z")), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
        Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()), Q_ARG(QStringList, QStringList()),
        Q_ARG(QVariantList, QVariantList()), Q_ARG(QVariantList, QVariantList()), Q_ARG(bool, false)));

    QTRY_COMPARE(controller.pendingConnectionApprovals().size(), 1);
    QTRY_VERIFY(QJsonDocument::fromJson(bridge.currentStateJson().toUtf8()).object()
                    .value("connectionApprovals").toArray().size() == 1);
    const QJsonArray approvals = QJsonDocument::fromJson(bridge.currentStateJson().toUtf8()).object()
                                     .value("connectionApprovals").toArray();
    QCOMPARE(approvals.size(), 1);
    QCOMPARE(approvals.at(0).toObject().value("id").toString(), QStringLiteral("42"));
    QCOMPARE(approvals.at(0).toObject().value("displayName").toString(), QStringLiteral("Cara"));

    QVERIFY(QMetaObject::invokeMethod(
        &controller, "handleMessage", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("connection_approval_result")), Q_ARG(QString, QStringLiteral("42")),
        Q_ARG(QString, QString()), Q_ARG(QString, QStringLiteral("Cara")), Q_ARG(QString, QStringLiteral("C003")),
        Q_ARG(QString, QStringLiteral("approved")), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
        Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()), Q_ARG(QStringList, QStringList()),
        Q_ARG(QVariantList, QVariantList()), Q_ARG(QVariantList, QVariantList()), Q_ARG(bool, false)));

    QTRY_VERIFY(controller.pendingConnectionApprovals().isEmpty());
    QTRY_VERIFY(QJsonDocument::fromJson(bridge.currentStateJson().toUtf8()).object()
                    .value("connectionApprovals").toArray().isEmpty());
}

void ChatBridgeTests::successfulConnectionClearsPreviousError() {
    GuiChatController controller;
    ChatBridge bridge(&controller);

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleConnectionFailed",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("temporary"))));
    QTRY_VERIFY(bridge.currentStateJson().contains(QStringLiteral("connection_failed")));

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleConnected",
                                      Qt::DirectConnection, Q_ARG(bool, false)));
    QTRY_VERIFY(QJsonDocument::fromJson(bridge.currentStateJson().toUtf8())
                    .object().value("connection").toObject().value("phase").toString()
                    == QStringLiteral("connected"));
    const QJsonObject connection = QJsonDocument::fromJson(
        bridge.currentStateJson().toUtf8()).object().value("connection").toObject();
    QVERIFY(!connection.contains(QStringLiteral("lastError")));
}

void ChatBridgeTests::reconnectingSnapshotPreservesTimelineAndReportsRecovery() {
    GuiChatController controller;
    ChatBridge bridge(&controller);
    controller.messageModel()->append({{"messageId", "draft-context"}, {"displayName", "Alice"},
                                       {"userCode", "A001"}, {"content", "keep this timeline"},
                                       {"selfMessage", true}, {"systemMessage", false}});
    const int messageCount = controller.messageModel()->rowCount();

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleConnectionLost", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("temporary network loss"))));

    QTRY_COMPARE(QJsonDocument::fromJson(bridge.currentStateJson().toUtf8())
                     .object().value("connection").toObject().value("phase").toString(),
                 QStringLiteral("reconnecting"));
    QCOMPARE(controller.messageModel()->rowCount(), messageCount);
}

void ChatBridgeTests::serverRejectionMarksOriginalMessageFailedWithoutTimelineError() {
    GuiChatController controller;
    controller.messageModel()->append({{"messageId", "queued-chat"}, {"displayName", "Alice"},
                                       {"userCode", "A001"}, {"content", "pending"},
                                       {"selfMessage", true}, {"systemMessage", false},
                                       {"deliveryState", "queued"}});

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleMessage", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("error")), Q_ARG(QString, QStringLiteral("queued-chat")),
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("Rate limit exceeded; please slow down")),
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
                                      Q_ARG(QStringList, QStringList()), Q_ARG(QStringList, QStringList()),
                                      Q_ARG(QVariantList, QVariantList()), Q_ARG(QVariantList, QVariantList()),
                                      Q_ARG(bool, false)));

    QCOMPARE(controller.messageModel()->rowCount(), 1);
    QCOMPARE(controller.messageModel()->valueAt(0, "deliveryState").toString(), QStringLiteral("failed"));
    QVERIFY(!controller.messageModel()->valueAt(0, "systemMessage").toBool());
}

void ChatBridgeTests::searchHistoryMergesWithoutDiscardingRealtimeRows() {
    GuiChatController controller;
    controller.messageModel()->append({{"messageId", "live-message"}, {"displayName", "Alice"},
                                       {"userCode", "A001"}, {"content", "latest realtime"},
                                       {"selfMessage", true}, {"systemMessage", false},
                                       {"deliveryState", "delivered"}});
    controller.searchActiveHistory(QStringLiteral("match"));
    const QVariantList history = {
        QVariantMap{{"messageId", "history-message"}, {"displayName", "Bob"}, {"userCode", "B001"},
                    {"content", "matching historical message"}, {"createdAt", "2026-09-08T10:00:00Z"},
                    {"deliveryState", "sent"}}
    };

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleHistory", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("lobby")), Q_ARG(QString, QString()),
                                      Q_ARG(bool, false), Q_ARG(QVariantList, history), Q_ARG(bool, false),
                                      Q_ARG(QString, QStringLiteral("match"))));

    QCOMPARE(controller.messageModel()->rowCount(), 2);
    QCOMPARE(controller.messageModel()->valueAt(0, "messageId").toString(), QStringLiteral("history-message"));
    QCOMPARE(controller.messageModel()->valueAt(1, "messageId").toString(), QStringLiteral("live-message"));
}

void ChatBridgeTests::recallRejectsAnUnrelatedMessageBeforeReportingSuccess() {
    GuiChatController controller;
    controller.messageModel()->append({{"messageId", "peer-1"}, {"displayName", "Bob"},
                                       {"userCode", "B002"}, {"content", "peer"},
                                       {"selfMessage", false}, {"systemMessage", false}});
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral(
        R"({"id":"recall-peer","type":"message.recall","payload":{"messageId":"peer-1"}})"));

    QCOMPARE(resultSpy.count(), 1);
    const QJsonObject result = QJsonDocument::fromJson(resultSpy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(result.value("id").toString(), QStringLiteral("recall-peer"));
    QCOMPARE(result.value("ok").toBool(), false);
    QCOMPARE(result.value("error").toObject().value("code").toString(), QStringLiteral("permission_denied"));
}

void ChatBridgeTests::recallReportsServerAcceptanceOrRejectionInsteadOfDispatchSuccess() {
    GuiChatController controller;
    controller.messageModel()->append({{"messageId", "own-1"}, {"displayName", "Alice"},
                                       {"userCode", "A001"}, {"content", "own"},
                                       {"selfMessage", true}, {"systemMessage", false}});
    ChatBridge bridge(&controller);
    QSignalSpy resultSpy(&bridge, &ChatBridge::commandResult);

    bridge.dispatch(QStringLiteral(
        R"({"id":"recall-own","type":"message.recall","payload":{"messageId":"own-1"}})"));
    QCOMPARE(resultSpy.count(), 0);

    QVERIFY(QMetaObject::invokeMethod(&controller, "handleMessage", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("error")), Q_ARG(QString, QStringLiteral("own-1")),
                                      Q_ARG(QString, QStringLiteral("recall-own")),
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()),
                                      Q_ARG(QString, QStringLiteral("Recall denied")), Q_ARG(QString, QString()),
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
                                      Q_ARG(QStringList, QStringList()), Q_ARG(QVariantList, QVariantList()),
                                      Q_ARG(QVariantList, QVariantList()), Q_ARG(bool, false)));
    QCOMPARE(resultSpy.count(), 1);
    QJsonObject rejected = QJsonDocument::fromJson(resultSpy.at(0).at(0).toString().toUtf8()).object();
    QCOMPARE(rejected.value("ok").toBool(), false);
    QCOMPARE(rejected.value("error").toObject().value("message").toString(), QStringLiteral("Recall denied"));

    bridge.dispatch(QStringLiteral(
        R"({"id":"recall-own-success","type":"message.recall","payload":{"messageId":"own-1"}})"));
    QCOMPARE(resultSpy.count(), 1);
    QVERIFY(QMetaObject::invokeMethod(&controller, "handleMessage", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("message_recalled")), Q_ARG(QString, QStringLiteral("own-1")),
                                      Q_ARG(QString, QStringLiteral("recall-own-success")),
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()),
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
                                      Q_ARG(QStringList, QStringList()), Q_ARG(QVariantList, QVariantList()),
                                      Q_ARG(QVariantList, QVariantList()), Q_ARG(bool, false)));
    QCOMPARE(resultSpy.count(), 2);
    const QJsonObject accepted = QJsonDocument::fromJson(resultSpy.at(1).at(0).toString().toUtf8()).object();
    QCOMPARE(accepted.value("id").toString(), QStringLiteral("recall-own-success"));
    QCOMPARE(accepted.value("ok").toBool(), true);
}

QTEST_MAIN(ChatBridgeTests)

#include "chat_bridge_tests.moc"
