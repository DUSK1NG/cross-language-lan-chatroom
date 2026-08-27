#include "lan_chat_core.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include <atomic>
#include <thread>
#include <vector>

class LanChatCoreTests final : public QObject {
    Q_OBJECT

private slots:
    void createReturnsStateSnapshot();
    void invalidJsonReturnsValidationError();
    void eventStringCanBeFreedExactlyOnce();
    void eventQueueRetainsAtMost256Events();
    void errorEventSurvivesStateQueuePressure();
    void destroyStopsCoreWithinFiveSeconds();
    void destroyIsSafeWithConcurrentAbiCalls();
    void destroyRetiresHandleBeforeConcurrentCalls();
};

void LanChatCoreTests::createReturnsStateSnapshot() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    char* state = lan_chat_core_current_state_json(handle);
    QVERIFY(state != nullptr);
    QVERIFY(QByteArray(state).contains("schemaVersion"));

    lan_chat_core_free_string(state);
    lan_chat_core_destroy(handle);
}

void LanChatCoreTests::invalidJsonReturnsValidationError() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    QCOMPARE(lan_chat_core_dispatch_json(handle, "not-json"), 2);

    lan_chat_core_destroy(handle);
}

void LanChatCoreTests::eventStringCanBeFreedExactlyOnce() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    QCOMPARE(lan_chat_core_dispatch_json(
                 handle,
                 R"({"id":"select-lobby","type":"conversation.selectRoom","payload":{"room":"lobby"}})"),
             0);

    char* event = nullptr;
    for (int attempt = 0; attempt < 20 && !event; ++attempt) {
        event = lan_chat_core_take_event_json(handle);
        if (!event) {
            QTest::qWait(50);
        }
    }
    QVERIFY(event != nullptr);
    const auto document = QJsonDocument::fromJson(QByteArray(event));
    QCOMPARE(document.object().value("kind").toString(), QStringLiteral("result"));

    lan_chat_core_free_string(event);
    lan_chat_core_destroy(handle);
}

void LanChatCoreTests::eventQueueRetainsAtMost256Events() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    for (int index = 0; index < 260; ++index) {
        const QByteArray command = QByteArrayLiteral(
            R"({"id":"queue-%1","type":"conversation.selectRoom","payload":{"room":"lobby"}})")
                                       .replace("%1", QByteArray::number(index));
        QCOMPARE(lan_chat_core_dispatch_json(handle, command.constData()), 0);
    }

    int eventCount = 0;
    while (char* event = lan_chat_core_take_event_json(handle)) {
        ++eventCount;
        lan_chat_core_free_string(event);
    }
    QCOMPARE(eventCount, 256);

    lan_chat_core_destroy(handle);
}

void LanChatCoreTests::errorEventSurvivesStateQueuePressure() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    QCOMPARE(lan_chat_core_dispatch_json(
                 handle,
                 R"({"id":"missing-host","type":"session.connectLocalHost","payload":{"serverExe":"C:/missing/lan-chat-server.exe","certFile":"C:/missing/server.crt","keyFile":"C:/missing/server.key","dbFile":"C:/missing/chat.db","username":"Alice","userCode":"A001"}})"),
             0);

    bool bridgeReportsConnectionFailure = false;
    QElapsedTimer waitForError;
    waitForError.start();
    while (!bridgeReportsConnectionFailure && waitForError.elapsed() < 2000) {
        if (char* state = lan_chat_core_current_state_json(handle)) {
            const QJsonObject connection = QJsonDocument::fromJson(QByteArray(state)).object()
                                               .value("connection").toObject();
            bridgeReportsConnectionFailure =
                connection.value("phase").toString() == QStringLiteral("error") &&
                connection.value("lastError").toObject().value("code").toString() ==
                    QStringLiteral("connection_failed");
            lan_chat_core_free_string(state);
        }
        if (!bridgeReportsConnectionFailure) QTest::qWait(20);
    }
    QVERIFY(bridgeReportsConnectionFailure);

    // State polling does not consume the error or state event needed to exercise the full queue.
    for (int index = 0; index < 255; ++index) {
        const QByteArray command = QByteArrayLiteral(
            R"({"id":"pressure-%1","type":"conversation.selectRoom","payload":{"room":"lobby"}})")
                                       .replace("%1", QByteArray::number(index));
        QCOMPARE(lan_chat_core_dispatch_json(handle, command.constData()), 0);
    }

    bool foundConnectionError = false;
    while (char* event = lan_chat_core_take_event_json(handle)) {
        const QJsonObject envelope = QJsonDocument::fromJson(QByteArray(event)).object();
        if (envelope.value("kind").toString() == QStringLiteral("error")) {
            const QJsonObject payload = envelope.value("payload").toObject();
            foundConnectionError = payload.value("code").toString() == QStringLiteral("connection_failed");
        }
        lan_chat_core_free_string(event);
    }
    QVERIFY(foundConnectionError);

    lan_chat_core_destroy(handle);
}

void LanChatCoreTests::destroyStopsCoreWithinFiveSeconds() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    QElapsedTimer timer;
    timer.start();
    lan_chat_core_destroy(handle);
    QVERIFY2(timer.elapsed() <= 5000, "Core shutdown exceeded five seconds");
}

void LanChatCoreTests::destroyIsSafeWithConcurrentAbiCalls() {
    auto* handle = lan_chat_core_create();
    QVERIFY(handle != nullptr);

    constexpr int workerCount = 12;
    constexpr int callsPerWorker = 64;
    std::atomic_int ready = 0;
    std::atomic_bool start = false;
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&]() {
            ready.fetch_add(1);
            while (!start.load()) {
                std::this_thread::yield();
            }
            for (int call = 0; call < callsPerWorker; ++call) {
                char* state = lan_chat_core_current_state_json(handle);
                lan_chat_core_free_string(state);
            }
        });
    }

    QTRY_COMPARE(ready.load(), workerCount);
    start.store(true);
    lan_chat_core_destroy(handle);

    for (auto& worker : workers) {
        worker.join();
    }
}

void LanChatCoreTests::destroyRetiresHandleBeforeConcurrentCalls() {
    auto* staleHandle = lan_chat_core_create();
    QVERIFY(staleHandle != nullptr);
    lan_chat_core_destroy(staleHandle);

    auto* liveHandle = lan_chat_core_create();
    QVERIFY(liveHandle != nullptr);

    QCOMPARE(lan_chat_core_dispatch_json(
                 staleHandle,
                 R"({"id":"stale","type":"conversation.selectRoom","payload":{"room":"lobby"}})"),
             1);

    char* staleState = reinterpret_cast<char*>(1);
    std::thread caller([&]() {
        staleState = lan_chat_core_current_state_json(staleHandle);
    });
    caller.join();

    QVERIFY(staleState == nullptr);
    lan_chat_core_destroy(liveHandle);
}

QTEST_GUILESS_MAIN(LanChatCoreTests)
#include "lan_chat_core_tests.moc"
