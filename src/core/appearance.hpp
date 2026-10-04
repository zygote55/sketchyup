#pragma once
#include "core/model.hpp"
namespace sketchy {
std::array<float, 3> faceColor(const Body &body, Id face);
// Topology-producing operations explicitly carry appearance along their face
// lineage. Conflicting colors on a merged face reject instead of losing a side.
void inheritFaceAppearance(const Body &before, Body &after,
                           const std::map<Id, std::vector<Id>> &descendants,
                           std::optional<std::array<float, 3>> newFaceColor = {},
                           std::optional<MaterialSides> newFaceMaterials = {});
} // namespace sketchy
