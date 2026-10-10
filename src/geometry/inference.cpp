#include "geometry/inference.hpp"
#include "geometry/inference_detail.hpp"
#include "geometry/constraints.hpp"
#include <algorithm>
#include <numeric>
#include <set>
namespace sketchy {
namespace {
std::array<double, 4> multiply(const std::array<double, 16> &m, std::array<double, 4> p) {
    std::array<double, 4> result{};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            result[r] += m[c * 4 + r] * p[c];
    return result;
}
std::optional<double> triangleHit(Vec3 origin, Vec3 direction, Vec3 a, Vec3 b, Vec3 c) {
    const auto e1 = b - a, e2 = c - a, h = cross(direction, e2);
    const auto det = dot(e1, h);
    if (std::abs(det) <= length(e1) * length(e2) * 1e-12)
        return {};
    const auto s = origin - a;
    const auto u = dot(s, h) / det;
    if (u < -1e-10 || u > 1 + 1e-10)
        return {};
    const auto q = cross(s, e1);
    const auto v = dot(direction, q) / det;
    if (v < -1e-10 || u + v > 1 + 1e-10)
        return {};
    const auto t = dot(e2, q) / det;
    if (t < 0)
        return {};
    return t;
}
bool samePoint(Vec3 a, Vec3 b) { return length(a - b) <= tolerance; }
} // namespace
std::optional<ScreenPoint> InferenceCamera::project(Vec3 point) const {
    const auto p = multiply(clipFromWorld, {point.x, point.y, point.z, 1});
    if (!std::isfinite(p[3]) || p[3] <= 0 || p[2] < -p[3] || p[2] > p[3])
        return {};
    return ScreenPoint{(p[0] / p[3] + 1) * width * .5, (1 - p[1] / p[3]) * height * .5, p[2] / p[3],
                       p[3]};
}
std::pair<Vec3, Vec3> InferenceCamera::ray(double x, double y) const {
    auto unproject = [&](double z) {
        const auto p = multiply(worldFromClip, {2 * x / width - 1, 1 - 2 * y / height, z, 1});
        if (!std::isfinite(p[3]) || p[3] == 0)
            throw std::runtime_error("Invalid inference camera inverse");
        return Vec3{p[0] / p[3], p[1] / p[3], p[2] / p[3]};
    };
    const auto a = unproject(-1), b = unproject(1), delta = b - a;
    const auto n = length(delta);
    if (!std::isfinite(n) || n == 0)
        throw std::runtime_error("Invalid inference camera ray");
    return {a, delta * (1 / n)};
}
const char *inferenceLabel(InferenceKind kind) {
    switch (kind) {
    case InferenceKind::GuidePoint:
        return "Guide point";
    case InferenceKind::OnGuide:
        return "On guide";
    case InferenceKind::Endpoint:
        return "Endpoint";
    case InferenceKind::Intersection:
        return "Intersection";
    case InferenceKind::Midpoint:
        return "Midpoint";
    case InferenceKind::Center:
        return "Center";
    case InferenceKind::OnEdge:
        return "On edge";
    case InferenceKind::OnFace:
        return "On face";
    }
    return "";
}
const char *inferenceEntityLabel(InferenceEntity entity) {
    switch (entity) {
    case InferenceEntity::Guide:
        return "guide";
    case InferenceEntity::Vertex:
        return "vertex";
    case InferenceEntity::Edge:
        return "edge";
    case InferenceEntity::Face:
        return "face";
    case InferenceEntity::Curve:
        return "curve";
    }
    return "";
}
namespace inference_detail {
Box bounds(Vec3 a, Vec3 b, Vec3 c) {
    return {{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})},
            {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})}};
}
Box join(Box a, Box b) {
    return {
        {std::min(a.low.x, b.low.x), std::min(a.low.y, b.low.y), std::min(a.low.z, b.low.z)},
        {std::max(a.high.x, b.high.x), std::max(a.high.y, b.high.y), std::max(a.high.z, b.high.z)}};
}
Planes frustum(const InferenceCamera &camera, double x, double y, double radius) {
    const auto left = 2 * (x - radius) / camera.width - 1,
               right = 2 * (x + radius) / camera.width - 1;
    const auto bottom = 1 - 2 * (y + radius) / camera.height,
               top = 1 - 2 * (y - radius) / camera.height;
    Planes planes{};
    for (int i = 0; i < 4; ++i) {
        const auto r0 = camera.clipFromWorld[i * 4], r1 = camera.clipFromWorld[i * 4 + 1],
                   r2 = camera.clipFromWorld[i * 4 + 2], r3 = camera.clipFromWorld[i * 4 + 3];
        planes[0][i] = r0 - left * r3;
        planes[1][i] = right * r3 - r0;
        planes[2][i] = r1 - bottom * r3;
        planes[3][i] = top * r3 - r1;
        planes[4][i] = r3 + r2;
        planes[5][i] = r3 - r2;
    }
    return planes;
}
bool intersects(Box box, const Planes &planes) {
    const auto center = (box.low + box.high) * .5, extent = (box.high - box.low) * .5;
    for (const auto &p : planes)
        if (p[0] * center.x + p[1] * center.y + p[2] * center.z + p[3] + std::abs(p[0]) * extent.x +
                std::abs(p[1]) * extent.y + std::abs(p[2]) * extent.z <
            -1e-10)
            return false;
    return true;
}
} // namespace inference_detail
using namespace inference_detail;
struct InferenceIndex::Local {
    BodyPtr source; // Geometry (surface, topology, curves) this index was built from.
    std::map<Id, std::vector<Id>> vertexEdges;
    std::vector<LocalPrimitive> primitives;
    std::vector<std::uint32_t> order;
    std::vector<Node> nodes;
};
struct InferenceIndex::Placement {
    BodyPtr record;
    Transform world;
    std::array<double, 9> magnitude{}; // |M_ij| at [i * 3 + j], M the linear part.
    std::shared_ptr<const Local> local;
    std::vector<Primitive> guides; // World space: bounded guide lines are not affine.
    Box box;
    bool empty{true};
};
namespace {
using Node = InferenceIndex::Node;
using LocalPrimitive = InferenceIndex::LocalPrimitive;
std::vector<Node> build(std::vector<std::uint32_t> &order, const std::vector<Box> &boxes) {
    order.resize(boxes.size());
    std::iota(order.begin(), order.end(), 0u);
    std::vector<Node> nodes;
    std::function<std::uint32_t(std::uint32_t, std::uint32_t)> partition =
        [&](std::uint32_t first, std::uint32_t count) {
            const auto id = std::uint32_t(nodes.size());
            Box box = boxes[order[first]];
            for (auto i = first + 1; i < first + count; ++i)
                box = join(box, boxes[order[i]]);
            nodes.push_back({box, first, count, 0, 0});
            if (count <= 8)
                return id;
            const auto extent = box.high - box.low;
            const auto axis = extent.x >= extent.y && extent.x >= extent.z ? 0
                              : extent.y >= extent.z                       ? 1
                                                                           : 2;
            auto center = [&](std::uint32_t i) {
                const auto p = boxes[i].low + boxes[i].high;
                return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
            };
            const auto middle = first + count / 2;
            std::nth_element(order.begin() + first, order.begin() + middle,
                             order.begin() + first + count,
                             [&](auto a, auto b) { return center(a) < center(b); });
            const auto left = partition(first, count / 2),
                       right = partition(middle, count - count / 2);
            nodes[id].count = 0;
            nodes[id].left = left;
            nodes[id].right = right;
            return id;
        };
    if (boxes.size() > UINT32_MAX)
        throw std::runtime_error("Inference index exceeds its primitive limit");
    if (!boxes.empty())
        partition(0, std::uint32_t(boxes.size()));
    return nodes;
}
// Inference reads only these fields of a body's geometry; guides stay per placement.
// Allocator floors (nextId) are ignored: a resolved instance record may carry higher
// floors than its definition member (see matchesComponentProjection).
bool sameGeometry(const Body &a, const Body &b) {
    return &a == &b ||
           (a.surface.vertices == b.surface.vertices && a.surface.faces == b.surface.faces &&
            a.surface.wires == b.surface.wires && a.topology.edges == b.topology.edges &&
            a.curves == b.curves);
}
std::shared_ptr<const InferenceIndex::Local> buildLocal(const BodyPtr &body) {
    auto local = std::make_shared<InferenceIndex::Local>();
    local->source = body;
    std::vector<Box> boxes;
    auto add = [&](InferenceKind kind, Id entity, Vec3 a, Vec3 b, Vec3 c) {
        local->primitives.push_back({kind, entity, a, b, c});
        boxes.push_back(bounds(a, b, c));
    };
    auto adjacency = body->topology.adjacency(body->surface);
    for (const auto &[vertex, p] : body->surface.vertices) {
        add(InferenceKind::Endpoint, vertex, p, p, p);
        if (adjacency.vertexEdges.contains(vertex) && adjacency.vertexEdges.at(vertex).size() >= 3)
            add(InferenceKind::Intersection, vertex, p, p, p);
    }
    for (const auto &[edge, e] : body->topology.edges) {
        const auto a = body->surface.vertices.at(e.a), b = body->surface.vertices.at(e.b);
        // World midpoints are taken between world endpoints; bound both endpoints.
        add(InferenceKind::Midpoint, edge, a, b, b);
        add(InferenceKind::OnEdge, edge, a, b, b);
    }
    for (const auto &[curve, c] : body->curves)
        add(InferenceKind::Center, curve, c.center, c.center, c.center);
    for (const auto &t : body->surface.triangles())
        add(InferenceKind::OnFace, t.face, t.a, t.b, t.c);
    local->vertexEdges = std::move(adjacency.vertexEdges);
    local->primitives.shrink_to_fit();
    local->nodes = build(local->order, boxes);
    return local;
}
// Exactly the world primitive a fully expanded index stores for this placement.
Primitive toWorld(const Transform &world, const LocalPrimitive &l) {
    switch (l.kind) {
    case InferenceKind::Midpoint: {
        const auto a = world.point(l.a), b = world.point(l.b);
        const auto midpoint = (a + b) * .5;
        return {l.kind, l.entity, midpoint, midpoint, midpoint, bounds(midpoint, midpoint, midpoint)};
    }
    case InferenceKind::OnEdge: {
        const auto a = world.point(l.a), b = world.point(l.b);
        return {l.kind, l.entity, a, b, b, bounds(a, b, b)};
    }
    case InferenceKind::OnFace: {
        const auto a = world.point(l.a), b = world.point(l.b), c = world.point(l.c);
        return {l.kind, l.entity, a, b, c, bounds(a, b, c)};
    }
    default: {
        const auto p = world.point(l.a);
        return {l.kind, l.entity, p, p, p, bounds(p, p, p)};
    }
    }
}
// Conservative world box of a transformed local box. The relative margin covers
// rounding in Transform::point, so every contained primitive's world box (and
// therefore every primitive the expanded index would visit) remains inside it.
Box worldBox(const InferenceIndex::Placement &placement, Box local) {
    const auto c = (local.low + local.high) * .5, e = (local.high - local.low) * .5;
    const auto center = placement.world.point(c);
    const std::array<double, 3> extent{e.x, e.y, e.z},
        magnitude{std::abs(c.x) + e.x, std::abs(c.y) + e.y, std::abs(c.z) + e.z};
    std::array<double, 3> radius{};
    for (int i = 0; i < 3; ++i) {
        double reach = 0, scale = std::abs(placement.world.m[12 + i]);
        for (int j = 0; j < 3; ++j) {
            reach += placement.magnitude[i * 3 + j] * extent[j];
            scale += placement.magnitude[i * 3 + j] * magnitude[j];
        }
        radius[i] = reach + 1e-12 * scale;
    }
    const Vec3 r{radius[0], radius[1], radius[2]};
    return {center - r, center + r};
}
} // namespace
void InferenceIndex::sync(const Document &doc, const std::function<bool()> &canceled) {
    if (identity_ == doc.identity() && revision_ == doc.revision() &&
        doc.isCurrentSnapshot(snapshot_))
        return;
    // Stage changed placements; failure leaves the previous snapshot usable.
    auto next = placements_;
    if (identity_ != doc.identity())
        next.clear();
    std::erase_if(next, [&](const auto &p) { return !doc.bodies().contains(p.first); });
    size_t builds = 0, locals = 0;
    // Resolved lazily, only when a changed record cannot keep its local index:
    // scene record -> canonical definition member, and local indexes by source.
    std::optional<std::map<Id, BodyPtr>> bound;
    std::map<const Body *, std::shared_ptr<const Local>> shared;
    auto sharedFor = [&](Id id, const Body &record) -> std::shared_ptr<const Local> {
        if (!bound) {
            bound.emplace();
            for (const auto &[root, instance] : doc.instances()) {
                const auto definition = doc.definitions().find(instance->definition);
                if (definition == doc.definitions().end())
                    continue;
                // A reference member's record is the nested instance root, bound
                // by that nested instance's own entry.
                for (const auto &[member, target] : instance->members)
                    if (const auto source = definition->second->members.find(member);
                        source != definition->second->members.end() &&
                        !definition->second->references.contains(member))
                        (*bound)[target] = source->second;
            }
            for (const auto &[placementId, placement] : next)
                shared.emplace(placement->local->source.get(), placement->local);
        }
        const auto found = bound->find(id);
        if (found == bound->end() || !sameGeometry(*found->second, record))
            return {};
        auto &slot = shared[found->second.get()];
        if (!slot) {
            slot = buildLocal(found->second);
            ++locals;
        }
        return slot;
    };
    for (const auto &[id, record] : doc.bodies()) {
        if (canceled && canceled())
            throw std::runtime_error("Inference preparation canceled");
        const auto world = doc.worldTransform(id);
        const auto found = next.find(id);
        const auto *previous = found == next.end() ? nullptr : found->second.get();
        if (previous && previous->record == record && previous->world == world)
            continue;
        std::shared_ptr<const Local> local;
        if (previous &&
            (previous->record == record || sameGeometry(*previous->local->source, *record)))
            local = previous->local;
        else
            local = sharedFor(id, *record);
        if (!local) {
            local = buildLocal(record);
            ++locals;
        }
        auto placement = std::make_shared<Placement>();
        placement->record = record;
        placement->world = world;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                placement->magnitude[i * 3 + j] = std::abs(world.m[j * 4 + i]);
        placement->local = local;
        for (const auto &[guideId, guide] : record->guides) {
            const auto origin = world.point(guide.origin);
            if (guide.kind == GuideKind::Point)
                placement->guides.push_back({InferenceKind::GuidePoint, guideId, origin, origin,
                                             origin, bounds(origin, origin, origin)});
            else {
                const auto ends =
                    boundedGuideLine(guideLine(origin, world.vector(guide.direction)));
                placement->guides.push_back({InferenceKind::OnGuide, guideId, ends[0], ends[1],
                                             ends[1], bounds(ends[0], ends[1], ends[1])});
            }
        }
        std::optional<Box> box;
        if (!local->nodes.empty())
            box = worldBox(*placement, local->nodes[0].box);
        for (const auto &guide : placement->guides)
            box = box ? join(*box, guide.box) : guide.box;
        placement->empty = !box;
        if (box)
            placement->box = *box;
        next[id] = std::move(placement);
        ++builds;
    }
    std::vector<std::pair<Id, const Placement *>> entries;
    std::vector<Box> boxes;
    size_t primitives = 0;
    for (const auto &[id, placement] : next) {
        primitives += placement->local->primitives.size() + placement->guides.size();
        if (!placement->empty) {
            entries.push_back({id, placement.get()});
            boxes.push_back(placement->box);
        }
    }
    std::vector<std::uint32_t> order;
    auto nodes = build(order, boxes);
    placements_ = std::move(next);
    entries_ = std::move(entries);
    entryOrder_ = std::move(order);
    nodes_ = std::move(nodes);
    snapshot_ = doc.saveStamp();
    identity_ = doc.identity();
    revision_ = doc.revision();
    bodyBuilds_ += builds;
    localBuilds_ += locals;
    primitiveCount_ = primitives;
}
size_t InferenceIndex::primitiveCount() const { return primitiveCount_; }
size_t InferenceIndex::indexedPrimitiveCount() const {
    std::set<const Local *> seen;
    size_t count = 0;
    for (const auto &[id, placement] : placements_) {
        count += placement->guides.size();
        if (seen.insert(placement->local.get()).second)
            count += placement->local->primitives.size();
    }
    return count;
}
size_t InferenceIndex::indexBytes() const {
    // Containers' payloads plus an estimate of ordered-map node overhead.
    constexpr size_t mapNode = 48;
    std::set<const Local *> seen;
    size_t bytes = sizeof(*this) + entries_.capacity() * sizeof(entries_[0]) +
                   entryOrder_.capacity() * sizeof(std::uint32_t) +
                   nodes_.capacity() * sizeof(Node);
    for (const auto &[id, placement] : placements_) {
        bytes += mapNode + sizeof(Id) + sizeof(placement) + sizeof(Placement) +
                 placement->guides.capacity() * sizeof(Primitive);
        if (!seen.insert(placement->local.get()).second)
            continue;
        const auto &local = *placement->local;
        bytes += sizeof(Local) + local.primitives.capacity() * sizeof(LocalPrimitive) +
                 local.order.capacity() * sizeof(std::uint32_t) +
                 local.nodes.capacity() * sizeof(Node);
        for (const auto &[vertex, edges] : local.vertexEdges)
            bytes += mapNode + sizeof(vertex) + sizeof(edges) + edges.capacity() * sizeof(Id);
    }
    return bytes;
}
void InferenceIndex::visit(const Planes &planes, Id context,
                           const std::function<void(Id, const Primitive &)> &fn) const {
    auto walk = [](const std::vector<Node> &nodes, const auto &test, const auto &leaf) {
        if (nodes.empty())
            return;
        // Median splits keep depth below log2(2^32 / 8) + 1, so at most one
        // pending sibling per level plus the current node fits.
        std::array<std::uint32_t, 64> pending;
        size_t size = 0;
        pending[size++] = 0;
        while (size) {
            const auto &node = nodes[pending[--size]];
            if (!test(node.box))
                continue;
            if (node.count)
                for (auto i = node.first; i < node.first + node.count; ++i)
                    leaf(i);
            else {
                pending[size++] = node.right;
                pending[size++] = node.left;
            }
        }
    };
    const auto inside = [&](Box box) { return intersects(box, planes); };
    walk(nodes_, inside, [&](std::uint32_t entry) {
        const auto &[id, placement] = entries_[entryOrder_[entry]];
        if (context && context != id)
            return;
        for (const auto &guide : placement->guides)
            if (intersects(guide.box, planes))
                fn(id, guide);
        const auto &local = *placement->local;
        walk(
            local.nodes,
            [&](Box box) { return intersects(worldBox(*placement, box), planes); },
            [&](std::uint32_t i) {
                const auto p = toWorld(placement->world, local.primitives[local.order[i]]);
                if (intersects(p.box, planes))
                    fn(id, p);
            });
    });
}
InferenceResult InferenceIndex::query(const InferenceQuery &q) const {
    return runQuery(
        q,
        [&](const Planes &planes, Id context, const auto &fn) { visit(planes, context, fn); },
        [&](Id id) {
            const auto &placement = *placements_.at(id);
            return BodyView{&placement.local->vertexEdges, placement.local->source.get(),
                            placement.record.get(), &placement.world};
        });
}
namespace inference_detail {
InferenceResult runQuery(const InferenceQuery &q, const Visitor &visit,
                         const std::function<BodyView(Id)> &bodyView) {
    if (!std::isfinite(q.camera.width) || !std::isfinite(q.camera.height) || q.camera.width <= 0 ||
        q.camera.height <= 0 || !std::isfinite(q.x) || !std::isfinite(q.y) ||
        !std::isfinite(q.radius) || q.radius <= 0 || q.radius > 64)
        throw std::runtime_error("Invalid inference viewport or acquisition radius");
    for (const auto *matrix : {&q.camera.clipFromWorld, &q.camera.worldFromClip})
        for (auto value : *matrix)
            if (!std::isfinite(value))
                throw std::runtime_error("Invalid inference camera");
    const auto plane = q.plane ? std::optional<DrawingPlane>{DrawingPlane::make(
                                     q.plane->origin, q.plane->normal, q.plane->xAxis)}
                               : std::nullopt;
    InferenceResult result;
    const auto [origin, direction] = q.camera.ray(q.x, q.y);
    // Candidate priority. Trailing point coordinates make the order total, so
    // results never depend on the order in which the index visits primitives.
    const auto priority = [](const InferenceCandidate &a, const InferenceCandidate &b) {
        return std::tie(a.kind, a.pixels, a.depth, a.body, a.entityType, a.entity, a.otherBody,
                        a.otherEntityType, a.otherEntity, a.point.x, a.point.y, a.point.z) <
               std::tie(b.kind, b.pixels, b.depth, b.body, b.entityType, b.entity, b.otherBody,
                        b.otherEntityType, b.otherEntity, b.point.x, b.point.y, b.point.z);
    };
    constexpr size_t candidateLimit = 4096, edgeLimit = 256;
    // Keep the best `limit` items by a total order. Pruning at twice the limit
    // bounds memory and still retains exactly the overall best items.
    const auto keep = [&](auto &items, size_t limit, const auto &less) {
        std::sort(items.begin(), items.end(), less);
        if (items.size() > limit) {
            items.resize(limit);
            result.truncated = true;
        }
    };
    auto add = [&](InferenceKind kind, Vec3 p, Id body, Id entity, Id otherBody = 0,
                   Id otherEntity = 0, bool edgeIntersection = false, bool guide = false,
                   bool otherGuide = false) {
        if (plane && std::abs(dot(p - plane->origin, plane->normal)) > tolerance)
            return;
        if (q.pointVisible && (!q.pointVisible(body, p) ||
                              (otherBody && !q.pointVisible(otherBody, p))))
            return;
        const auto screen = q.camera.project(p);
        if (!screen)
            return;
        const auto distance = std::hypot(screen->x - q.x, screen->y - q.y);
        if (distance > q.radius)
            return;
        const auto type =
            guide || kind == InferenceKind::GuidePoint || kind == InferenceKind::OnGuide
                ? InferenceEntity::Guide
            : kind == InferenceKind::OnFace ? InferenceEntity::Face
            : kind == InferenceKind::Center ? InferenceEntity::Curve
            : kind == InferenceKind::OnEdge || kind == InferenceKind::Midpoint || edgeIntersection
                ? InferenceEntity::Edge
                : InferenceEntity::Vertex;
        const auto otherType = otherGuide ? InferenceEntity::Guide : InferenceEntity::Edge;
        if (q.eligible && (!q.eligible(body, type, entity) ||
                           (otherBody && !q.eligible(otherBody, otherType, otherEntity))))
            return;
        result.candidates.push_back({kind, p, body, entity, otherBody, otherEntity, distance,
                                     screen->depth, type, otherType});
        if (result.candidates.size() >= 2 * candidateLimit)
            keep(result.candidates, candidateLimit, priority);
    };
    struct Edge {
        Id body;
        Primitive p;
    };
    const auto edgeOrder = [](const Edge &a, const Edge &b) {
        return std::tie(a.body, a.p.kind, a.p.entity) < std::tie(b.body, b.p.kind, b.p.entity);
    };
    auto typeOf = [](InferenceKind kind) {
        return kind == InferenceKind::OnFace ? InferenceEntity::Face
               : kind == InferenceKind::OnGuide || kind == InferenceKind::GuidePoint
                   ? InferenceEntity::Guide
               : kind == InferenceKind::OnEdge || kind == InferenceKind::Midpoint
                   ? InferenceEntity::Edge
               : kind == InferenceKind::Center ? InferenceEntity::Curve
                                               : InferenceEntity::Vertex;
    };
    auto visiblePrimitive = [&](Id body, const Primitive &p) {
        if (!q.visible)
            return true;
        const auto type = typeOf(p.kind);
        if (!q.visible(body, type, p.entity))
            return false;
        if (type == InferenceEntity::Vertex) {
            const auto &vertexEdges = *bodyView(body).vertexEdges;
            const auto found = vertexEdges.find(p.entity);
            return found != vertexEdges.end() &&
                   std::any_of(found->second.begin(), found->second.end(), [&](Id edge) {
                       return q.visible(body, InferenceEntity::Edge, edge);
                   });
        }
        if (type == InferenceEntity::Curve) {
            const auto &edges = bodyView(body).geometry->curves.at(p.entity).edges;
            return std::any_of(edges.begin(), edges.end(), [&](auto edge) {
                return q.visible(body, InferenceEntity::Edge, edge.edge);
            });
        }
        return true;
    };
    std::vector<Edge> edges;
    visit(frustum(q.camera, q.x, q.y, q.radius), q.context, [&](Id body, const Primitive &p) {
        ++result.visitedPrimitives;
        const bool guide = p.kind == InferenceKind::OnGuide || p.kind == InferenceKind::GuidePoint;
        if ((guide && !q.includeGuides) || !visiblePrimitive(body, p) ||
            (q.eligible && !q.eligible(body, typeOf(p.kind), p.entity)))
            return;
        if (p.kind == InferenceKind::OnFace) {
            if (auto t = triangleHit(origin, direction, p.a, p.b, p.c))
                add(p.kind, origin + direction * *t, body, p.entity);
        } else if (p.kind == InferenceKind::OnEdge || p.kind == InferenceKind::OnGuide) {
            edges.push_back({body, p});
            if (edges.size() >= 2 * edgeLimit)
                keep(edges, edgeLimit, edgeOrder);
            if (plane) {
                const auto a = dot(p.a - plane->origin, plane->normal),
                           b = dot(p.b - plane->origin, plane->normal);
                if (std::abs(a) > tolerance || std::abs(b) > tolerance) {
                    if (a * b <= 0 && std::abs(a - b) > 0)
                        add(InferenceKind::Intersection, p.a + (p.b - p.a) * (a / (a - b)), body,
                            p.entity, 0, 0, true, guide);
                    return;
                }
            }
            if (guide) {
                const auto view = bodyView(body);
                const auto &line = view.record->guides.at(p.entity);
                if (const auto projected =
                        projectDirection({DirectionKind::Parallel, view.world->point(line.origin),
                                          view.world->vector(line.direction), body, p.entity,
                                          InferenceEntity::Guide},
                                         q.camera, q.x, q.y))
                    add(p.kind, projected->point, body, p.entity);
                return;
            }
            double nearT = 0, farT = 1;
            const auto ca = multiply(q.camera.clipFromWorld, {p.a.x, p.a.y, p.a.z, 1});
            const auto cb = multiply(q.camera.clipFromWorld, {p.b.x, p.b.y, p.b.z, 1});
            for (int sign : {-1, 1}) {
                const auto fa = ca[3] + sign * ca[2], fb = cb[3] + sign * cb[2];
                if (fa < 0 && fb < 0)
                    return;
                if (fa < 0)
                    nearT = std::max(nearT, fa / (fa - fb));
                if (fb < 0)
                    farT = std::min(farT, fa / (fa - fb));
            }
            if (nearT > farT)
                return;
            const auto first = p.a + (p.b - p.a) * nearT, last = p.a + (p.b - p.a) * farT;
            const auto a = q.camera.project(first), b = q.camera.project(last);
            if (!a || !b)
                return;
            const auto dx = b->x - a->x, dy = b->y - a->y, denominator = dx * dx + dy * dy;
            if (denominator < 1e-16)
                return;
            const auto screenT =
                std::clamp(((q.x - a->x) * dx + (q.y - a->y) * dy) / denominator, 0., 1.);
            const auto t = (screenT / b->w) / ((1 - screenT) / a->w + screenT / b->w);
            add(p.kind, first + (last - first) * t, body, p.entity);
        } else
            add(p.kind, p.a, body, p.entity);
    });
    // Pair edges in a canonical order: the reported first/other identities and
    // any edge truncation are independent of spatial traversal order.
    keep(edges, edgeLimit, edgeOrder);
    for (size_t i = 0; i < edges.size(); ++i)
        for (size_t j = i + 1; j < edges.size(); ++j) {
            ++result.intersectionPairs;
            const auto &a = edges[i].p, &b = edges[j].p;
            const auto u = a.b - a.a, v = b.b - b.a, w = a.a - b.a;
            const auto aa = dot(u, u), bb = dot(u, v), cc = dot(v, v), dd = dot(u, w),
                       ee = dot(v, w), denom = aa * cc - bb * bb;
            if (denom <= aa * cc * 1e-12)
                continue;
            const auto s = (bb * ee - cc * dd) / denom, t = (aa * ee - bb * dd) / denom;
            if (s < 0 || s > 1 || t < 0 || t > 1)
                continue;
            const auto pa = a.a + u * s, pb = b.a + v * t;
            if (samePoint(pa, pb))
                add(InferenceKind::Intersection, pa, edges[i].body, a.entity, edges[j].body,
                    b.entity, true, a.kind == InferenceKind::OnGuide,
                    b.kind == InferenceKind::OnGuide);
        }
    keep(result.candidates, candidateLimit, priority);
    std::vector<InferenceCandidate> visible;
    size_t visibilityChecks = 0;
    for (const auto &candidate : result.candidates) {
        if (++visibilityChecks > 128) {
            result.truncated = true;
            break;
        }
        if (std::any_of(visible.begin(), visible.end(), [&](const auto &p) {
                return p.kind == candidate.kind && p.body == candidate.body &&
                       p.entity == candidate.entity && p.entityType == candidate.entityType &&
                       p.otherBody == candidate.otherBody &&
                       p.otherEntity == candidate.otherEntity &&
                       p.otherEntityType == candidate.otherEntityType &&
                       samePoint(p.point, candidate.point);
            }))
            continue;
        const auto screen = *q.camera.project(candidate.point);
        const auto [eye, cameraRay] = q.camera.ray(screen.x, screen.y);
        // A float viewport inverse can round-trip to a slightly different ray.
        // Test visibility toward the actual candidate, otherwise its own face
        // can intersect that displaced ray before the projected target distance.
        const auto delta = candidate.point - eye;
        const auto target = length(delta);
        const auto ray = target > 0 ? delta * (1 / target) : cameraRay;
        bool occluded = false;
        // Construction guides are a dotted overlay, visible through faces.
        // A mixed model/guide intersection still honors model occlusion.
        const bool overlay =
            candidate.entityType == InferenceEntity::Guide &&
            (!candidate.otherBody || candidate.otherEntityType == InferenceEntity::Guide);
        if (!overlay)
            visit(frustum(q.camera, screen.x, screen.y, .01), 0, [&](Id body, const Primitive &p) {
                ++result.visitedPrimitives;
                if (p.kind == InferenceKind::OnFace && visiblePrimitive(body, p))
                    if (auto t = triangleHit(eye, ray, p.a, p.b, p.c);
                        t && *t < target - std::max(tolerance, target * 1e-8) &&
                        (!q.pointVisible || q.pointVisible(body, eye + ray * *t)))
                        occluded = true;
            });
        if (!overlay && !occluded && q.extraOcclusion)
            occluded = q.extraOcclusion(eye, ray, target);
        if (!occluded)
            visible.push_back(candidate);
        if (visible.size() == 32) {
            result.truncated = true;
            break;
        }
    }
    result.candidates = std::move(visible);
    return result;
}
} // namespace inference_detail
} // namespace sketchy
