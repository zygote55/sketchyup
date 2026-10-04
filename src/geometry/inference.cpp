#include "geometry/inference.hpp"
#include "geometry/constraints.hpp"
#include <algorithm>
#include <numeric>
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
InferenceIndex::Box InferenceIndex::bounds(Vec3 a, Vec3 b, Vec3 c) {
    return {{std::min({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}), std::min({a.z, b.z, c.z})},
            {std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}), std::max({a.z, b.z, c.z})}};
}
InferenceIndex::Box InferenceIndex::join(Box a, Box b) {
    return {
        {std::min(a.low.x, b.low.x), std::min(a.low.y, b.low.y), std::min(a.low.z, b.low.z)},
        {std::max(a.high.x, b.high.x), std::max(a.high.y, b.high.y), std::max(a.high.z, b.high.z)}};
}
std::vector<InferenceIndex::Node> InferenceIndex::build(std::vector<size_t> &order,
                                                        const std::vector<Box> &boxes) {
    order.resize(boxes.size());
    std::iota(order.begin(), order.end(), 0);
    std::vector<Node> nodes;
    std::function<size_t(size_t, size_t)> partition = [&](size_t first, size_t count) {
        const auto id = nodes.size();
        Box box = boxes[order[first]];
        for (size_t i = first + 1; i < first + count; ++i)
            box = join(box, boxes[order[i]]);
        nodes.push_back({box, first, count, 0, 0});
        if (count <= 8)
            return id;
        const auto extent = box.high - box.low;
        const auto axis = extent.x >= extent.y && extent.x >= extent.z ? 0
                          : extent.y >= extent.z                       ? 1
                                                                       : 2;
        auto center = [&](size_t i) {
            const auto p = boxes[i].low + boxes[i].high;
            return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        };
        const auto middle = first + count / 2;
        std::nth_element(order.begin() + first, order.begin() + middle,
                         order.begin() + first + count,
                         [&](size_t a, size_t b) { return center(a) < center(b); });
        const auto left = partition(first, count / 2), right = partition(middle, count - count / 2);
        nodes[id].count = 0;
        nodes[id].left = left;
        nodes[id].right = right;
        return id;
    };
    if (!boxes.empty())
        partition(0, boxes.size());
    return nodes;
}
void InferenceIndex::sync(const Document &doc, const std::function<bool()> &canceled) {
    if (identity_ == doc.identity() && revision_ == doc.revision() &&
        doc.isCurrentSnapshot(snapshot_))
        return;
    // Stage changed caches; failure leaves the previous snapshot usable.
    auto next = bodies_;
    if (identity_ != doc.identity())
        next.clear();
    std::erase_if(next, [&](const auto &p) { return !doc.bodies().contains(p.first); });
    size_t builds = 0;
    for (const auto &[id, body] : doc.bodies()) {
        if (canceled && canceled())
            throw std::runtime_error("Inference preparation canceled");
        const auto world = doc.worldTransform(id);
        if (next.contains(id) && next.at(id)->record == body && next.at(id)->world == world)
            continue;
        Cache cache;
        cache.record = body;
        cache.world = world;
        auto add = [&](InferenceKind kind, Id entity, Vec3 a, Vec3 b, Vec3 c) {
            cache.primitives.push_back({kind, entity, a, b, c, bounds(a, b, c)});
        };
        const auto adjacency = body->topology.adjacency(body->surface);
        cache.vertexEdges = adjacency.vertexEdges;
        for (const auto &[vertex, local] : body->surface.vertices) {
            const auto p = world.point(local);
            add(InferenceKind::Endpoint, vertex, p, p, p);
            if (adjacency.vertexEdges.contains(vertex) &&
                adjacency.vertexEdges.at(vertex).size() >= 3)
                add(InferenceKind::Intersection, vertex, p, p, p);
        }
        for (const auto &[edge, e] : body->topology.edges) {
            const auto a = world.point(body->surface.vertices.at(e.a)),
                       b = world.point(body->surface.vertices.at(e.b));
            const auto midpoint = (a + b) * .5;
            add(InferenceKind::Midpoint, edge, midpoint, midpoint, midpoint);
            add(InferenceKind::OnEdge, edge, a, b, b);
        }
        for (const auto &[curve, c] : body->curves) {
            const auto center = world.point(c.center);
            add(InferenceKind::Center, curve, center, center, center);
        }
        for (const auto &[id, guide] : body->guides) {
            const auto origin = world.point(guide.origin);
            if (guide.kind == GuideKind::Point)
                add(InferenceKind::GuidePoint, id, origin, origin, origin);
            else {
                const auto ends =
                    boundedGuideLine(guideLine(origin, world.vector(guide.direction)));
                add(InferenceKind::OnGuide, id, ends[0], ends[1], ends[1]);
            }
        }
        for (const auto &t : body->surface.triangles())
            add(InferenceKind::OnFace, t.face, world.point(t.a), world.point(t.b),
                world.point(t.c));
        std::vector<Box> boxes;
        boxes.reserve(cache.primitives.size());
        for (const auto &p : cache.primitives)
            boxes.push_back(p.box);
        cache.nodes = build(cache.order, boxes);
        if (!cache.nodes.empty())
            cache.box = cache.nodes[0].box;
        next[id] = std::make_shared<Cache>(std::move(cache));
        ++builds;
    }
    std::vector<Id> ids;
    std::vector<Box> boxes;
    for (const auto &[id, cache] : next)
        if (!cache->nodes.empty()) {
            ids.push_back(id);
            boxes.push_back(cache->box);
        }
    std::vector<size_t> order;
    auto nodes = build(order, boxes);
    bodies_ = std::move(next);
    bodyIds_ = std::move(ids);
    bodyOrder_ = std::move(order);
    nodes_ = std::move(nodes);
    snapshot_ = doc.saveStamp();
    identity_ = doc.identity();
    revision_ = doc.revision();
    bodyBuilds_ += builds;
}
size_t InferenceIndex::primitiveCount() const {
    size_t count = 0;
    for (const auto &[id, cache] : bodies_)
        count += cache->primitives.size();
    return count;
}
InferenceIndex::Planes InferenceIndex::frustum(const InferenceCamera &camera, double x, double y,
                                               double radius) {
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
bool InferenceIndex::intersects(Box box, const Planes &planes) {
    const auto center = (box.low + box.high) * .5, extent = (box.high - box.low) * .5;
    for (const auto &p : planes)
        if (p[0] * center.x + p[1] * center.y + p[2] * center.z + p[3] + std::abs(p[0]) * extent.x +
                std::abs(p[1]) * extent.y + std::abs(p[2]) * extent.z <
            -1e-10)
            return false;
    return true;
}
void InferenceIndex::visit(const Planes &planes, Id context,
                           const std::function<void(Id, const Primitive &)> &fn) const {
    auto walk = [&](const auto &nodes, const auto &order, auto leaf) {
        if (nodes.empty())
            return;
        std::vector<size_t> pending{0};
        while (!pending.empty()) {
            const auto &node = nodes[pending.back()];
            pending.pop_back();
            if (!intersects(node.box, planes))
                continue;
            if (node.count)
                for (size_t i = node.first; i < node.first + node.count; ++i)
                    leaf(order[i]);
            else {
                pending.push_back(node.right);
                pending.push_back(node.left);
            }
        }
    };
    walk(nodes_, bodyOrder_, [&](size_t index) {
        const auto id = bodyIds_[index];
        if (context && context != id)
            return;
        const auto &cache = *bodies_.at(id);
        walk(cache.nodes, cache.order, [&](size_t p) {
            if (intersects(cache.primitives[p].box, planes))
                fn(id, cache.primitives[p]);
        });
    });
}
InferenceResult InferenceIndex::query(const InferenceQuery &q) const {
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
    auto add = [&](InferenceKind kind, Vec3 p, Id body, Id entity, Id otherBody = 0,
                   Id otherEntity = 0, bool edgeIntersection = false, bool guide = false,
                   bool otherGuide = false) {
        if (plane && std::abs(dot(p - plane->origin, plane->normal)) > tolerance)
            return;
        const auto screen = q.camera.project(p);
        if (!screen)
            return;
        const auto distance = std::hypot(screen->x - q.x, screen->y - q.y);
        if (distance > q.radius)
            return;
        if (result.candidates.size() >= 4096) {
            result.truncated = true;
            return;
        }
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
    };
    struct Edge {
        Id body;
        const Primitive *p;
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
        const auto &cache = *bodies_.at(body);
        if (type == InferenceEntity::Vertex) {
            const auto found = cache.vertexEdges.find(p.entity);
            return found != cache.vertexEdges.end() &&
                   std::any_of(found->second.begin(), found->second.end(), [&](Id edge) {
                       return q.visible(body, InferenceEntity::Edge, edge);
                   });
        }
        if (type == InferenceEntity::Curve) {
            const auto &edges = cache.record->curves.at(p.entity).edges;
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
            if (edges.size() < 256)
                edges.push_back({body, &p});
            else
                result.truncated = true;
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
                const auto &cache = *bodies_.at(body);
                const auto &line = cache.record->guides.at(p.entity);
                if (const auto projected =
                        projectDirection({DirectionKind::Parallel, cache.world.point(line.origin),
                                          cache.world.vector(line.direction), body, p.entity,
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
    for (size_t i = 0; i < edges.size(); ++i)
        for (size_t j = i + 1; j < edges.size(); ++j) {
            ++result.intersectionPairs;
            const auto &a = *edges[i].p, &b = *edges[j].p;
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
    std::sort(result.candidates.begin(), result.candidates.end(), [](const auto &a, const auto &b) {
        return std::tie(a.kind, a.pixels, a.depth, a.body, a.entityType, a.entity, a.otherBody,
                        a.otherEntityType, a.otherEntity) <
               std::tie(b.kind, b.pixels, b.depth, b.body, b.entityType, b.entity, b.otherBody,
                        b.otherEntityType, b.otherEntity);
    });
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
        const auto [eye, ray] = q.camera.ray(screen.x, screen.y);
        const auto target = dot(candidate.point - eye, ray);
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
                        t && *t < target - std::max(tolerance, target * 1e-8))
                        occluded = true;
            });
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
} // namespace sketchy
