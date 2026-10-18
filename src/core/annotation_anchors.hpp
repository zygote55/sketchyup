#pragma once
#include "geometry/topology.hpp"
#include <map>
#include <optional>
#include <set>
namespace sketchy {
class Document;
enum class AnchorKind { Point, Vertex, Edge, Face };
enum class AnchorState { Resolved, Missing, Ambiguous };
struct AnnotationAnchor {
    AnchorKind kind{AnchorKind::Point};
    Id body{}, entity{};
    double parameter{};           // Edge fraction from the canonical a endpoint to b.
    std::array<Id, 3> vertices{}; // Face support triangle, in body-local coordinates.
    std::array<double, 3> weights{};
    Vec3 fallback{}; // Fixed world point, or last unambiguously resolved world position.
    AnchorState state{AnchorState::Resolved};
    bool operator==(const AnnotationAnchor &) const = default;
};
struct ResolvedAnchor {
    Vec3 point;
    AnchorState state{AnchorState::Resolved};
};
void validateAnnotationAnchor(const AnnotationAnchor &anchor);
AnnotationAnchor pointAnchor(Vec3 world);
AnnotationAnchor vertexAnchor(const Document &doc, Id body, Id vertex);
AnnotationAnchor edgeAnchor(const Document &doc, Id body, Id edge, double fraction);
AnnotationAnchor faceAnchor(const Document &doc, Id body, Id face, Vec3 localPoint);
ResolvedAnchor resolveAnnotationAnchor(const Document &doc, const AnnotationAnchor &anchor);
// Explicit topology lineage controls rebinding. Unique geometric descendants may
// retain association; shared split boundaries remain ambiguous. Broken anchors
// preserve their last world position and never silently attach to reused IDs.
AnnotationAnchor remapAnnotationAnchor(const Document &before, const Document &after,
                                       const std::map<Id, TopologyChanges> &changes,
                                       const AnnotationAnchor &anchor);
// Geometry transfers (Edit::transfers: source context -> contexts that received
// geometry with vertex, edge and face IDs unchanged) carry an anchor whose entity left
// its context. The successor is returned only when the entity is gone from its context,
// exactly one target holds it (an edge with the same endpoint IDs; a face whose support
// triangle still lies on it) and that point is at the same world position. Otherwise
// nothing is returned and the caller keeps the anchor as it is. Shared by model
// annotations (resolveAnnotationEdit) and paper anchors (remapDrawingAnchor).
std::optional<AnnotationAnchor>
transferAnnotationAnchor(const Document &before, const Document &after,
                         const std::map<Id, std::set<Id>> &transfers,
                         const AnnotationAnchor &anchor);
} // namespace sketchy
