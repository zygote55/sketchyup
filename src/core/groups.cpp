#include "core/groups.hpp"
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
} // namespace sketchy
