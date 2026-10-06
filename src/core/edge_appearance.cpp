#include "core/edge_appearance.hpp"
#include <algorithm>
namespace sketchy {
EdgeAppearance edgeAppearance(const Body &body, Id edge) {
    const auto found = body.edgeAppearances.find(edge);
    return found == body.edgeAppearances.end() ? EdgeAppearance{} : found->second;
}
void validateEdgeAppearances(const Body &body) {
    if (body.edgeAppearances.size() > Topology::edgeLimit)
        throw std::runtime_error("Too many edge appearance records");
    for (const auto &[id, style] : body.edgeAppearances)
        if (!body.topology.edges.contains(id) || style == EdgeAppearance{})
            throw std::runtime_error(
                "Edge appearance must name an existing edge with nondefault flags");
}
std::map<Id, EdgeAppearance>
inheritedEdgeAppearances(const Body &before, const Body &after,
                         const std::map<Id, std::vector<Id>> &descendants) {
    auto result = after.edgeAppearances;
    for (auto it = result.begin(); it != result.end();) {
        if (after.topology.edges.contains(it->first)) {
            ++it;
            continue;
        }
        if (!before.edgeAppearances.contains(it->first) ||
            before.edgeAppearances.at(it->first) != it->second)
            throw std::runtime_error("Edge appearance references a missing edge");
        it = result.erase(it);
    }
    std::map<Id, EdgeAppearance> inherited;
    auto assign = [&](Id target, EdgeAppearance style) {
        const auto [it, added] = inherited.emplace(target, style);
        if (!added && it->second != style)
            throw std::runtime_error("Merged edges have different appearances; make their hide, "
                                     "soften and smooth flags alike before merging");
    };
    for (const auto &[source, targets] : descendants) {
        const auto style = edgeAppearance(before, source);
        for (auto target : targets) {
            if (target == source)
                continue; // Retained identity honors an explicit appearance edit.
            if (before.topology.edges.contains(target))
                assign(target, edgeAppearance(before, target));
            assign(target, style);
        }
    }
    for (const auto &[target, style] : inherited) {
        if (before.topology.edges.contains(target))
            continue;
        if (result.contains(target) && result.at(target) != style)
            throw std::runtime_error("New edge appearance disagrees with its source lineage");
        if (style != EdgeAppearance{})
            result[target] = style;
    }
    return result;
}
void reportEdgeAppearanceChanges(const Body &before, const Body &after, EntityChanges &changes) {
    if (before.edgeAppearances == after.edgeAppearances)
        return;
    for (const auto &[id, edge] : after.topology.edges)
        if (before.topology.edges.contains(id) &&
            edgeAppearance(before, id) != edgeAppearance(after, id))
            changes.modified.push_back(id);
    std::sort(changes.modified.begin(), changes.modified.end());
    changes.modified.erase(std::unique(changes.modified.begin(), changes.modified.end()),
                           changes.modified.end());
}
ChangeReport setEdgeAppearance(Document &doc, const SelectionSet &edges, Id context,
                               std::optional<bool> hidden, std::optional<bool> soft,
                               std::optional<bool> smooth) {
    if (edges.empty() || edges.size() > 4096 || (!hidden && !soft && !smooth))
        throw std::runtime_error("Edge appearance requires 1–4096 edges and an explicit flag");
    Selection policy;
    policy.enter(doc, context);
    std::map<Id, std::shared_ptr<Body>> changed;
    for (const auto &ref : edges) {
        if (ref.kind != SelectionKind::Edge || !doc.bodies().contains(ref.body))
            throw std::runtime_error("Edge appearance requires existing typed edges");
        const auto &before = *doc.bodies().at(ref.body);
        if (!policy.inContext(doc, ref.body) ||
            policy.hidden(doc, {ref.body, SelectionKind::Body, 0}) ||
            policy.locked(doc, ref.body) ||
            (before.kind != BodyKind::Geometry && ref.body != context) ||
            !before.topology.edges.contains(ref.entity))
            throw std::runtime_error(
                "Edge appearance target is missing, hidden, locked or outside context");
        if (!changed.contains(ref.body)) {
            if (changed.size() >= 128)
                throw std::runtime_error("Edge appearance is limited to 128 bodies");
            changed[ref.body] = std::make_shared<Body>(before);
        }
        auto &body = *changed.at(ref.body);
        auto style = edgeAppearance(body, ref.entity);
        if (hidden)
            style.hidden = *hidden;
        if (soft)
            style.soft = *soft;
        if (smooth)
            style.smooth = *smooth;
        if (style == EdgeAppearance{})
            body.edgeAppearances.erase(ref.entity);
        else
            body.edgeAppearances[ref.entity] = style;
    }
    Edit edit{"Edge appearance", {}};
    for (auto &[id, body] : changed)
        if (*body != *doc.bodies().at(id))
            edit.changes.push_back({id, doc.bodies().at(id), std::move(body)});
    if (edit.changes.empty())
        return {};
    return doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
