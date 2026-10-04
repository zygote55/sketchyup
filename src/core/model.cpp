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
               b->surface.wires.size() * sizeof(std::array<Id, 2>) +
               b->topology.edges.size() * (sizeof(EdgeRecord) + 64);
    for (const auto &[id, curve] : b->curves)
        n += sizeof(Curve) + 64 + curve.edges.size() * sizeof(OrientedEdge);
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
    size_t vertices = 0, faces = 0, wires = 0, edges = 0, curves = 0;
    for (const auto &[id, b] : bodies) {
        vertices += b->surface.vertices.size();
        faces += b->surface.faces.size();
        wires += b->surface.wires.size();
        edges += b->topology.edges.size();
        curves += b->curves.size();
        const auto world = worldTransformIn(bodies, id);
        for (const auto &[vertex, point] : b->surface.vertices)
            checkPoint(world.point(point));
    }
    if (bodies.size() > 10000 || vertices > 100000 || faces > 100000 || wires > 100000 ||
        edges > Topology::edgeLimit || curves > 10000)
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
Id Document::addWire(Id context, Vec3 a, Vec3 b) {
    BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = "Edges";
    }
    const auto first = body->surface.vertex(a), second = body->surface.vertex(b);
    if (first == second)
        throw std::runtime_error("Edge endpoints coincide");
    body->surface.wires.push_back({first, second});
    apply({"Draw edge", {{body->id, old, body}}}, revision_);
    return body->id;
}
ChangeReport Document::insertEdges(Id context, Vec3 origin, Vec3 normal,
                                   const std::vector<std::array<Vec3, 2>> &edges,
                                   std::string name) {
    BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = std::move(name);
    }
    auto result = insertPlanarEdges(body->surface, origin, normal, edges);
    if (old && result.surface == old->surface)
        return {};
    body->surface = std::move(result.surface);
    return apply({"Insert planar edges", {{body->id, old, body, std::move(result.faces)}}},
                 revision_);
}
ChangeReport Document::addCurve(Id context, Curve curve) {
    const auto chords = curve.chords();
    BodyPtr old = context ? bodies_.at(context) : nullptr;
    auto body = old ? std::make_shared<Body>(*old) : std::make_shared<Body>();
    if (!old) {
        body->id = nextId_;
        body->name = curve.kind == CurveKind::Circle ? "Circle"
                     : curve.kind == CurveKind::Pie  ? "Pie"
                                                     : "Arc";
    }
    const auto normal =
        DrawingPlane::make(curve.center, cross(curve.xAxis, curve.yAxis), curve.xAxis).normal;
    auto result = insertPlanarEdges(body->surface, curve.center, normal, chords);
    body->surface = std::move(result.surface);
    body->topology = Topology::rebuild(body->surface, body->topology);
    size_t budget = 1000000;
    if (!bindCurve(curve, body->surface, body->topology, budget))
        throw std::runtime_error("Curve outline could not be associated with editable edges");
    if (body->surface.nextId == UINT64_MAX)
        throw std::runtime_error("Curve identity space exhausted");
    body->curves.emplace(body->surface.nextId++, std::move(curve));
    return apply({"Draw curve", {{body->id, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::splitEdge(Id context, Id edge, double fraction) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    sketchy::splitEdge(body->surface, old->topology.edges.at(edge), fraction);
    return apply({"Split edge", {{context, old, body}}}, revision_);
}
ChangeReport Document::eraseFace(Id context, Id face) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = sketchy::eraseFace(old->surface, face);
    body->surface = std::move(result.surface);
    return apply({"Erase face", {{context, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::eraseEdge(Id context, Id edge) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = sketchy::eraseEdge(old->surface, old->topology, edge);
    body->surface = std::move(result.surface);
    return apply({"Erase edge", {{context, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::healFace(Id context, Id edge, Vec3 origin, Vec3 normal) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    if (!old->topology.edges.contains(edge))
        throw std::runtime_error("Healing boundary edge does not exist");
    const auto boundary = old->topology.edges.at(edge);
    auto result = insertPlanarEdges(
        old->surface, origin, normal,
        {{old->surface.vertices.at(boundary.a), old->surface.vertices.at(boundary.b)}}, true);
    if (result.surface.faces.size() <= old->surface.faces.size())
        throw PlanarError("NO_CLOSED_REGION",
                          "This edge does not bound a missing closed planar face");
    body->surface = std::move(result.surface);
    return apply({"Heal face", {{context, old, body, std::move(result.faces)}}}, revision_);
}
ChangeReport Document::cleanup(Id context) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = cleanupCoincident(old->surface, old->topology);
    if (result.surface == old->surface)
        return {};
    body->surface = std::move(result.surface);
    return apply({"Merge coincident topology",
                  {{context, old, body, std::move(result.faces), std::move(result.vertices),
                    std::move(result.edges)}}},
                 revision_);
}
ChangeReport Document::pushPull(Id context, Id face, double distance) {
    const auto old = bodies_.at(context);
    auto body = std::make_shared<Body>(*old);
    auto result = sketchy::pushPull(old->surface, face, distance);
    body->surface = std::move(result.surface);
    return apply({"Push/pull face", {{context, old, body, std::move(result.faces)}}}, revision_);
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
    const auto world = worldTransform(id);
    double area = 0;
    for (const auto &triangle : bodies_.at(id)->surface.triangulate(face)) {
        const auto a = world.point(triangle.a), b = world.point(triangle.b),
                   c = world.point(triangle.c);
        area += length(cross(b - a, c - a)) * .5;
    }
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
        if (p) {
            const auto floor =
                surfaceFloors_.contains(c.id) ? surfaceFloors_.at(c.id) : p->surface.nextId;
            const auto edgeFloor =
                edgeFloors_.contains(c.id) ? edgeFloors_.at(c.id) : p->topology.nextId;
            if (p->surface.nextId < floor || p->topology.nextId < edgeFloor) {
                auto restored = std::make_shared<Body>(*p);
                restored->surface.nextId = floor;
                restored->topology.nextId = edgeFloor;
                next[c.id] = std::move(restored);
            } else
                next[c.id] = p;
        } else
            next.erase(c.id);
    }
    bodies_.swap(next);
}
ChangeReport Document::apply(Edit edit, std::uint64_t expected) {
    if (revision_ == UINT64_MAX)
        throw std::runtime_error("Document revision space exhausted");
    if (expected != revision_)
        throw std::runtime_error("STALE_REVISION: inspect current document before retrying");
    if (edit.changes.empty())
        throw std::runtime_error("Empty edit");
    std::set<Id> ids;
    Id next = std::max(nextId_, edit.nextIdFloor);
    auto floors = surfaceFloors_;
    auto edgeFloors = edgeFloors_;
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
            const auto floor = floors.contains(c.id) ? floors.at(c.id) : Id{1};
            for (const auto &[id, vertex] : c.after->surface.vertices)
                if (id < floor && (!c.before || !c.before->surface.vertices.contains(id)))
                    throw std::runtime_error("Retired vertex ID cannot be reused");
            for (const auto &[id, face] : c.after->surface.faces)
                if (id < floor && (!c.before || !c.before->surface.faces.contains(id)))
                    throw std::runtime_error("Retired face ID cannot be reused");
            for (const auto &[id, curve] : c.after->curves)
                if (id < floor && (!c.before || !c.before->curves.contains(id)))
                    throw std::runtime_error("Retired curve ID cannot be reused");
            floors[c.id] = std::max(floor, c.after->surface.nextId);
            next = std::max(next, c.id + 1);
        }
        edit.bytes += sizeof(Change) + bytes(c.before) + bytes(c.after);
    }
    // Freeze caller-owned mutable records before storing them as const pointers.
    for (auto &c : edit.changes)
        if (c.after) {
            auto frozen = std::make_shared<Body>(*c.after);
            frozen->surface.nextId = floors.at(c.id);
            const auto edgeFloor = edgeFloors.contains(c.id) ? edgeFloors.at(c.id) : Id{1};
            bool indexed = true;
            try {
                frozen->topology.validate(frozen->surface);
            } catch (const std::runtime_error &) {
                indexed = false;
            }
            if (!indexed)
                frozen->topology =
                    Topology::rebuild(frozen->surface, c.before ? c.before->topology : Topology{},
                                      std::max(edgeFloor, frozen->topology.nextId));
            for (const auto &[id, edge] : frozen->topology.edges) {
                if (c.before && c.before->topology.edges.contains(id)) {
                    const auto &previous = c.before->topology.edges.at(id);
                    if (edge.a != previous.a || edge.b != previous.b)
                        throw std::runtime_error(
                            "An edge ID cannot be reassigned to unrelated endpoints");
                } else if (id < edgeFloor)
                    throw std::runtime_error("Retired edge ID cannot be reused");
            }
            frozen->topology.nextId = std::max(frozen->topology.nextId, edgeFloor);
            frozen->topology.validate(frozen->surface);
            // Ordinary geometry edits retain the analytic record only while its
            // complete original outline still exists, including split chords.
            size_t curveBudget = 1000000;
            for (auto it = frozen->curves.begin(); it != frozen->curves.end();) {
                const bool existing = c.before && c.before->curves.contains(it->first) &&
                                      c.before->curves.at(it->first) == it->second;
                if (existing &&
                    !bindCurve(it->second, frozen->surface, frozen->topology, curveBudget))
                    it = frozen->curves.erase(it);
                else
                    ++it;
            }
            validateCurves(frozen->curves, frozen->surface, frozen->topology);
            edgeFloors[c.id] = frozen->topology.nextId;
            c.after = std::move(frozen);
        }
    edit.bytes = sizeof(Edit) + edit.label.size();
    ChangeReport report;
    const Surface emptySurface;
    const Topology emptyTopology;
    for (const auto &change : edit.changes) {
        edit.bytes += sizeof(Change) + bytes(change.before) + bytes(change.after);
        for (const auto *mapping :
             {&change.faceDescendants, &change.vertexDescendants, &change.edgeDescendants})
            for (const auto &[id, targets] : *mapping)
                edit.bytes += 96 + targets.size() * sizeof(Id);
        auto changes = compareTopology(change.before ? change.before->surface : emptySurface,
                                       change.before ? change.before->topology : emptyTopology,
                                       change.after ? change.after->surface : emptySurface,
                                       change.after ? change.after->topology : emptyTopology,
                                       change.edgeDescendants.empty());
        auto mapEntities = [&](const auto &mapping, const auto &before, const auto &after,
                               EntityChanges &entities) {
            for (const auto &[old, descendants] : mapping) {
                if (!before.contains(old))
                    throw std::runtime_error("Topology lineage source does not exist");
                std::set<Id> unique;
                for (auto id : descendants)
                    if (!after.contains(id) || !unique.insert(id).second)
                        throw std::runtime_error("Invalid topology lineage target");
                entities.descendants[old] = descendants;
            }
        };
        mapEntities(change.faceDescendants,
                    change.before ? change.before->surface.faces : emptySurface.faces,
                    change.after ? change.after->surface.faces : emptySurface.faces, changes.faces);
        mapEntities(change.vertexDescendants,
                    change.before ? change.before->surface.vertices : emptySurface.vertices,
                    change.after ? change.after->surface.vertices : emptySurface.vertices,
                    changes.vertices);
        mapEntities(change.edgeDescendants,
                    change.before ? change.before->topology.edges : emptyTopology.edges,
                    change.after ? change.after->topology.edges : emptyTopology.edges,
                    changes.edges);
        const std::map<Id, Curve> noCurves;
        changes.curves = compareCurves(change.before ? change.before->curves : noCurves,
                                       change.after ? change.after->curves : noCurves);
        report.emplace(change.id, std::move(changes));
    }
    if (edit.bytes > historyLimit)
        throw std::runtime_error("Edit exceeds the 64 MiB history budget");
    auto updated = bodies_;
    for (const auto &c : edit.changes) {
        if (c.after)
            updated[c.id] = c.after;
        else
            updated.erase(c.id);
    }
    validateDocumentSize(updated);
    // Keep floors for live contexts and contexts reachable from retained history.
    // Redo is about to be discarded. Floors of permanently retired bodies are unnecessary.
    std::set<Id> reachable;
    for (const auto &[id, body] : updated)
        reachable.insert(id);
    for (const auto &entry : undo_)
        for (const auto &change : entry.edit.changes)
            reachable.insert(change.id);
    for (const auto &change : edit.changes)
        reachable.insert(change.id);
    std::erase_if(floors, [&](const auto &entry) { return !reachable.contains(entry.first); });
    std::erase_if(edgeFloors, [&](const auto &entry) { return !reachable.contains(entry.first); });
    History h{std::move(edit), state_, std::make_shared<State>()};
    undo_.push_back(h); // Allocation can still fail before any committed change.
    for (const auto &r : redo_)
        historyBytes_ -= r.edit.bytes;
    redo_.clear();
    historyBytes_ += h.edit.bytes;
    bodies_.swap(updated);
    nextId_ = next;
    surfaceFloors_.swap(floors);
    edgeFloors_.swap(edgeFloors);
    state_ = h.after;
    ++revision_;
    while (historyBytes_ > historyLimit && undo_.size() > 1) {
        historyBytes_ -= undo_.front().edit.bytes;
        undo_.pop_front();
    }
    return report;
}
Document::AmendStamp Document::amendmentStamp() const {
    AmendStamp stamp;
    if (!undo_.empty() && undo_.back().after == state_) {
        stamp.session = session_;
        stamp.state = state_;
        stamp.revision = revision_;
    }
    return stamp;
}
bool Document::canAmend(const AmendStamp &stamp) const {
    return stamp.session == session_ && stamp.state == state_ && stamp.revision == revision_ &&
           !undo_.empty() && undo_.back().after == state_;
}
ChangeReport Document::amendLast(const AmendStamp &stamp,
                                 const std::function<void(Document &)> &replace) {
    if (!canAmend(stamp))
        throw std::runtime_error("The most recent operation can no longer be revised");
    std::set<Id> contexts;
    size_t createdContexts = 0;
    for (const auto &change : undo_.back().edit.changes) {
        contexts.insert(change.id);
        if (!change.before && change.after)
            ++createdContexts;
    }
    Document staged = *this;
    staged.undo();
    const auto baseline = staged.bodies_;
    // Rewind only the private candidate. A replacement publishes one revision,
    // and retains the pre-operation history entry and monotonic allocator floors.
    staged.revision_ = revision_;
    replace(staged);
    if (staged.identity_ != identity_ || staged.session_ != session_ ||
        staged.revision_ != revision_ + 1 || staged.undo_.empty() || staged.state_ == state_)
        throw std::runtime_error("Replacement must commit exactly one atomic operation");
    for (const auto &[id, body] : baseline)
        if (!contexts.contains(id) &&
            (!staged.bodies_.contains(id) || staged.bodies_.at(id) != body))
            throw std::runtime_error("Replacement cannot change another editing context");
    size_t newContexts = 0;
    for (const auto &[id, body] : staged.bodies_)
        if (!baseline.contains(id))
            ++newContexts;
    if (newContexts != createdContexts)
        throw std::runtime_error(
            "Replacement must preserve the operation's context creation count");
    ChangeReport report;
    std::set<Id> ids;
    for (const auto &[id, body] : bodies_)
        ids.insert(id);
    for (const auto &[id, body] : staged.bodies_)
        ids.insert(id);
    const Body empty;
    for (auto id : ids) {
        auto before = bodies_.contains(id) ? bodies_.at(id) : nullptr;
        auto after = staged.bodies_.contains(id) ? staged.bodies_.at(id) : nullptr;
        if (before != after) {
            report.emplace(id, compareTopology(before ? before->surface : empty.surface,
                                               before ? before->topology : empty.topology,
                                               after ? after->surface : empty.surface,
                                               after ? after->topology : empty.topology));
            report.at(id).curves = compareCurves(before ? before->curves : empty.curves,
                                                 after ? after->curves : empty.curves);
        }
    }
    *this = std::move(staged);
    return report;
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
Document::SaveStamp Document::saveStamp() const {
    SaveStamp stamp;
    stamp.session = session_;
    stamp.state = state_;
    return stamp;
}
bool Document::markSaved(const SaveStamp &stamp) {
    if (!owns(stamp))
        return false;
    savedState_ = stamp.state;
    return true;
}
void Document::restore(std::string identity, Id next, std::map<Id, BodyPtr> bodies,
                       std::uint64_t revision) {
    if (identity.size() != 32 ||
        !std::all_of(identity.begin(), identity.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        !next || bodies.size() > 10000)
        throw std::runtime_error("Invalid document metadata");
    std::map<Id, Id> floors, edgeFloors;
    for (auto &[id, b] : bodies) {
        if (!b || id != b->id || id >= next)
            throw std::runtime_error("Invalid body ID allocator");
        validate(*b);
        auto restored = std::make_shared<Body>(*b);
        if (restored->topology.edges.empty() && restored->topology.nextId == 1)
            restored->topology = Topology::rebuild(restored->surface, {});
        restored->topology.validate(restored->surface);
        validateCurves(restored->curves, restored->surface, restored->topology);
        edgeFloors.emplace(id, restored->topology.nextId);
        b = std::move(restored);
        floors.emplace(id, b->surface.nextId);
    }
    validateDocumentSize(bodies);
    auto fresh = std::make_shared<State>();
    auto session = std::make_shared<State>();
    identity_ = std::move(identity);
    nextId_ = next;
    bodies_ = std::move(bodies);
    surfaceFloors_ = std::move(floors);
    edgeFloors_ = std::move(edgeFloors);
    undo_.clear();
    redo_.clear();
    historyBytes_ = 0;
    revision_ = revision;
    session_ = std::move(session);
    state_ = std::move(fresh);
    savedState_ = state_;
}
} // namespace sketchy
