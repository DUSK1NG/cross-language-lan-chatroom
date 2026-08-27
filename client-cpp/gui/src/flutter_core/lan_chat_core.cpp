#include "lan_chat_core.h"

#include "chat_bridge.hpp"
#include "gui_chat_controller.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMutex>
#include <QQueue>
#include <QStringConverter>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace {
constexpr int kSuccess = 0;
constexpr int kInvalidHandle = 1;
constexpr int kInvalidJson = 2;
constexpr int kClosing = 3;
constexpr int kMaxQueuedEvents = 256;

struct QueuedEvent {
    QByteArray kind;
    QByteArray serializedJson;
};

struct CoreSession {
    QThread thread;
    QObject* executor = nullptr;
    GuiChatController* controller = nullptr;
    ChatBridge* bridge = nullptr;
    QMutex apiMutex;
    QMutex eventsMutex;
    QQueue<QueuedEvent> events;
    std::atomic_bool closing = false;
};

char* copyUtf8(const QByteArray& value) {
    auto* result = static_cast<char*>(std::malloc(static_cast<size_t>(value.size()) + 1));
    if (!result) {
        return nullptr;
    }
    std::memcpy(result, value.constData(), static_cast<size_t>(value.size()));
    result[value.size()] = '\0';
    return result;
}

bool ensureQtApplication() {
    if (QCoreApplication::instance()) {
        return true;
    }

    static QMutex applicationMutex;
    QMutexLocker lock(&applicationMutex);
    if (QCoreApplication::instance()) {
        return true;
    }

    static int argumentCount = 1;
    static char applicationName[] = "lan_chat_core";
    static char* arguments[] = {applicationName, nullptr};
    // The core can be loaded from Flutter's Win32 runner, which does not create a Qt
    // application. Qt needs this process-lifetime object before its worker threads
    // can receive queued invocations. It intentionally outlives all CoreSessions.
    static QCoreApplication* ownedApplication =
        new QCoreApplication(argumentCount, arguments);
    return ownedApplication == QCoreApplication::instance();
}

bool isJsonObjectUtf8(const char* input, QString* json) {
    if (!input) {
        return false;
    }

    const QByteArray bytes(input);
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        return false;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    *json = decoded;
    return true;
}

void enqueueEvent(CoreSession* session, const char* kind, const QString& payload) {
    const QByteArray eventKind(kind);
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    const QJsonValue value = error.error == QJsonParseError::NoError && document.isObject()
        ? QJsonValue(document.object())
        : QJsonValue(payload);
    const QByteArray event = QJsonDocument(QJsonObject{
        {QStringLiteral("kind"), QString::fromLatin1(kind)},
        {QStringLiteral("payload"), value},
    }).toJson(QJsonDocument::Compact);

    QMutexLocker lock(&session->eventsMutex);
    if (session->closing.load()) {
        return;
    }
    if (session->events.size() >= kMaxQueuedEvents) {
        const auto state = std::find_if(session->events.cbegin(), session->events.cend(),
                                        [](const QueuedEvent& queued) {
                                            return queued.kind == "state";
                                        });
        if (state == session->events.cend()) {
            return;
        }
        session->events.erase(state);
    }
    session->events.enqueue({eventKind, event});
}

template <typename Function>
bool invokeOnCoreThread(CoreSession* session, Function&& function) {
    if (!session->executor || !session->thread.isRunning()) {
        return false;
    }
    if (QThread::currentThread() == &session->thread) {
        std::forward<Function>(function)();
        return true;
    }
    return QMetaObject::invokeMethod(session->executor, std::forward<Function>(function),
                                     Qt::BlockingQueuedConnection);
}

void destroySession(CoreSession* session) {
    session->closing.store(true);
    invokeOnCoreThread(session, [session]() {
        delete session->bridge;
        session->bridge = nullptr;
        delete session->controller;
        session->controller = nullptr;
    });
    session->thread.quit();
    session->thread.wait();
}
}  // namespace

extern "C" LanChatCoreHandle lan_chat_core_create(void) {
    try {
        if (!ensureQtApplication()) {
            return nullptr;
        }
        auto session = std::make_unique<CoreSession>();
        session->thread.start();
        session->executor = new QObject;
        session->executor->moveToThread(&session->thread);
        QObject::connect(&session->thread, &QThread::finished, session->executor,
                         &QObject::deleteLater);

        const bool created = invokeOnCoreThread(session.get(), [&session]() {
            session->controller = new GuiChatController;
            session->bridge = new ChatBridge(session->controller);
            QObject::connect(session->bridge, &ChatBridge::stateChanged, session->bridge,
                             [core = session.get()](const QString& json) {
                                 enqueueEvent(core, "state", json);
                             });
            QObject::connect(session->bridge, &ChatBridge::commandResult, session->bridge,
                             [core = session.get()](const QString& json) {
                                 enqueueEvent(core, "result", json);
                             });
            QObject::connect(session->bridge, &ChatBridge::bridgeError, session->bridge,
                             [core = session.get()](const QString& json) {
                                 enqueueEvent(core, "error", json);
                             });
        });
        if (!created || !session->controller || !session->bridge) {
            destroySession(session.get());
            return nullptr;
        }
        return session.release();
    } catch (...) {
        return nullptr;
    }
}

extern "C" void lan_chat_core_destroy(const LanChatCoreHandle handle) {
    if (!handle) {
        return;
    }
    try {
        auto* session = static_cast<CoreSession*>(handle);
        QMutexLocker lock(&session->apiMutex);
        destroySession(session);
        lock.unlock();
        delete session;
    } catch (...) {
        // C ABI exports must never leak C++ exceptions.
    }
}

extern "C" int lan_chat_core_dispatch_json(const LanChatCoreHandle handle,
                                             const char* command_json) {
    if (!handle) {
        return kInvalidHandle;
    }
    try {
        auto* session = static_cast<CoreSession*>(handle);
        QMutexLocker lock(&session->apiMutex);
        if (session->closing.load()) {
            return kClosing;
        }

        QString command;
        if (!isJsonObjectUtf8(command_json, &command)) {
            return kInvalidJson;
        }
        return invokeOnCoreThread(session, [session, command]() {
            session->bridge->dispatch(command);
        }) ? kSuccess : kClosing;
    } catch (...) {
        return kClosing;
    }
}

extern "C" char* lan_chat_core_current_state_json(const LanChatCoreHandle handle) {
    if (!handle) {
        return nullptr;
    }
    try {
        auto* session = static_cast<CoreSession*>(handle);
        QMutexLocker lock(&session->apiMutex);
        if (session->closing.load()) {
            return nullptr;
        }

        QString state;
        if (!invokeOnCoreThread(session, [session, &state]() {
                state = session->bridge->currentStateJson();
            })) {
            return nullptr;
        }
        return copyUtf8(state.toUtf8());
    } catch (...) {
        return nullptr;
    }
}

extern "C" char* lan_chat_core_take_event_json(const LanChatCoreHandle handle) {
    if (!handle) {
        return nullptr;
    }
    try {
        auto* session = static_cast<CoreSession*>(handle);
        QMutexLocker apiLock(&session->apiMutex);
        if (session->closing.load()) {
            return nullptr;
        }
        QMutexLocker eventsLock(&session->eventsMutex);
        if (session->events.isEmpty()) {
            return nullptr;
        }
        return copyUtf8(session->events.dequeue().serializedJson);
    } catch (...) {
        return nullptr;
    }
}

extern "C" void lan_chat_core_free_string(char* value) {
    std::free(value);
}
