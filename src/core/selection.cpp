#include "core/selection.hpp"
#include "core/edge_appearance.hpp"
#include "core/groups.hpp"
#include <algorithm>
namespace sketchy {
bool Selection::exists(const Document &doc, SelectedEntity e) const {
    if (!doc.bodies().contains(e.body))
        return false;
    const auto &b = *doc.bodies().at(e.body);
    switch (e.kind) {
    case SelectionKind::Body:
        return e.entity == 0;
    case SelectionKind::Face:
        return b.surface.faces.contains(e.entity);
    case SelectionKind::Edge:
        return b.topology.edges.contains(e.entity);
    case SelectionKind::Guide:
        return b.guides.contains(e.entity);
    }
    return false;
}
bool Selection::hidden(const Document &doc, SelectedEntity e, bool includeSoft) const {
    if (hidden_.contains(e))
        return true;
    if (e.kind == SelectionKind::Edge && doc.bodies().contains(e.body)) {
        const auto appearance = edgeAppearance(*doc.bodies().at(e.body), e.entity);
        if (appearance.hidden || (includeSoft && appearance.soft))
            return true;
    }
    for (auto body = e.body; body && doc.bodies().contains(body);
         body = doc.bodies().at(body)->parent)
        if (doc.bodies().at(body)->hidden || !tagVisible(doc.tags(), doc.bodies().at(body)->tag) ||
            hidden_.contains({body, SelectionKind::Body, 0}))
            return true;
    return false;
}
bool Selection::locked(const Document &doc, Id body) const {
    for (; body && doc.bodies().contains(body); body = doc.bodies().at(body)->parent)
        if (doc.bodies().at(body)->locked || locked_.contains(body))
            return true;
    return false;
}
bool Selection::inContext(const Document &doc, Id body) const {
    if (!doc.bodies().contains(body))
        return false;
    if (body == context_)
        return true;
    if (context_ && doc.bodies().at(context_)->kind == BodyKind::Geometry)
        return false; // Legacy explicit raw-context editing.
    return doc.bodies().at(body)->kind == BodyKind::Geometry &&
           enclosingGroup(doc, body) == context_;
}
bool Selection::selectable(const Document &doc, SelectedEntity e) const {
    if (!exists(doc, e) || locked(doc, e.body) || (!showHidden_ && hidden(doc, e)))
        return false;
    if (e.kind != SelectionKind::Body)
        return inContext(doc, e.body);
    if (e.body == context_)
        return false;
    const bool rawContext = context_ && doc.bodies().at(context_)->kind == BodyKind::Geometry;
    if (rawContext ? (doc.bodies().at(e.body)->kind != BodyKind::Group ||
                      doc.bodies().at(e.body)->parent != context_)
                   : enclosingGroup(doc, e.body) != context_)
        return false;
    if (!doc.isCurrentSnapshot(lockCacheStamp_)) {
        persistentLockedAncestors_.clear();
        for (const auto &[id, body] : doc.bodies())
            if (body->locked)
                for (auto parent = id; parent; parent = doc.bodies().at(parent)->parent)
                    persistentLockedAncestors_.insert(parent);
        lockCacheStamp_ = doc.saveStamp();
    }
    if (persistentLockedAncestors_.contains(e.body))
        return false;
    for (auto lockedBody : locked_)
        for (auto parent = lockedBody; parent && doc.bodies().contains(parent);
             parent = doc.bodies().at(parent)->parent)
            if (parent == e.body)
                return false;
    return true;
}
std::optional<SelectedEntity> Selection::pickTarget(const Document &doc,
                                                    SelectedEntity entity) const {
    if (!exists(doc, entity) || (!showHidden_ && hidden(doc, entity)))
        return {};
    if (selectable(doc, entity))
        return entity;
    for (auto body = entity.body; body; body = doc.bodies().at(body)->parent) {
        const SelectedEntity group{body, SelectionKind::Body, 0};
        if (doc.bodies().at(body)->kind == BodyKind::Group && selectable(doc, group))
            return group;
    }
    return {};
}
bool Selection::inActiveHierarchy(const Document &doc, Id body) const {
    if (!context_)
        return true;
    for (; body; body = doc.bodies().at(body)->parent)
        if (body == context_)
            return true;
    return false;
}
void Selection::prune(const Document &doc) {
    std::erase_if(hidden_, [&](auto e) { return !exists(doc, e); });
    std::erase_if(locked_, [&](Id body) { return !doc.bodies().contains(body); });
    if (context_ && (!doc.bodies().contains(context_) || locked(doc, context_) ||
                     (!showHidden_ && hidden(doc, {context_, SelectionKind::Body, 0}))))
        context_ = 0;
    std::erase_if(entities_, [&](auto e) { return !selectable(doc, e); });
}
void Selection::sync(const Document &doc) {
    if (!doc.owns(session_)) {
        entities_.clear();
        hidden_.clear();
        locked_.clear();
        context_ = 0;
        showHidden_ = false;
        session_ = doc.saveStamp();
    }
    prune(doc);
}
bool Selection::apply(const Document &doc, const SelectionSet &entities, SelectionMode mode) {
    const auto requested = entities;
    sync(doc);
    if (mode != SelectionMode::Replace && mode != SelectionMode::Add &&
        mode != SelectionMode::Toggle)
        throw std::runtime_error("Unknown selection mode");
    const auto before = entities_;
    if (mode == SelectionMode::Replace)
        entities_.clear();
    for (auto e : requested) {
        if (!selectable(doc, e))
            continue;
        if (mode == SelectionMode::Toggle && entities_.contains(e))
            entities_.erase(e);
        else
            entities_.insert(e);
    }
    // A whole-context selection already covers its subentities.
    std::erase_if(entities_, [&](auto e) {
        if (e.kind != SelectionKind::Body && entities_.contains({e.body, SelectionKind::Body, 0}))
            return true;
        for (auto parent = doc.bodies().at(e.body)->parent; parent;
             parent = doc.bodies().at(parent)->parent)
            if (entities_.contains({parent, SelectionKind::Body, 0}))
                return true;
        return false;
    });
    return before != entities_;
}
void Selection::enter(const Document &doc, Id context) {
    sync(doc);
    if (context && (!doc.bodies().contains(context) || locked(doc, context) ||
                    (!showHidden_ && hidden(doc, {context, SelectionKind::Body, 0}))))
        throw std::runtime_error("Cannot enter a missing, hidden or locked context");
    context_ = context;
    entities_.clear();
}
void Selection::showHidden(const Document &doc, bool show) {
    sync(doc);
    showHidden_ = show;
    prune(doc);
}
void Selection::hide(const Document &doc, const SelectionSet &entities) {
    sync(doc);
    for (auto e : entities)
        if (selectable(doc, e))
            hidden_.insert(e);
    prune(doc);
}
void Selection::reveal(const Document &doc) {
    sync(doc);
    hidden_.clear();
}
void Selection::restoreSceneVisibility(const Document &doc, const SceneVisibility &visibility) {
    visibility.validate();
    sync(doc);
    auto hidden = hidden_;
    std::erase_if(hidden, [&](const auto &entity) {
        return visibility.bodyVisible.contains(entity.body);
    });
    for (const auto &entity : visibility.hiddenEntities) {
        const auto kind = entity.kind == SceneEntityKind::Body ? SelectionKind::Body
            : entity.kind == SceneEntityKind::Face ? SelectionKind::Face
            : entity.kind == SceneEntityKind::Edge ? SelectionKind::Edge : SelectionKind::Guide;
        const SelectedEntity selected{entity.body, kind, entity.entity};
        if (exists(doc, selected))
            hidden.insert(selected);
    }
    hidden_.swap(hidden);
    showHidden_ = visibility.showHidden;
    prune(doc);
}
void Selection::lock(const Document &doc, Id body, bool locked) {
    sync(doc);
    if (!doc.bodies().contains(body))
        throw std::runtime_error("Context does not exist");
    if (locked)
        locked_.insert(body);
    else
        locked_.erase(body);
    prune(doc);
}
void Selection::unlockAll(const Document &doc) {
    sync(doc);
    locked_.clear();
}
SelectionSet Selection::boundary(const Document &doc, SelectedEntity e) const {
    SelectionSet result;
    if (!selectable(doc, e))
        return result;
    result.insert(e);
    if (e.kind == SelectionKind::Face) {
        const auto &body = *doc.bodies().at(e.body);
        const auto adjacency = body.topology.adjacency(body.surface);
        for (const auto &loop : adjacency.faceLoops.at(e.entity))
            for (const auto &edge : loop) {
                SelectedEntity selected{e.body, SelectionKind::Edge, edge.edge};
                if (selectable(doc, selected))
                    result.insert(selected);
            }
    }
    return result;
}
SelectionSet Selection::connected(const Document &doc, SelectedEntity e) const {
    SelectionSet result;
    if (!selectable(doc, e))
        return result;
    if (e.kind == SelectionKind::Body || e.kind == SelectionKind::Guide)
        return {e};
    const auto &body = *doc.bodies().at(e.body);
    const auto adjacency = body.topology.adjacency(body.surface);
    std::vector<Id> pending;
    if (e.kind == SelectionKind::Edge)
        pending.push_back(e.entity);
    else
        for (const auto &loop : adjacency.faceLoops.at(e.entity))
            for (const auto &edge : loop)
                pending.push_back(edge.edge);
    std::set<Id> visited, visitedVertices, visitedFaces;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second)
            continue;
        const SelectedEntity edge{e.body, SelectionKind::Edge, id};
        if (selectable(doc, edge))
            result.insert(edge);
        const auto &record = body.topology.edges.at(id);
        for (auto vertex : {record.a, record.b})
            if (visitedVertices.insert(vertex).second)
                for (auto neighbor : adjacency.vertexEdges.at(vertex))
                    if (!visited.contains(neighbor))
                        pending.push_back(neighbor);
        for (const auto &incident : adjacency.edgeFaces.at(id)) {
            if (visitedFaces.insert(incident.face).second)
                for (const auto &loop : adjacency.faceLoops.at(incident.face))
                    for (const auto &boundary : loop)
                        if (!visited.contains(boundary.edge))
                            pending.push_back(boundary.edge);
            SelectedEntity face{e.body, SelectionKind::Face, incident.face};
            if (selectable(doc, face))
                result.insert(face);
        }
    }
    return result;
}
std::vector<SelectedEntity> Selection::ordered(const Document &doc) const {
    std::vector<SelectedEntity> result;
    auto add = [&](SelectedEntity e) {
        if (selectable(doc, e))
            result.push_back(e);
    };
    for (const auto &[id, body] : doc.bodies()) {
        add({id, SelectionKind::Body, 0});
        if (!inContext(doc, id) || locked(doc, id))
            continue;
        for (const auto &[face, record] : body->surface.faces)
            add({id, SelectionKind::Face, face});
        for (const auto &[edge, record] : body->topology.edges)
            add({id, SelectionKind::Edge, edge});
        for (const auto &[guide, record] : body->guides)
            add({id, SelectionKind::Guide, guide});
    }
    return result;
}
std::string Selection::summary() const {
    if (entities_.empty())
        return "No selection";
    std::array<size_t, 4> counts{};
    for (auto e : entities_)
        ++counts[size_t(e.kind)];
    std::string result;
    const char *names[]{"context", "face", "edge", "guide"};
    for (size_t i = 0; i < counts.size(); ++i)
        if (counts[i]) {
            if (!result.empty())
                result += ", ";
            result += std::to_string(counts[i]) + " " + names[i] + (counts[i] == 1 ? "" : "s");
        }
    return result;
}
ChangeReport eraseSelected(Document &doc, Selection &selection) {
    selection.sync(doc);
    if (selection.entities().empty())
        return {};
    if (std::count_if(selection.entities().begin(), selection.entities().end(),
                      [](auto e) { return e.kind != SelectionKind::Body; }) > 100)
        throw std::runtime_error("Delete supports up to 100 subentities per edit; select whole "
                                 "contexts for bulk deletion");
    auto staged = doc;
    struct Lineage {
        std::map<Id, std::vector<Id>> faces, edges, vertices;
    };
    std::map<Id, Lineage> lineages;
    auto compose = [&](const ChangeReport &report) {
        for (const auto &[id, changes] : report) {
            const auto &original = *doc.bodies().at(id);
            auto update = [](auto &mapping, const auto &before, const EntityChanges &step) {
                for (auto &[source, targets] : mapping) {
                    std::vector<Id> next;
                    for (auto target : targets) {
                        const auto found = step.descendants.find(target);
                        if (found == step.descendants.end())
                            next.push_back(target);
                        else
                            next.insert(next.end(), found->second.begin(), found->second.end());
                    }
                    std::sort(next.begin(), next.end());
                    next.erase(std::unique(next.begin(), next.end()), next.end());
                    targets = std::move(next);
                }
                for (const auto &[source, targets] : step.descendants)
                    if (before.contains(source) && !mapping.contains(source) &&
                        targets != std::vector<Id>{source})
                        mapping[source] = targets;
            };
            auto &mapping = lineages[id];
            update(mapping.faces, original.surface.faces, changes.faces);
            update(mapping.edges, original.topology.edges, changes.edges);
            update(mapping.vertices, original.surface.vertices, changes.vertices);
        }
    };
    Edit contexts{"Delete selected contexts", {}};
    for (const auto &[id, body] : doc.bodies())
        for (auto ancestor = id; ancestor; ancestor = doc.bodies().at(ancestor)->parent)
            if (selection.entities().contains({ancestor, SelectionKind::Body, 0})) {
                contexts.changes.push_back({id, body, nullptr});
                break;
            }
    for (const auto &change : contexts.changes)
        if (doc.instances().contains(change.id))
            contexts.instances.push_back({change.id, doc.instances().at(change.id), nullptr});
    if (!contexts.changes.empty())
        compose(staged.apply(std::move(contexts), staged.revision()));
    // Whole contexts first, then faces before their retained boundary edges.
    // Healing may retire later IDs, so skip only IDs already consumed by this
    // staged operation; never publish intermediate mutations.
    for (const auto kind : {SelectionKind::Face, SelectionKind::Edge, SelectionKind::Guide})
        for (auto e : selection.entities()) {
            if (e.kind != kind || !selection.exists(staged, e))
                continue;
            switch (kind) {
            case SelectionKind::Body:
                break; // Contexts were already removed in a single staged edit.
            case SelectionKind::Face:
                compose(staged.eraseFace(e.body, e.entity));
                break;
            case SelectionKind::Edge:
                compose(staged.eraseEdge(e.body, e.entity));
                break;
            case SelectionKind::Guide:
                compose(staged.eraseGuide(e.body, e.entity));
                break;
            }
        }
    Edit edit{"Delete selection", {}};
    appendSceneMetadataChanges(edit, doc, staged);
    for (const auto &[id, before] : doc.bodies()) {
        const auto after = staged.bodies().contains(id) ? staged.bodies().at(id) : BodyPtr{};
        if (before != after) {
            auto &mapping = lineages[id];
            edit.changes.push_back({id, before, after, std::move(mapping.faces),
                                    std::move(mapping.vertices), std::move(mapping.edges)});
        }
    }
    const auto report = doc.apply(std::move(edit), doc.revision());
    selection.clear();
    selection.sync(doc);
    return report;
}
} // namespace sketchy
