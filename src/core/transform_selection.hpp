#pragma once
#include "core/model.hpp"
#include <compare>
#include <set>
namespace sketchy {
enum class TransformKind { Context, Face, Edge, Vertex, Guide };
struct TransformTarget {
    Id body{};
    TransformKind kind{TransformKind::Context};
    Id entity{};
    auto operator<=>(const TransformTarget &) const = default;
};
using TransformTargets = std::set<TransformTarget>;
enum class TransformSpace { World, Local };
struct GeometryCopies {
    std::map<Id, Id> vertices, edges, faces, curves, guides;
};
struct TransformResult {
    ChangeReport changes;
    // Copied contexts retain source subentity IDs in their new context namespace.
    std::map<Id, Id> copies;
    // Raw geometry is copied inside its current context, with fresh subentity IDs.
    std::map<Id, GeometryCopies> geometryCopies;
};
// Shared vertices move once and remain attached to every incident face/edge.
// Invalid/nonplanar results reject atomically; this operation does not weld,
// boolean, or automatically triangulate intersecting/folded geometry.
TransformResult transformSelected(Document &doc, const TransformTargets &targets,
                                  const Transform &operation, Vec3 pivot = {},
                                  TransformSpace space = TransformSpace::World, bool copy = false);
} // namespace sketchy
