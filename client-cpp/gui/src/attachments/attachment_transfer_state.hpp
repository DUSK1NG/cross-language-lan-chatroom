#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace attachments {

class AttachmentTransferState final {
public:
    enum class Status { Idle, Initializing, Uploading, Resuming, Completed, Cancelled, Failed };
    static constexpr int MaxChunkRetries = 3;

    bool begin(std::int64_t logicalSize, std::int64_t chunkSize);
    bool acceptInit(const std::string& attachmentId, const std::string& uploadId,
                    std::int64_t chunkSize, const std::string& expiresAt);
    bool acceptResume(const std::vector<std::int64_t>& receivedIndexes);
    bool acceptChunkAck(std::int64_t chunkIndex);
    bool registerChunkCommand(const std::string& commandId, std::int64_t chunkIndex);
    bool acceptChunkAck(const std::string& commandId, std::int64_t chunkIndex);
    bool recordChunkFailure(std::int64_t chunkIndex);
    void cancel();
    void fail();

    Status status() const { return status_; }
    std::int64_t logicalSize() const { return logicalSize_; }
    std::int64_t chunkSize() const { return chunkSize_; }
    std::int64_t chunkCount() const;
    std::int64_t receivedCount() const { return static_cast<std::int64_t>(received_.size()); }
    const std::string& attachmentId() const { return attachmentId_; }
    const std::string& uploadId() const { return uploadId_; }
    const std::string& expiresAt() const { return expiresAt_; }
    bool received(std::int64_t chunkIndex) const;
    std::vector<std::int64_t> missingIndexes() const;

private:
    bool validIndex(std::int64_t chunkIndex) const;

    Status status_ = Status::Idle;
    std::int64_t logicalSize_ = 0;
    std::int64_t chunkSize_ = 0;
    std::string attachmentId_;
    std::string uploadId_;
    std::string expiresAt_;
    std::set<std::int64_t> received_;
    std::map<std::int64_t, int> failedAttempts_;
    std::map<std::string, std::int64_t> pendingChunkCommands_;
};

}  // namespace attachments
