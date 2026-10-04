#pragma once
#include "core/transform_selection.hpp"
namespace sketchy {
struct ConsolidationResult {
    Id destination{};
    ChangeReport changes;
    std::map<Id, GeometryCopies> transfers;
};
// Weld raw records in one editing context, preserving nested group boundaries.
// An explicit member set narrows eligibility; locked/hidden records always reject.
// Intersecting faces are not booleaned or split by this operation.
ConsolidationResult consolidateContext(Document &doc, Id context = 0,
                                       std::optional<std::set<Id>> members = {});
// Keep independently tagged geometry separate when merging a whole context.
std::vector<std::set<Id>> consolidationGroups(const Document &doc, Id context,
                                              std::optional<std::set<Id>> members = {});
} // namespace sketchy
