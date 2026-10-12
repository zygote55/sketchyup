#pragma once
#include "core/component_records.hpp"
#include <set>

namespace sketchy {
// Edit validation may reuse only immutable projections from a valid Document.
// Restore/import continue to use the full validator without a baseline.
void validateComponentInstanceEdits(const ComponentDefinitions &definitions,
                                    const ComponentInstances &instances,
                                    const std::map<Id, BodyPtr> &scene, const Document &baseline);
// True when a live record is an instance root or a member target of a binding.
bool componentBound(const Document &doc, Id id);
// True when a live record is a member target of a binding (nested placement roots
// included); a top-level placement root is not.
bool componentMemberBound(const Document &doc, Id id);
// Adds every record a valid placement rooted at `root` owns, through nested
// placements, excluding the root. In a valid document these are exactly the
// root's descendants.
void collectPlacementDescendants(const ComponentInstances &instances, Id root,
                                 std::set<Id> &descendants);
// Incremental form for edits that insert records and bindings and may change
// existing placement roots that no binding owns: no definition changes, every
// added binding's root and targets are inserted records, every changed root is
// listed in `changedRoots` and keeps its binding, and every other modified record
// is unbound in the valid baseline; nothing is erased. Accepts and rejects exactly
// as validateComponentInstanceEdits on the materialized candidate.
void validateComponentInstanceInsertions(const ComponentDefinitions &definitions,
                                         const ComponentInstances &added,
                                         const std::set<Id> &changedRoots,
                                         const std::map<Id, BodyPtr> &changedBodies,
                                         const Document &baseline);
} // namespace sketchy
