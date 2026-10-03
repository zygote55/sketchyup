#include "core/model.hpp"
#include <algorithm>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
namespace sketchy {
namespace {
size_t bytes(const BodyPtr &b) {
    if (!b)
        return 0;
    size_t n = sizeof(Body) + b->name.size() + b->surface.vertices.size() * (sizeof(Vec3) + 64) +
               b->surface.wires.size() * sizeof(std::array<Id, 2>);
    for (const auto &[id, f] : b->surface.faces) {
        n += sizeof(Face) + 64;
        for (const auto &l : f.loops)
            n += sizeof(l) + l.size() * sizeof(Id);
    }
    for (const auto &[key, value] : b->properties) {
        n += key.size() + sizeof(value) + 96;
        if (auto text = std::get_if<std::string>(&value))
            n += text->size();
    }
    return n;
}
Transform worldTransformIn(const std::map<Id, BodyPtr> &bodies, Id id) {
    Transform result;
    std::set<Id> visited;
    while (id) {
        if (!visited.insert(id).second || visited.size() > 128)
            throw std::runtime_error("Cyclic or excessively deep hierarchy");
        auto found = bodies.find(id);
        if (found == bodies.end())
            throw std::runtime_error("Missing parent entity");
        result = found->second->transform * result;
        id = found->second->parent;
    }
    return result;
}
void validateDocumentSize(const std::map<Id, BodyPtr> &bodies) {
    size_t vertices = 0, faces = 0, wires = 0;
    for (const auto &[id, b] : bodies) {
        vertices += b->surface.vertices.size();
        faces += b->surface.faces.size();
        wires += b->surface.wires.size();
        const auto world = worldTransformIn(bodies, id);
        for (const auto &[vertex, point] : b->surface.vertices)
            checkPoint(world.point(point));
    }
    if (bodies.size() > 10000 || vertices > 100000 || faces > 100000 || wires > 100000)
        throw std::runtime_error("Document complexity exceeds editing limits");
}
void validate(const Body &b) {
    if (!b.id || b.name.size() > 1024)
        throw std::runtime_error("Invalid body identity or name");
    for (float c : b.color)
        if (!std::isfinite(c) || c < 0 || c > 1)
            throw std::runtime_error("Invalid material color");
    b.transform.validate();
    if (b.properties.size() > 128)
        throw std::runtime_error("Too many entity properties");
    for (const auto &[key, value] : b.properties) {
        if (key.empty() || key.size() > 128)
            throw std::runtime_error("Invalid property key");
        std::visit(
            [](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, double>) {
                    if (!std::isfinite(v))
                        throw std::runtime_error("Property number must be finite");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    if (v.size() > 2048)
                        throw std::runtime_error("Property string exceeds limit");
                }
            },
            value);
    }
    b.surface.validate();
}
} // namespace
Document::Document() {
    std::random_device random;
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        out << std::setw(8) << random();
    identity_ = out.str();
}
Id Document::addFace(const std::vector<std::vector<Vec3>> &loops, std::string name) {
    auto b = std::make_shared<Body>();
    b->id = nextId_;
    b->name = std::move(name);
    b->surface.addFace(loops);
    apply({"Draw face", {{b->id, nullptr, b}}}, revision_);
    return b->id;
}
void Document::extrude(Id id, Id face, double distance) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    b->surface.extrude(face, distance);
    apply({"Extrude face", {{id, old, b}}}, revision_);
}
void Document::move(Id id, Vec3 delta) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    checkPoint(delta);
    const auto localDelta =
        old->parent ? worldTransform(old->parent).inverse().vector(delta) : delta;
    b->transform = Transform::translation(localDelta) * old->transform;
    apply({"Move", {{id, old, b}}}, revision_);
}
void Document::paint(Id id, std::array<float, 3> color) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    b->color = color;
    apply({"Paint", {{id, old, b}}}, revision_);
}
Transform Document::worldTransform(Id id) const { return worldTransformIn(bodies_, id); }
std::vector<Triangle> Document::worldTriangles(Id id) const {
    auto triangles = bodies_.at(id)->surface.triangles();
    const auto world = worldTransform(id);
    for (auto &triangle : triangles) {
        triangle.a = world.point(triangle.a);
        triangle.b = world.point(triangle.b);
        triangle.c = world.point(triangle.c);
    }
    return triangles;
}
double Document::worldArea(Id id, Id face) const {
    if (!bodies_.at(id)->surface.faces.contains(face))
        throw std::runtime_error("Missing face");
    double area = 0;
    for (const auto &triangle : worldTriangles(id))
        if (triangle.face == face)
            area += length(cross(triangle.b - triangle.a, triangle.c - triangle.a)) * .5;
    return area;
}
void Document::transform(Id id, Transform local, Id parent) {
    auto old = bodies_.at(id);
    auto body = std::make_shared<Body>(*old);
    body->transform = local;
    body->parent = parent;
    apply({"Transform", {{id, old, body}}}, revision_);
}
void Document::erase(Id id) { apply({"Delete", {{id, bodies_.at(id), nullptr}}}, revision_); }
void Document::update(Edit edit, bool forward) {
    // Allocate into a temporary map before replacing authoritative state.
    auto next = bodies_;
    for (const auto &c : edit.changes) {
        auto p = forward ? c.after : c.before;
        if (p)
            next[c.id] = p;
        else
            next.erase(c.id);
    }
    bodies_.swap(next);
}
void Document::apply(Edit edit, std::uint64_t expected) {
    if (revision_ == UINT64_MAX || stateCounter_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    if (expected != revision_)
        throw std::runtime_error("STALE_REVISION: inspect current document before retrying");
    if (edit.changes.empty())
        throw std::runtime_error("Empty edit");
    std::set<Id> ids;
    Id next = std::max(nextId_, edit.nextIdFloor);
    edit.bytes = sizeof(Edit) + edit.label.size();
    for (const auto &c : edit.changes) {
        if (!c.id || !ids.insert(c.id).second || (!c.before && !c.after))
            throw std::runtime_error("Invalid change set");
        auto it = bodies_.find(c.id);
        if ((it == bodies_.end() ? nullptr : it->second) != c.before)
            throw std::runtime_error("Change precondition failed");
        if (!c.before && c.id < nextId_)
            throw std::runtime_error("Retired ID cannot be reused");
        if (c.after) {
            if (c.after->id != c.id || c.id == UINT64_MAX)
                throw std::runtime_error("Identity mismatch");
            validate(*c.after);
            next = std::max(next, c.id + 1);
        }
        edit.bytes += sizeof(Change) + bytes(c.before) + bytes(c.after);
    }
    if (edit.bytes > historyLimit)
        throw std::runtime_error("Edit exceeds the 64 MiB history budget");
    // Freeze caller-owned mutable records before storing them as const pointers.
    for (auto &c : edit.changes)
        if (c.after)
            c.after = std::make_shared<const Body>(*c.after);
    auto updated = bodies_;
    for (const auto &c : edit.changes) {
        if (c.after)
            updated[c.id] = c.after;
        else
            updated.erase(c.id);
    }
    validateDocumentSize(updated);
    History h{std::move(edit), state_, stateCounter_ + 1};
    undo_.push_back(h); // Allocation can still fail before any committed change.
    for (const auto &r : redo_)
        historyBytes_ -= r.edit.bytes;
    redo_.clear();
    historyBytes_ += h.edit.bytes;
    bodies_.swap(updated);
    nextId_ = next;
    state_ = ++stateCounter_;
    ++revision_;
    while (historyBytes_ > historyLimit && undo_.size() > 1) {
        historyBytes_ -= undo_.front().edit.bytes;
        undo_.pop_front();
    }
}
void Document::undo() {
    if (undo_.empty())
        return;
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    auto h = undo_.back();
    redo_.push_back(h);
    try {
        update(h.edit, false);
    } catch (...) {
        redo_.pop_back();
        throw;
    }
    undo_.pop_back();
    state_ = h.before;
    ++revision_;
}
void Document::redo() {
    if (redo_.empty())
        return;
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    auto h = redo_.back();
    undo_.push_back(h);
    try {
        update(h.edit, true);
    } catch (...) {
        undo_.pop_back();
        throw;
    }
    redo_.pop_back();
    state_ = h.after;
    ++revision_;
}
void Document::restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies,
                       std::uint64_t revision) {
    if (identity.size() != 32 ||
        !std::all_of(identity.begin(), identity.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !next || bodies.size() > 10000)
        throw std::runtime_error("Invalid document metadata");
    for (auto &[id, b] : bodies) {
        if (!b || id != b->id || id >= next)
            throw std::runtime_error("Invalid body ID allocator");
        validate(*b);
        b = std::make_shared<const Body>(*b);
    }
    validateDocumentSize(bodies);
    identity_ = std::move(identity);
    nextId_ = next;
    bodies_ = std::move(bodies);
    undo_.clear();
    redo_.clear();
    historyBytes_ = 0;
    revision_ = revision;
    state_ = ++stateCounter_;
    savedState_ = state_;
}
} // namespace sketchy
