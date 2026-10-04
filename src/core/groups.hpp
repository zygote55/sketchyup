#pragma once
#include "core/model.hpp"
#include <set>
namespace sketchy {
// The nearest enclosing group; raw records retain their own geometry identities.
Id enclosingGroup(const Document &doc, Id body);
bool persistentlyLocked(const Document &doc, Id body);
Id createGroup(Document &doc, const std::set<Id> &members, std::string name = "Group");
ChangeReport reparentPreservingWorld(Document &doc, Id body, Id parent);
ChangeReport explodeGroup(Document &doc, Id group);
ChangeReport setEntityState(Document &doc, Id body, std::optional<bool> hidden,
                            std::optional<bool> locked);
} // namespace sketchy
