#include "chat_bridge.hpp"
#include "gui_chat_controller.hpp"
#include "gui_connection_worker.hpp"
#include "graphics_info.hpp"
#include "host_path_resolver.hpp"
#include "performance_profile.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <winsock2.h>

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

bool sendRawTlsFrame(QSslSocket& socket, const QJsonObject& object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (payload.isEmpty() || payload.size() > 64 * 1024) return false;
    QByteArray frame(4, Qt::Uninitialized);
    const auto size = static_cast<quint32>(payload.size());
    frame[0] = static_cast<char>((size >> 24) & 0xff);
    frame[1] = static_cast<char>((size >> 16) & 0xff);
    frame[2] = static_cast<char>((size >> 8) & 0xff);
    frame[3] = static_cast<char>(size & 0xff);
    frame.append(payload);
    return socket.write(frame) == frame.size() && socket.waitForBytesWritten(2000);
}

bool readRawTlsFrame(QSslSocket& socket, QJsonObject* object, int timeoutMs) {
    std::array<char, 4> header{};
    int offset = 0;
    while (offset < static_cast<int>(header.size())) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeoutMs)) return false;
        const qint64 read = socket.read(header.data() + offset, header.size() - offset);
        if (read <= 0) return false;
        offset += static_cast<int>(read);
    }
    const auto size = (static_cast<quint32>(static_cast<unsigned char>(header[0])) << 24) |
                      (static_cast<quint32>(static_cast<unsigned char>(header[1])) << 16) |
                      (static_cast<quint32>(static_cast<unsigned char>(header[2])) << 8) |
                      static_cast<quint32>(static_cast<unsigned char>(header[3]));
    if (size == 0 || size > 64 * 1024) return false;
    QByteArray payload;
    payload.reserve(static_cast<int>(size));
    while (payload.size() < static_cast<int>(size)) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(timeoutMs)) return false;
        const QByteArray chunk = socket.read(static_cast<qint64>(size) - payload.size());
        if (chunk.isEmpty()) return false;
        payload.append(chunk);
    }
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) return false;
    *object = document.object();
    return true;
}

bool receiveRawTlsType(QSslSocket& socket, const QString& type, QJsonObject* object, int timeoutMs) {
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
}  // namespace

class ChatBridgeTests final : public QObject {
    Q_OBJECT

private slots:
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

void ChatBridgeTests::serverConnectionCompletesWithoutMessageLifetimeCorruption() {
    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available() || !QFileInfo::exists(hostPaths.certFile)) {
        QSKIP("TLS integration certificate is not available in this checkout");
    }

    GuiChatController controller;
    QSignalSpy connectedSpy(&controller, &GuiChatController::connectedChanged);
    QSignalSpy failedSpy(&controller, &GuiChatController::connectionFailed);
    controller.connectToServer("127.0.0.1", 8888, "Alice", "A001", hostPaths.certFile);

    QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() > 0 || failedSpy.count() > 0, 10000);
    QVERIFY2(connectedSpy.count() > 0, "local Host connection did not succeed");
    controller.disconnectFromServer();
    QTest::qWait(100);
}

void ChatBridgeTests::localHostConnectionCompletesWithoutMessageLifetimeCorruption() {
    const HostPathResolver::HostPaths hostPaths =
        HostPathResolver::resolveHostPaths(QCoreApplication::applicationDirPath());
    if (!hostPaths.available() || !QFileInfo::exists(hostPaths.dbFile)) {
        QSKIP("Local Host integration files are not available in this checkout");
    }

    WinsockScope winsock;
    QVERIFY2(winsock.result() == 0, "WSAStartup failed");
    GuiChatController controller;
    QSignalSpy connectedSpy(&controller, &GuiChatController::connectedChanged);
    QSignalSpy failedSpy(&controller, &GuiChatController::connectionFailed);
    controller.connectToLocalHost(hostPaths.serverExe, hostPaths.certFile, hostPaths.keyFile,
                                  hostPaths.dbFile, "Alice", "A001");

    QTRY_VERIFY_WITH_TIMEOUT(connectedSpy.count() > 0 || failedSpy.count() > 0, 12000);
    QVERIFY2(connectedSpy.count() > 0, "local Host connection did not succeed");
    controller.disconnectFromServer();
    QTest::qWait(100);
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
    auto expectRawError = [&](QSslSocket& socket, const QJsonObject& frame,
                              const QString& commandId, const QString& content) {
        if (!sendRawTlsFrame(socket, frame)) return false;
        QJsonObject response;
        if (!receiveRawTlsType(socket, QStringLiteral("error"), &response, 5000)) return false;
        return response.value(QStringLiteral("command_id")).toString() == commandId &&
               response.value(QStringLiteral("content")).toString() == content;
    };
    auto expectRawAck = [&](QSslSocket& socket, const QJsonObject& frame,
                            const QString& type, const QString& commandId,
                            const QString& content) {
        if (!sendRawTlsFrame(socket, frame)) return false;
        QJsonObject response;
        if (!receiveRawTlsType(socket, type, &response, 5000)) return false;
        if (response.value(QStringLiteral("command_id")).toString() != commandId) return false;
        if (!content.isEmpty() && response.value(QStringLiteral("content")).toString() != content) return false;
        return true;
    };

    QSslSocket mallory;
    mallory.setPeerVerifyMode(QSslSocket::VerifyNone);
    mallory.connectToHostEncrypted(QStringLiteral("127.0.0.1"), 8888);
    QVERIFY2(mallory.waitForEncrypted(10000), qPrintable(mallory.errorString()));
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
    mallory.disconnectFromHost();

    QSslSocket rawBob;
    rawBob.setPeerVerifyMode(QSslSocket::VerifyNone);
    rawBob.connectToHostEncrypted(QStringLiteral("127.0.0.1"), 8888);
    QVERIFY2(rawBob.waitForEncrypted(10000), qPrintable(rawBob.errorString()));
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
    rawBob.disconnectFromHost();

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
    host.addMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                      QStringLiteral("B001"), QStringLiteral("add-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 1, 8000);
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
    qputenv("LAN_CHAT_TEST_DROP_MLS_COMMIT_ONCE", "1");

    host.addMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                      QStringLiteral("C001"), QStringLiteral("add-carol"));
    QTRY_VERIFY_WITH_TIMEOUT(host.reconnecting(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(host.connected(), 12000);
    qunsetenv("LAN_CHAT_TEST_DROP_MLS_COMMIT_ONCE");
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
