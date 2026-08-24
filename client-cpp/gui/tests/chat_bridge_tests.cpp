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
    void snapshotContainsRoomAndMemberRoles();
    void dispatchSelectRoomCallsController();
    void invalidJsonEmitsCommandResultWithoutCallingController();
    void passwordDoesNotAppearInSnapshotOrResult();
    void modelChangesAreCoalescedIntoOneStateChangedSignal();
    void burstModelChangesDoNotPushStateAtFrameRate();
    void stateSamplingReportsDirtyModelCount();
    void workerDisconnectRunsOnWorkerThread();
    void serverConnectionCompletesWithoutMessageLifetimeCorruption();
    void localHostConnectionCompletesWithoutMessageLifetimeCorruption();
    void approvedLanMemberConnectionCompletesAfterLoginPending();
    void usersResponseUsesBulkModelUpdates();
    void roomsResponseUsesBulkModelUpdates();
    void connectionApprovalStateIsExposedAndCleared();
    void successfulConnectionClearsPreviousError();
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

    workerThread.quit();
    QVERIFY(workerThread.wait(1000));
    delete worker;
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
        Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
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
        Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
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
        Q_ARG(QStringList, QStringList()), Q_ARG(QStringList, QStringList()),
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
        Q_ARG(QStringList, QStringList()), Q_ARG(QStringList, QStringList()),
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
                                      Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
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
                                      Q_ARG(QString, QString()), Q_ARG(QString, QString()), Q_ARG(QStringList, QStringList()),
                                      Q_ARG(QStringList, QStringList()), Q_ARG(QVariantList, QVariantList()),
                                      Q_ARG(QVariantList, QVariantList()), Q_ARG(bool, false)));
    QCOMPARE(resultSpy.count(), 2);
    const QJsonObject accepted = QJsonDocument::fromJson(resultSpy.at(1).at(0).toString().toUtf8()).object();
    QCOMPARE(accepted.value("id").toString(), QStringLiteral("recall-own-success"));
    QCOMPARE(accepted.value("ok").toBool(), true);
}

QTEST_MAIN(ChatBridgeTests)

#include "chat_bridge_tests.moc"
