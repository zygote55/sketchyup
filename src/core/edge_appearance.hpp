#pragma once
#include "core/selection.hpp"
namespace sketchy {
EdgeAppearance edgeAppearance(const Body &body, Id edge);
void validateEdgeAppearances(const Body &body);
// Inherit declared/inferred edge descendants. Conflicting merged appearances
// reject instead of selecting a source by iteration order.
std::map<Id, EdgeAppearance>
inheritedEdgeAppearances(const Body &before, const Body &after,
                         const std::map<Id, std::vector<Id>> &descendants);
void reportEdgeAppearanceChanges(const Body &before, const Body &after, EntityChanges &changes);
ChangeReport setEdgeAppearance(Document &doc, const SelectionSet &edges, Id context,
                               std::optional<bool> hidden, std::optional<bool> soft,
                               std::optional<bool> smooth);
} // namespace sketchy
