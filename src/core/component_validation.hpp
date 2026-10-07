#pragma once
#include "core/component_records.hpp"

namespace sketchy {
// Edit validation may reuse only immutable projections from a valid Document.
// Restore/import continue to use the full validator without a baseline.
void validateComponentInstanceEdits(const ComponentDefinitions &definitions,
                                    const ComponentInstances &instances,
                                    const std::map<Id, BodyPtr> &scene, const Document &baseline);
} // namespace sketchy
