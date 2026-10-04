#include "core/groups.hpp"
#include "core/geometry_subset.hpp"
#include <algorithm>
namespace sketchy {
Id enclosingGroup(const Document &doc, Id body) {
    for (auto parent = doc.bodies().at(body)->parent; parent;
         parent = doc.bodies().at(parent)->parent)
        if (doc.bodies().at(parent)->kind == BodyKind::Group)
            return parent;
    return 0;
}
bool persistentlyLocked(const Document &doc, Id body) {
    for (; body; body = doc.bodies().at(body)->parent)
        if (doc.bodies().at(body)->locked)
            return true;
    return false;
}
Id createGroup(Document &doc, const std::set<Id> &members, std::string name) {
    if (members.empty() || members.size() > 10000)
        throw std::runtime_error("Group requires 1–10000 sibling members");
    const auto parent = doc.bodies().at(*members.begin())->parent;
    auto group = std::make_shared<Body>();
    group->id = doc.nextId();
    group->name = std::move(name);
    group->kind = BodyKind::Group;
    group->parent = parent;
    Edit edit{"Make group", {{group->id, nullptr, group}}};
    for (auto id : members) {
        const auto old = doc.bodies().at(id);
        if (old->parent != parent)
            throw std::runtime_error("Group members must share an immediate parent");
        auto member = std::make_shared<Body>(*old);
        member->parent = group->id;
        edit.changes.push_back({id, old, member});
    }
    doc.apply(std::move(edit), doc.revision());
    return group->id;
}
ChangeReport reparentPreservingWorld(Document &doc, Id id, Id parent) {
    const auto old = doc.bodies().at(id);
    if (parent && doc.bodies().at(parent)->kind != BodyKind::Group)
        throw std::runtime_error("Reparent destination must be a group or the model");
    for (auto ancestor = parent; ancestor; ancestor = doc.bodies().at(ancestor)->parent)
        if (ancestor == id)
            throw std::runtime_error("Reparent would create a hierarchy cycle");
    if (parent == old->parent)
        return {};
    const auto frame = parent ? doc.worldTransform(parent) : Transform{};
    auto body = std::make_shared<Body>(*old);
    body->transform = frame.inverse() * doc.worldTransform(id);
    body->parent = parent;
    return doc.apply({"Reparent", {{id, old, body}}}, doc.revision());
}
ChangeReport explodeGroup(Document &doc, Id id) {
    const auto old = doc.bodies().at(id);
    if (old->kind != BodyKind::Group)
        throw std::runtime_error("Explode requires a group");
    Edit edit{"Explode group", {}};
    if (doc.instances().contains(id))
        edit.instances.push_back({id, doc.instances().at(id), nullptr});
    // Groups may own geometry drawn directly in their editing context. Retain
    // that record and its IDs as raw geometry when removing the group boundary.
    const bool geometry = !old->surface.vertices.empty() || !old->guides.empty();
    if (geometry) {
        auto raw = std::make_shared<Body>(*old);
        raw->kind = BodyKind::Geometry;
        edit.changes.push_back({id, old, raw});
    } else
        edit.changes.push_back({id, old, nullptr});
    for (const auto &[childId, child] : doc.bodies()) {
        if (child->parent != id)
            continue;
        auto promoted = std::make_shared<Body>(*child);
        promoted->parent = old->parent;
        promoted->transform = old->transform * child->transform;
        promoted->hidden = old->hidden || child->hidden;
        edit.changes.push_back({childId, child, promoted});
    }
    return doc.apply(std::move(edit), doc.revision());
}
ChangeReport setEntityState(Document &doc, Id id, std::optional<bool> hidden,
                            std::optional<bool> locked) {
    if (!hidden && !locked)
        throw std::runtime_error("Entity state requires hidden or locked");
    if (!id) {
        if (hidden.value_or(false) || locked.value_or(false))
            throw std::runtime_error("Model-wide state may only reveal or unlock entities");
        Edit edit{"Reveal or unlock document entities", {}};
        auto cleared = [&](BodyPtr old) -> BodyPtr {
            auto body = std::make_shared<Body>(*old);
            if (hidden)
                body->hidden = false;
            if (locked)
                body->locked = false;
            return *body == *old ? old : body;
        };
        for (const auto &[bodyId, old] : doc.bodies()) {
            const auto body = cleared(old);
            if (body != old)
                edit.changes.push_back({bodyId, old, body});
        }
        for (const auto &[definitionId, old] : doc.definitions()) {
            auto definition = std::make_shared<ComponentDefinition>(*old);
            bool changed = false;
            for (auto &[member, body] : definition->members) {
                const auto next = cleared(body);
                changed |= next != body;
                body = next;
            }
            if (changed)
                edit.definitions.push_back({definitionId, old, definition});
        }
        return edit.changes.empty() && edit.definitions.empty()
                   ? ChangeReport{}
                   : doc.apply(std::move(edit), doc.revision());
    }
    const auto old = doc.bodies().at(id);
    auto body = std::make_shared<Body>(*old);
    if (hidden)
        body->hidden = *hidden;
    if (locked)
        body->locked = *locked;
    if (*body == *old)
        return {};
    return doc.apply({"Change entity visibility or lock", {{id, old, body}}}, doc.revision());
}
GroupSelectionResult groupSelected(Document &doc, Selection &selection, std::string name) {
    selection.sync(doc);
    if (selection.entities().empty() || selection.entities().size() > 10000)
        throw std::runtime_error("Select editable faces, edges, guides or contexts to group");
    const auto context = selection.context();
    const auto frame = context ? doc.worldTransform(context) : Transform{};
    auto group = std::make_shared<Body>();
    group->id = doc.nextId();
    group->kind = BodyKind::Group;
    group->parent = context;
    group->name = std::move(name);
    GroupSelectionResult result;
    result.group = group->id;
    Edit edit{"Make group from selection", {{group->id, nullptr, group}}};
    Id next = group->id;
    auto allocate = [&] {
        if (next >= UINT64_MAX - 1)
            throw std::runtime_error("Context identity space exhausted");
        return ++next;
    };
    std::map<Id, GeometrySubset> parts;
    for (auto entity : selection.entities()) {
        const auto &source = *doc.bodies().at(entity.body);
        auto &part = parts[entity.body];
        if (entity.kind == SelectionKind::Body)
            part.whole = true;
        else if (entity.kind == SelectionKind::Face)
            includeGeometryFace(source, part, entity.entity);
        else if (entity.kind == SelectionKind::Guide)
            part.guides.insert(entity.entity);
        else {
            const auto &edge = source.topology.edges.at(entity.entity);
            part.edges.insert(entity.entity);
            part.vertices.insert(edge.a);
            part.vertices.insert(edge.b);
        }
    }
    for (const auto &[id, part] : parts) {
        const auto old = doc.bodies().at(id);
        auto member = part.whole ? std::make_shared<Body>(*old) : extractGeometry(*old, part);
        member->parent = group->id;
        member->transform = frame.inverse() * doc.worldTransform(id);
        if (part.whole) {
            edit.changes.push_back({id, old, member});
            continue;
        }
        member->id = allocate();
        member->kind = BodyKind::Geometry;
        result.movedGeometry[id] = member->id;
        edit.changes.push_back({member->id, nullptr, member});
        auto remaining = std::make_shared<Body>(*old);
        if (id == context)
            remaining->kind = BodyKind::Group;
        for (auto face : part.faces)
            remaining->surface.faces.erase(face);
        std::erase_if(remaining->faceColors, [&](const auto &entry) {
            return !remaining->surface.faces.contains(entry.first);
        });
        for (auto guide : part.guides)
            remaining->guides.erase(guide);
        std::set<std::array<Id, 2>> movedEdges;
        for (const auto &[edgeId, edge] : member->topology.edges)
            movedEdges.insert({edge.a, edge.b});
        std::erase_if(remaining->surface.wires, [&](auto edge) {
            return movedEdges.contains({std::min(edge[0], edge[1]), std::max(edge[0], edge[1])});
        });
        std::set<Id> used;
        for (const auto &[faceId, face] : remaining->surface.faces)
            for (const auto &loop : face.loops)
                used.insert(loop.begin(), loop.end());
        for (auto wire : remaining->surface.wires)
            used.insert(wire.begin(), wire.end());
        std::erase_if(remaining->surface.vertices, [&](const auto &vertex) {
            return member->surface.vertices.contains(vertex.first) && !used.contains(vertex.first);
        });
        remaining->topology =
            Topology::rebuild(remaining->surface, old->topology, old->topology.nextId);
        std::erase_if(remaining->curves, [&](const auto &curve) {
            return std::any_of(
                curve.second.edges.begin(), curve.second.edges.end(),
                [&](auto edge) { return !remaining->topology.edges.contains(edge.edge); });
        });
        const bool children = context == id || std::any_of(doc.bodies().begin(), doc.bodies().end(),
                                                           [&](const auto &entry) {
                                                               return entry.second->parent == id;
                                                           });
        if (remaining->kind == BodyKind::Geometry && remaining->surface.vertices.empty() &&
            remaining->guides.empty() && !children)
            edit.changes.push_back({id, old, nullptr});
        else if (*remaining != *old)
            edit.changes.push_back({id, old, remaining});
    }
    result.changes = doc.apply(std::move(edit), doc.revision());
    return result;
}
} // namespace sketchy
