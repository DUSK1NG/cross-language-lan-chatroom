#include "attachments/attachment_transfer_state.hpp"

#include <QtTest>

using attachments::AttachmentTransferState;

class AttachmentTransferStateTests final : public QObject {
    Q_OBJECT

private slots:
    void initAndResumeTrackOnlyServerIndexes();
    void rejectsInvalidAckAndResumeIndexes();
    void completesAfterAllChunksAndSupportsCancel();
    void failsAfterRetryBudget();
};

void AttachmentTransferStateTests::initAndResumeTrackOnlyServerIndexes() {
    AttachmentTransferState state;
    QVERIFY(state.begin(100, 40));
    QCOMPARE(state.status(), AttachmentTransferState::Status::Initializing);
    QVERIFY(state.acceptInit("attachment-1", "upload-1", 40, "2030-01-01T00:00:00Z"));
    QVERIFY(state.registerChunkCommand("cmd-1", 1));
    QVERIFY(!state.acceptChunkAck("cmd-1", 2));
    QVERIFY(state.acceptChunkAck("cmd-1", 1));
    QVERIFY(state.acceptResume({0, 2}));
    QCOMPARE(state.missingIndexes(), std::vector<std::int64_t>({1}));
    QVERIFY(state.acceptChunkAck(1));
    QCOMPARE(state.status(), AttachmentTransferState::Status::Completed);
}

void AttachmentTransferStateTests::rejectsInvalidAckAndResumeIndexes() {
    AttachmentTransferState state;
    QVERIFY(state.begin(100, 40));
    QVERIFY(state.acceptInit("attachment-1", "upload-1", 40, "2030-01-01T00:00:00Z"));
    QVERIFY(!state.acceptResume({3}));
    QVERIFY(!state.received(3));
    QVERIFY(!state.acceptChunkAck(-1));
    QVERIFY(!state.acceptChunkAck(3));
}

void AttachmentTransferStateTests::completesAfterAllChunksAndSupportsCancel() {
    AttachmentTransferState state;
    QVERIFY(state.begin(1, 40));
    QVERIFY(state.acceptInit("attachment-1", "upload-1", 40, "2030-01-01T00:00:00Z"));
    state.cancel();
    QCOMPARE(state.status(), AttachmentTransferState::Status::Cancelled);
    QVERIFY(!state.acceptChunkAck(0));
}

void AttachmentTransferStateTests::failsAfterRetryBudget() {
    AttachmentTransferState state;
    QVERIFY(state.begin(40, 40));
    QVERIFY(state.acceptInit("attachment-1", "upload-1", 40, "2030-01-01T00:00:00Z"));
    for (int retry = 0; retry < AttachmentTransferState::MaxChunkRetries; ++retry) {
        QVERIFY(state.recordChunkFailure(0));
    }
    QVERIFY(!state.recordChunkFailure(0));
    QCOMPARE(state.status(), AttachmentTransferState::Status::Failed);
}

QTEST_MAIN(AttachmentTransferStateTests)
#include "attachment_transfer_state_tests.moc"
