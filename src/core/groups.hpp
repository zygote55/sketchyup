#pragma once
#include "core/model.hpp"
#include "core/selection.hpp"
#include <set>
namespace sketchy {
struct GroupSelectionResult {
    Id group{};
    std::map<Id, Id> movedGeometry; // source context to new context; geometry IDs survive
    ChangeReport changes;
};
GroupSelectionResult groupSelected(Document &doc, Selection &selection, std::string name = "Group");
// The nearest enclosing group; raw records retain their own geometry identities.
Id enclosingGroup(const Document &doc, Id body);
bool persistentlyLocked(const Document &doc, Id body);
Id createGroup(Document &doc, const std::set<Id> &members, std::string name = "Group");
ChangeReport reparentPreservingWorld(Document &doc, Id body, Id parent);
ChangeReport explodeGroup(Document &doc, Id group);
ChangeReport setEntityState(Document &doc, Id body, std::optional<bool> hidden,
                            std::optional<bool> locked);
ChangeReport renameEntity(Document &doc, Id body, std::string name);
} // namespace sketchy
