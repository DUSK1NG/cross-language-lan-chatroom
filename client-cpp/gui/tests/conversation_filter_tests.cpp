#include "chat_model.hpp"
#include "conversation_filter_model.hpp"

#include <QtTest>

class ConversationFilterTests final : public QObject {
    Q_OBJECT

private slots:
    void emptyQueryShowsAllRows();
    void queryMatchesConfiguredRolesCaseInsensitively();
    void queryCanMatchAnyConfiguredRole();
    void queryHidesRowsWithoutMatches();
};

void ConversationFilterTests::emptyQueryShowsAllRows() {
    ChatListModel source({QStringLiteral("roomName"), QStringLiteral("memberCount")});
    source.appendRows({{{"roomName", "lobby"}, {"memberCount", 2}},
                       {{"roomName", "study"}, {"memberCount", 3}}});
    ConversationFilterModel filter;
    filter.setSourceModel(&source);
    filter.setSearchRoles({QStringLiteral("roomName")});

    filter.setQuery(QString());

    QCOMPARE(filter.rowCount(), 2);
}

void ConversationFilterTests::queryMatchesConfiguredRolesCaseInsensitively() {
    ChatListModel source({QStringLiteral("roomName")});
    source.append({{"roomName", "Study Group"}});
    ConversationFilterModel filter;
    filter.setSourceModel(&source);
    filter.setSearchRoles({QStringLiteral("roomName")});

    filter.setQuery(QStringLiteral("study"));

    QCOMPARE(filter.rowCount(), 1);
}

void ConversationFilterTests::queryCanMatchAnyConfiguredRole() {
    ChatListModel source({QStringLiteral("displayName"), QStringLiteral("userCode")});
    source.append({{"displayName", "Bob"}, {"userCode", "B001"}});
    ConversationFilterModel filter;
    filter.setSourceModel(&source);
    filter.setSearchRoles({QStringLiteral("displayName"), QStringLiteral("userCode")});

    filter.setQuery(QStringLiteral("b001"));

    QCOMPARE(filter.rowCount(), 1);
}

void ConversationFilterTests::queryHidesRowsWithoutMatches() {
    ChatListModel source({QStringLiteral("roomName")});
    source.appendRows({{{"roomName", "lobby"}}, {{"roomName", "study"}}});
    ConversationFilterModel filter;
    filter.setSourceModel(&source);
    filter.setSearchRoles({QStringLiteral("roomName")});

    filter.setQuery(QStringLiteral("missing"));

    QCOMPARE(filter.rowCount(), 0);
}

QTEST_MAIN(ConversationFilterTests)

#include "conversation_filter_tests.moc"
