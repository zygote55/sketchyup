#pragma once
#include "core/model.hpp"
#include <compare>
#include <set>
namespace sketchy {
enum class SelectionKind { Body, Face, Edge, Guide };
struct SelectedEntity {
    Id body{};
    SelectionKind kind{SelectionKind::Body};
    Id entity{}; // Body selections use zero; other IDs are scoped and typed.
    auto operator<=>(const SelectedEntity &) const = default;
};
using SelectionSet = std::set<SelectedEntity>;
enum class SelectionMode { Replace, Add, Toggle };
// Ephemeral editor state. Selection, temporary hiding/locking and the current
// editing context do not change document bytes, history or dirty state.
class Selection {
  public:
    void sync(const Document &doc);
    bool belongsTo(const Document &doc) const { return doc.owns(session_); }
    const SelectionSet &entities() const { return entities_; }
    const SelectionSet &hiddenEntities() const { return hidden_; }
    const std::set<Id> &lockedBodies() const { return locked_; }
    Id context() const { return context_; }
    bool showingHidden() const { return showHidden_; }
    bool exists(const Document &doc, SelectedEntity entity) const;
    bool hidden(const Document &doc, SelectedEntity entity) const;
    bool locked(const Document &doc, Id body) const;
    bool inContext(const Document &doc, Id body) const;
    bool selectable(const Document &doc, SelectedEntity entity) const;
    std::optional<SelectedEntity> pickTarget(const Document &doc, SelectedEntity entity) const;
    bool inActiveHierarchy(const Document &doc, Id body) const;
    bool apply(const Document &doc, const SelectionSet &entities, SelectionMode mode);
    void clear() { entities_.clear(); }
    void enter(const Document &doc, Id context);
    void showHidden(const Document &doc, bool show);
    void hide(const Document &doc, const SelectionSet &entities);
    void reveal(const Document &doc);
    void lock(const Document &doc, Id body, bool locked);
    void unlockAll(const Document &doc);
    SelectionSet boundary(const Document &doc, SelectedEntity entity) const;
    SelectionSet connected(const Document &doc, SelectedEntity entity) const;
    std::vector<SelectedEntity> ordered(const Document &doc) const;
    std::string summary() const;

  private:
    SelectionSet entities_, hidden_;
    std::set<Id> locked_;
    Id context_{};
    bool showHidden_{};
    Document::SaveStamp session_;
    mutable Document::SaveStamp lockCacheStamp_;
    mutable std::set<Id> persistentLockedAncestors_;
    void prune(const Document &doc);
};
// Erases the eligible typed selection atomically as one document edit.
ChangeReport eraseSelected(Document &doc, Selection &selection);
} // namespace sketchy
