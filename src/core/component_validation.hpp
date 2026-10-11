#pragma once
#include "core/component_records.hpp"

namespace sketchy {
// Edit validation may reuse only immutable projections from a valid Document.
// Restore/import continue to use the full validator without a baseline.
void validateComponentInstanceEdits(const ComponentDefinitions &definitions,
                                    const ComponentInstances &instances,
                                    const std::map<Id, BodyPtr> &scene, const Document &baseline);
// True when a live record is an instance root or a member target of a binding.
bool componentBound(const Document &doc, Id id);
// Incremental form for edits that only insert records and bindings: no definition
// changes, every added binding's root and targets are inserted records, and every
// modified or erased record is unbound in the valid baseline. Accepts and rejects
// exactly as validateComponentInstanceEdits on the materialized candidate.
void validateComponentInstanceInsertions(const ComponentDefinitions &definitions,
                                         const ComponentInstances &added,
                                         const std::map<Id, BodyPtr> &changedBodies,
                                         const Document &baseline);
} // namespace sketchy
