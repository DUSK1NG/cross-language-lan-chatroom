#include "chat_bridge.hpp"
#include "gui_chat_controller.hpp"
#include "gui_connection_worker.hpp"
#include "graphics_info.hpp"
#include "host_path_resolver.hpp"
#include "performance_profile.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTcpSocket>
#include <QThread>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <winsock2.h>

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
    host.fetchMlsKeyPackage(QStringLiteral("lobby"), QStringLiteral("B001"), QStringLiteral("fetch-bob"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 0, 5000);
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
    QVERIFY(hostMlsSpy.at(2).at(1).toBool());
    host.addMlsMember(QStringLiteral("lobby"), QStringLiteral("group-e2e"),
                      QStringLiteral("C001"), QStringLiteral("add-carol"));
    QTRY_VERIFY_WITH_TIMEOUT(hostMlsSpy.count() > 3, 8000);
    QCOMPARE(hostMlsSpy.at(3).at(0).toString(), QStringLiteral("add-carol"));
    QVERIFY(hostMlsSpy.at(3).at(1).toBool());
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
