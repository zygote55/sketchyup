#include "core/model.hpp"
namespace sketchy {
HistoryPage Document::history(size_t offset, size_t limit) const {
    const auto total = undo_.size() + redo_.size();
    if (limit < 1 || limit > 1000 || offset > total)
        throw std::runtime_error("History page is outside its supported bounds");
    const auto base = !undo_.empty()   ? undo_.front().before
                      : !redo_.empty() ? redo_.back().before
                                       : state_;
    HistoryPage result{total,          undo_.size(),        offset, historyBytes_,
                       historyPruned_, base == savedState_, {}};
    const auto end = offset + std::min(limit, total - offset);
    for (auto i = offset; i < end; ++i) {
        const auto &entry =
            i < undo_.size() ? undo_[i] : redo_[redo_.size() - 1 - (i - undo_.size())];
        result.entries.push_back({i + 1, entry.edit.label, entry.edit.metadata, i < undo_.size(),
                                  entry.after == savedState_});
    }
    return result;
}
void Document::navigateHistory(size_t position, std::uint64_t expectedRevision) {
    if (expectedRevision != revision_)
        throw std::runtime_error("STALE_REVISION: inspect history before navigating");
    if (position > undo_.size() + redo_.size())
        throw std::runtime_error("History position is no longer available");
    const auto steps = position > undo_.size() ? position - undo_.size() : undo_.size() - position;
    if (steps > UINT64_MAX - revision_)
        throw std::runtime_error("Document revision space exhausted");
    if (!steps)
        return;
    // Records are shared immutable values. Stage the cursor/maps privately so failed navigation
    // cannot publish only a prefix of the requested undo/redo steps.
    Document candidate = *this;
    while (candidate.undo_.size() > position)
        candidate.undo();
    while (candidate.undo_.size() < position)
        candidate.redo();
    *this = std::move(candidate);
}
} // namespace sketchy
