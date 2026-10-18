#pragma once
#include "core/drawing_item_records.hpp"
#include <optional>
#include <set>
namespace sketchy {
class Document;
struct Edit;
// Paper anchor lineage (ADR 0164 §3, addendum). A model edit rewrites the vertex and
// edge anchors of view-bound dimensions and labels inside the same edit, the way it
// rewrites model annotations (remapAnnotationAnchor), so they survive edge splits and
// make component. Only the anchors change; lastMm and every other field are kept.

// The successor of one paper anchor, or nothing when the anchor is kept as it is:
// it still resolves by ID, it was already broken, or the lineage is missing or
// ambiguous (the item then breaks, as before, and Reattach repairs it).
//  - Same context: the edit's topology lineage (remapAnnotationAnchor). An edge
//    anchor that lands on an endpoint of its successor pieces, such as the new
//    vertex of a split at the anchor, becomes a vertex anchor there.
//  - Another context: a transfer that moved the entity with its ID unchanged to
//    exactly one context, at the same world position.
std::optional<AnnotationAnchor> remapDrawingAnchor(const Document &before, const Document &after,
                                                   const std::map<Id, TopologyChanges> &changes,
                                                   const std::map<Id, std::set<Id>> &transfers,
                                                   const AnnotationAnchor &anchor);
// Whether any bound item anchors a context that the lineage names.
bool drawingAnchorsTouched(const DrawingItemRecords &items,
                           const std::map<Id, TopologyChanges> &changes,
                           const std::map<Id, std::set<Id>> &transfers);
// Whether `after` differs from `before` only in the anchors of a bound item.
bool drawingItemAnchorsOnly(const DrawingItemRecord &before, const DrawingItemRecord &after);
// Rewrites the anchors of every bound item the edit did not author, appending one
// anchor-only change per rewritten item to `edit.drawingItems`.
void resolveDrawingAnchorEdit(const Document &before, const Document &after,
                              const std::map<Id, TopologyChanges> &changes, Edit &edit,
                              DrawingItemRecords &items);
} // namespace sketchy
