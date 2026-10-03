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
    return n;
}
void validateDocumentSize(const std::map<Id, BodyPtr> &bodies) {
    size_t vertices = 0, faces = 0, wires = 0;
    for (const auto &[id, b] : bodies) {
        vertices += b->surface.vertices.size();
        faces += b->surface.faces.size();
        wires += b->surface.wires.size();
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
    b->surface.translate(delta);
    apply({"Move", {{id, old, b}}}, revision_);
}
void Document::paint(Id id, std::array<float, 3> color) {
    auto old = bodies_.at(id);
    auto b = std::make_shared<Body>(*old);
    b->color = color;
    apply({"Paint", {{id, old, b}}}, revision_);
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
void Document::restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies) {
    if (identity.empty() || identity.size() > 128 || !next || bodies.size() > 10000)
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
    ++revision_;
    state_ = ++stateCounter_;
    savedState_ = state_;
}
} // namespace sketchy
