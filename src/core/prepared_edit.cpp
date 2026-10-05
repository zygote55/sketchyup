#include "core/model.hpp"
namespace sketchy {
size_t Document::PreparedEdit::retainedBytes() const {
    return sizeof(PreparedEdit) + edit_.bytes + snapshot_->readSnapshotBytes();
}
Document::PreparedEdit
Document::prepareEdit(const std::function<void(Document &)> &operation) const {
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    auto candidate = readSnapshot();
    operation(candidate);
    if (candidate.session_ != session_ || candidate.identity_ != identity_ ||
        candidate.revision_ != revision_ + 1 || candidate.savedState_ != savedState_ ||
        candidate.undo_.size() != 1 || !candidate.redo_.empty() ||
        candidate.undo_.front().before != state_ ||
        candidate.undo_.front().after != candidate.state_)
        throw std::runtime_error("A prepared operation must produce exactly one composed edit");
    PreparedEdit result;
    result.base_ = saveStamp();
    result.revision_ = revision_;
    result.edit_ = candidate.undo_.front().edit;
    result.snapshot_ = std::make_shared<const Document>(candidate.readSnapshot());
    return result;
}
bool Document::canApply(const PreparedEdit &prepared) const {
    return prepared.snapshot_ && revision_ == prepared.revision_ &&
           isCurrentSnapshot(prepared.base_);
}
ChangeReport Document::applyPrepared(const PreparedEdit &prepared) {
    if (!canApply(prepared))
        throw std::runtime_error("STALE_PROPOSAL: document changed after staging");
    return apply(prepared.edit_, prepared.revision_);
}
} // namespace sketchy
