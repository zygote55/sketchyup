#include "core/drawing_item_lineage.hpp"
#include "core/model.hpp"
#include <algorithm>
#include <type_traits>
#include <utility>
namespace sketchy {
namespace {
// The bound anchors of an item: a dimension's two, or a view-target label's one.
template <class Record> auto boundAnchors(Record &item) {
    using Anchor =
        std::conditional_t<std::is_const_v<Record>, const AnnotationAnchor, AnnotationAnchor>;
    std::vector<Anchor *> result;
    if (auto *dimension = std::get_if<DrawingDimensionItem>(&item.data))
        result = {&dimension->anchors[0], &dimension->anchors[1]};
    else if (auto *label = std::get_if<DrawingLabelItem>(&item.data);
             label && label->target.kind == DrawingLabelTargetKind::View)
        result = {&label->target.anchor};
    return result;
}
bool entityExists(const Body &body, const AnnotationAnchor &anchor) {
    return anchor.kind == AnchorKind::Vertex ? body.surface.vertices.contains(anchor.entity)
                                             : body.topology.edges.contains(anchor.entity);
}
// An edge anchor whose point is an endpoint of its successor pieces becomes a vertex
// anchor there; any other point stays on the one piece that contains it.
std::optional<AnnotationAnchor> splitSuccessor(const Document &before, const Document &after,
                                               const TopologyChanges &changes,
                                               const AnnotationAnchor &anchor,
                                               const AnnotationAnchor &remapped) {
    const auto &oldBody = *before.bodies().at(anchor.body);
    const auto &body = *after.bodies().at(anchor.body);
    const auto &oldEdge = oldBody.topology.edges.at(anchor.entity);
    if (!body.surface.vertices.contains(oldEdge.a) || !body.surface.vertices.contains(oldEdge.b))
        return remapped.state == AnchorState::Resolved ? std::optional{remapped} : std::nullopt;
    const auto a = body.surface.vertices.at(oldEdge.a), b = body.surface.vertices.at(oldEdge.b);
    const auto target = a + (b - a) * anchor.parameter;
    const auto found = changes.edges.descendants.find(anchor.entity);
    if (found == changes.edges.descendants.end())
        return remapped.state == AnchorState::Resolved ? std::optional{remapped} : std::nullopt;
    std::set<Id> endpoints;
    for (auto piece : found->second) {
        if (!body.topology.edges.contains(piece))
            continue;
        const auto &edge = body.topology.edges.at(piece);
        for (auto vertex : {edge.a, edge.b})
            if (length(body.surface.vertices.at(vertex) - target) <= tolerance)
                endpoints.insert(vertex);
    }
    if (endpoints.size() == 1)
        return vertexAnchor(after, anchor.body, *endpoints.begin());
    if (endpoints.empty() && remapped.state == AnchorState::Resolved)
        return remapped;
    return {};
}
} // namespace
std::optional<AnnotationAnchor> remapDrawingAnchor(const Document &before, const Document &after,
                                                   const std::map<Id, TopologyChanges> &changes,
                                                   const std::map<Id, std::set<Id>> &transfers,
                                                   const AnnotationAnchor &anchor) {
    if ((anchor.kind != AnchorKind::Vertex && anchor.kind != AnchorKind::Edge) ||
        anchor.state != AnchorState::Resolved)
        return {};
    // An anchor that already failed to resolve is never re-bound.
    const auto previous = resolveAnnotationAnchor(before, anchor);
    if (previous.state != AnchorState::Resolved)
        return {};
    const auto context = after.bodies().find(anchor.body);
    if (context != after.bodies().end() && entityExists(*context->second, anchor))
        return {}; // IDs are never reassigned, so the anchor still names its entity.
    try {
        if (context != after.bodies().end())
            if (const auto report = changes.find(anchor.body); report != changes.end()) {
                const auto remapped = remapAnnotationAnchor(before, after, changes, anchor);
                const auto result =
                    anchor.kind == AnchorKind::Edge
                        ? splitSuccessor(before, after, report->second, anchor, remapped)
                    : remapped.state == AnchorState::Resolved ? std::optional{remapped}
                                                              : std::nullopt;
                if (result)
                    return result;
            }
        const auto moved = transfers.find(anchor.body);
        if (moved == transfers.end())
            return {};
        std::vector<AnnotationAnchor> matches;
        for (auto target : moved->second) {
            const auto found = after.bodies().find(target);
            if (found == after.bodies().end() || !entityExists(*found->second, anchor))
                continue;
            if (anchor.kind == AnchorKind::Vertex) {
                matches.push_back(vertexAnchor(after, target, anchor.entity));
                continue;
            }
            const auto &old = before.bodies().at(anchor.body)->topology.edges.at(anchor.entity);
            const auto &edge = found->second->topology.edges.at(anchor.entity);
            if (edge.a == old.a && edge.b == old.b)
                matches.push_back(edgeAnchor(after, target, anchor.entity, anchor.parameter));
        }
        // A transfer keeps world positions; anything else is not the same point.
        if (matches.size() == 1 && length(matches.front().fallback - previous.point) <=
                                       tolerance * std::max(1., length(previous.point)))
            return matches.front();
    } catch (const std::runtime_error &) {
        // Lineage beyond its work budgets keeps today's broken state.
    }
    return {};
}
bool drawingAnchorsTouched(const DrawingItemRecords &items,
                           const std::map<Id, TopologyChanges> &changes,
                           const std::map<Id, std::set<Id>> &transfers) {
    for (const auto &[id, pointer] : items) {
        for (const auto *anchor : boundAnchors(std::as_const(*pointer)))
            if (changes.contains(anchor->body) || transfers.contains(anchor->body))
                return true;
    }
    return false;
}
bool drawingItemAnchorsOnly(const DrawingItemRecord &before, const DrawingItemRecord &after) {
    if (before.kind() != after.kind())
        return false;
    auto restored = after, original = before;
    const auto anchors = boundAnchors(restored), previous = boundAnchors(original);
    if (anchors.empty() || anchors.size() != previous.size())
        return false;
    for (size_t i = 0; i < anchors.size(); ++i)
        *anchors[i] = *previous[i];
    return restored == before && !(after == before);
}
void resolveDrawingAnchorEdit(const Document &before, const Document &after,
                              const std::map<Id, TopologyChanges> &changes, Edit &edit,
                              DrawingItemRecords &items) {
    if (edit.drawingAnchorsResolved || (changes.empty() && edit.transfers.empty()))
        return;
    std::set<Id> authored;
    for (const auto &change : edit.drawingItems)
        authored.insert(change.id);
    for (auto &[id, pointer] : items) {
        if (authored.contains(id))
            continue;
        auto updated = *pointer;
        bool changed = false;
        for (auto *anchor : boundAnchors(updated)) {
            if (!changes.contains(anchor->body) && !edit.transfers.contains(anchor->body))
                continue;
            if (const auto successor =
                    remapDrawingAnchor(before, after, changes, edit.transfers, *anchor)) {
                validateAnnotationAnchor(*successor);
                *anchor = *successor;
                changed = true;
            }
        }
        if (!changed || updated == *pointer)
            continue;
        const auto old = pointer;
        pointer = std::make_shared<DrawingItemRecord>(std::move(updated));
        edit.drawingItems.push_back({id, old, pointer});
    }
}
} // namespace sketchy
