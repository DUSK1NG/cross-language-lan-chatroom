#include "attachments/attachment_transfer_state.hpp"

#include <algorithm>

namespace attachments {

bool AttachmentTransferState::begin(const std::int64_t logicalSize, const std::int64_t chunkSize) {
    if (logicalSize <= 0 || chunkSize <= 0 || status_ != Status::Idle) return false;
    logicalSize_ = logicalSize;
    chunkSize_ = chunkSize;
    attachmentId_.clear();
    uploadId_.clear();
    expiresAt_.clear();
    received_.clear();
    failedAttempts_.clear();
    pendingChunkCommands_.clear();
    status_ = Status::Initializing;
    return true;
}

bool AttachmentTransferState::acceptInit(const std::string& attachmentId, const std::string& uploadId,
                                          const std::int64_t chunkSize, const std::string& expiresAt) {
    if (status_ != Status::Initializing || attachmentId.empty() || uploadId.empty() ||
        expiresAt.empty() || chunkSize != chunkSize_) {
        return false;
    }
    attachmentId_ = attachmentId;
    uploadId_ = uploadId;
    expiresAt_ = expiresAt;
    status_ = Status::Uploading;
    return true;
}

bool AttachmentTransferState::acceptResume(const std::vector<std::int64_t>& receivedIndexes) {
    if (status_ != Status::Uploading && status_ != Status::Resuming) return false;
    std::set<std::int64_t> validated;
    for (const auto index : receivedIndexes) {
        if (!validIndex(index)) return false;
        validated.insert(index);
    }
    received_ = std::move(validated);
    failedAttempts_.clear();
    pendingChunkCommands_.clear();
    status_ = received_.size() == static_cast<std::size_t>(chunkCount())
        ? Status::Completed : Status::Uploading;
    return true;
}

bool AttachmentTransferState::acceptChunkAck(const std::int64_t chunkIndex) {
    if (status_ != Status::Uploading || !validIndex(chunkIndex)) return false;
    received_.insert(chunkIndex);
    failedAttempts_.erase(chunkIndex);
    if (received_.size() == static_cast<std::size_t>(chunkCount())) status_ = Status::Completed;
    return true;
}

bool AttachmentTransferState::registerChunkCommand(const std::string& commandId,
                                                    const std::int64_t chunkIndex) {
    if (status_ != Status::Uploading || commandId.empty() || !validIndex(chunkIndex) || received(chunkIndex)) return false;
    if (pendingChunkCommands_.contains(commandId)) return false;
    pendingChunkCommands_.emplace(commandId, chunkIndex);
    return true;
}

bool AttachmentTransferState::acceptChunkAck(const std::string& commandId,
                                             const std::int64_t chunkIndex) {
    const auto it = pendingChunkCommands_.find(commandId);
    if (it == pendingChunkCommands_.end() || it->second != chunkIndex) return false;
    pendingChunkCommands_.erase(it);
    return acceptChunkAck(chunkIndex);
}

bool AttachmentTransferState::recordChunkFailure(const std::int64_t chunkIndex) {
    if (status_ != Status::Uploading || !validIndex(chunkIndex) || received(chunkIndex)) return false;
    const int attempts = ++failedAttempts_[chunkIndex];
    if (attempts > MaxChunkRetries) status_ = Status::Failed;
    return attempts <= MaxChunkRetries;
}

void AttachmentTransferState::cancel() {
    if (status_ == Status::Initializing || status_ == Status::Uploading || status_ == Status::Resuming) {
        status_ = Status::Cancelled;
    }
}

void AttachmentTransferState::fail() {
    if (status_ != Status::Completed && status_ != Status::Cancelled) status_ = Status::Failed;
}

std::int64_t AttachmentTransferState::chunkCount() const {
    return chunkSize_ > 0 ? (logicalSize_ + chunkSize_ - 1) / chunkSize_ : 0;
}

bool AttachmentTransferState::received(const std::int64_t chunkIndex) const {
    return received_.find(chunkIndex) != received_.end();
}

std::vector<std::int64_t> AttachmentTransferState::missingIndexes() const {
    std::vector<std::int64_t> result;
    for (std::int64_t index = 0; index < chunkCount(); ++index) {
        if (!received(index)) result.push_back(index);
    }
    return result;
}

bool AttachmentTransferState::validIndex(const std::int64_t chunkIndex) const {
    return chunkIndex >= 0 && chunkIndex < chunkCount();
}

}  // namespace attachments
