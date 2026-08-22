#include "chat_model.hpp"

#include <QSignalSpy>
#include <QtTest>

class ChatModelTests final : public QObject {
    Q_OBJECT

private slots:
    void appendRowsEmitsOneInsertRange();
    void replaceRowsEmitsOneReset();
    void updateRowsEmitsOneDataChangedRange();
    void appendRowsKeepsNewestRowsAtTheModelLimit();
    void prependRowsInsertsOlderRowsBeforeCurrentRows();
    void largeMessageFixturesStayBoundedAtTheModelLimit();
};

void ChatModelTests::appendRowsEmitsOneInsertRange() {
    ChatListModel model({QStringLiteral("id"), QStringLiteral("content")});
    QSignalSpy rowsInsertedSpy(&model, &QAbstractItemModel::rowsInserted);

    model.appendRows({{{"id", "one"}, {"content", "first"}},
                      {{"id", "two"}, {"content", "second"}}});

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(rowsInsertedSpy.count(), 1);
    QCOMPARE(model.valueAt(1, "content").toString(), QStringLiteral("second"));
}

void ChatModelTests::replaceRowsEmitsOneReset() {
    ChatListModel model({QStringLiteral("id")});
    model.append({{"id", "old"}});
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    model.replaceRows({{{"id", "one"}}, {{"id", "two"}}, {{"id", "three"}}});

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(model.valueAt(0, "id").toString(), QStringLiteral("one"));
}

void ChatModelTests::updateRowsEmitsOneDataChangedRange() {
    ChatListModel model({QStringLiteral("id"), QStringLiteral("count")});
    model.appendRows({{{"id", "one"}, {"count", 0}},
                      {{"id", "two"}, {"count", 0}}});
    QSignalSpy dataChangedSpy(&model, &QAbstractItemModel::dataChanged);

    model.updateRows({{{"count", 1}}, {{"count", 2}}});

    QCOMPARE(dataChangedSpy.count(), 1);
    QCOMPARE(model.valueAt(0, "count").toInt(), 1);
    QCOMPARE(model.valueAt(1, "count").toInt(), 2);
}

void ChatModelTests::appendRowsKeepsNewestRowsAtTheModelLimit() {
    ChatListModel model({QStringLiteral("id")});
    QList<QVariantMap> rows;
    for (int i = 0; i < 1005; ++i) {
        rows.append({{"id", i}});
    }

    model.appendRows(rows);

    QCOMPARE(model.rowCount(), 1000);
    QCOMPARE(model.valueAt(0, "id").toInt(), 5);
    QCOMPARE(model.valueAt(999, "id").toInt(), 1004);
}

void ChatModelTests::prependRowsInsertsOlderRowsBeforeCurrentRows() {
    ChatListModel model({QStringLiteral("id")});
    model.appendRows({{{"id", 1}}, {{"id", 2}}, {{"id", 3}}});

    model.prependRows({{{"id", -1}}, {{"id", 0}}});

    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.valueAt(0, "id").toInt(), -1);
    QCOMPARE(model.valueAt(1, "id").toInt(), 0);
    QCOMPARE(model.valueAt(2, "id").toInt(), 1);
    QCOMPARE(model.valueAt(4, "id").toInt(), 3);
}

void ChatModelTests::largeMessageFixturesStayBoundedAtTheModelLimit() {
    for (const int count : {100, 1000, 10000, 50000}) {
        ChatListModel model({QStringLiteral("id")});
        QList<QVariantMap> rows;
        rows.reserve(count);
        for (int i = 0; i < count; ++i) {
            rows.append({{"id", i}});
        }

        model.replaceRows(rows);

        QCOMPARE(model.rowCount(), qMin(count, 1000));
        QCOMPARE(model.valueAt(0, "id").toInt(), qMax(0, count - 1000));
        QCOMPARE(model.valueAt(model.rowCount() - 1, "id").toInt(), count - 1);
    }
}

QTEST_MAIN(ChatModelTests)

#include "chat_model_tests.moc"
