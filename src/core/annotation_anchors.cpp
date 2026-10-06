#include "core/annotation_anchors.hpp"
#include "core/model.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
const Body &body(const Document &doc, Id id) {
    require(doc.bodies().contains(id), "Annotation context does not exist");
    return *doc.bodies().at(id);
}
std::optional<std::array<double, 3>> coordinates(const Triangle &triangle, Vec3 p) {
    const auto a = triangle.b - triangle.a, b = triangle.c - triangle.a, d = p - triangle.a;
    const auto n = cross(a, b);
    const auto magnitude = length(n);
    if (magnitude <= tolerance * tolerance || std::abs(dot(d, n)) / magnitude > tolerance)
        return {};
    const auto determinant = dot(n, n);
    const auto v = dot(cross(d, b), n) / determinant, w = dot(cross(a, d), n) / determinant,
               u = 1 - v - w;
    const auto epsilon = tolerance / std::max({length(a), length(b), tolerance});
    if (std::min({u, v, w}) < -epsilon)
        return {};
    std::array<double, 3> result{std::max(0., u), std::max(0., v), std::max(0., w)};
    const auto sum = result[0] + result[1] + result[2];
    for (auto &weight : result)
        weight /= sum;
    return result;
}
std::optional<AnnotationAnchor> onFace(const Document &doc, Id owner, Id face, Vec3 point,
                                       size_t *budget = nullptr) {
    const auto &record = body(doc, owner);
    if (!record.surface.faces.contains(face))
        return {};
    const auto triangles = record.surface.triangulate(face);
    require(triangles.size() <= 32768, "Annotation face exceeds triangle budget");
    if (budget) {
        require(triangles.size() <= *budget, "Annotation lineage exceeds triangle-work budget");
        *budget -= triangles.size();
    }
    std::map<Vec3, Id, bool (*)(Vec3, Vec3)> ids(
        [](Vec3 a, Vec3 b) { return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z); });
    for (const auto &loop : record.surface.faces.at(face).loops)
        for (auto vertex : loop)
            ids.emplace(record.surface.vertices.at(vertex), vertex);
    for (const auto &triangle : triangles)
        if (const auto weights = coordinates(triangle, point)) {
            AnnotationAnchor result;
            result.kind = AnchorKind::Face;
            result.body = owner;
            result.entity = face;
            result.vertices = {ids.at(triangle.a), ids.at(triangle.b), ids.at(triangle.c)};
            result.weights = *weights;
            result.fallback = doc.worldTransform(owner).point(point);
            return result;
        }
    return {};
}
std::optional<Vec3> localPoint(const Body &record, const AnnotationAnchor &anchor) {
    const auto &surface = record.surface;
    if (anchor.kind == AnchorKind::Vertex) {
        if (surface.vertices.contains(anchor.entity))
            return surface.vertices.at(anchor.entity);
    } else if (anchor.kind == AnchorKind::Edge) {
        if (record.topology.edges.contains(anchor.entity)) {
            const auto &edge = record.topology.edges.at(anchor.entity);
            const auto a = surface.vertices.at(edge.a), b = surface.vertices.at(edge.b);
            return a + (b - a) * anchor.parameter;
        }
    } else if (anchor.kind == AnchorKind::Face) {
        Vec3 point;
        for (size_t i = 0; i < 3; ++i) {
            if (!surface.vertices.contains(anchor.vertices[i]))
                return {};
            point = point + surface.vertices.at(anchor.vertices[i]) * anchor.weights[i];
        }
        return point;
    }
    return {};
}
std::optional<double> onEdge(const Body &record, Id id, Vec3 point) {
    if (!record.topology.edges.contains(id))
        return {};
    const auto &edge = record.topology.edges.at(id);
    const auto a = record.surface.vertices.at(edge.a), b = record.surface.vertices.at(edge.b),
               v = b - a;
    const auto fraction = dot(point - a, v) / dot(v, v);
    const auto clamped = std::clamp(fraction, 0., 1.);
    if (length(a + v * clamped - point) > tolerance)
        return {};
    return clamped;
}
} // namespace
void validateAnnotationAnchor(const AnnotationAnchor &a) {
    checkPoint(a.fallback);
    require(a.state == AnchorState::Resolved || a.state == AnchorState::Missing ||
                a.state == AnchorState::Ambiguous,
            "Invalid annotation anchor state");
    require(a.kind == AnchorKind::Point || a.kind == AnchorKind::Vertex ||
                a.kind == AnchorKind::Edge || a.kind == AnchorKind::Face,
            "Invalid annotation anchor kind");
    require(std::isfinite(a.parameter) && a.parameter >= 0 && a.parameter <= 1,
            "Invalid annotation edge fraction");
    if (a.kind == AnchorKind::Point)
        require(!a.body && !a.entity && a.state == AnchorState::Resolved,
                "Fixed annotation point has no geometric identity");
    else
        require(a.body && a.entity, "Geometric annotation anchor needs context and entity IDs");
    if (a.kind != AnchorKind::Edge)
        require(a.parameter == 0, "Only edge anchors carry a fraction");
    if (a.kind != AnchorKind::Face)
        require(a.vertices == std::array<Id, 3>{} && a.weights == std::array<double, 3>{},
                "Only face anchors carry support coordinates");
    else {
        require(a.vertices[0] && a.vertices[1] && a.vertices[2] &&
                    std::set<Id>(a.vertices.begin(), a.vertices.end()).size() == 3,
                "Face anchor needs three distinct support vertices");
        double sum{};
        for (auto weight : a.weights) {
            require(std::isfinite(weight) && weight >= 0 && weight <= 1,
                    "Invalid face anchor weight");
            sum += weight;
        }
        require(std::abs(sum - 1) < 1e-8, "Face anchor weights must sum to one");
    }
}
AnnotationAnchor pointAnchor(Vec3 point) {
    AnnotationAnchor result;
    result.fallback = point;
    validateAnnotationAnchor(result);
    return result;
}
AnnotationAnchor vertexAnchor(const Document &doc, Id owner, Id vertex) {
    const auto &record = body(doc, owner);
    require(record.surface.vertices.contains(vertex), "Annotation vertex does not exist");
    AnnotationAnchor result;
    result.kind = AnchorKind::Vertex;
    result.body = owner;
    result.entity = vertex;
    result.fallback = doc.worldTransform(owner).point(record.surface.vertices.at(vertex));
    validateAnnotationAnchor(result);
    return result;
}
AnnotationAnchor edgeAnchor(const Document &doc, Id owner, Id edge, double fraction) {
    AnnotationAnchor result;
    result.kind = AnchorKind::Edge;
    result.body = owner;
    result.entity = edge;
    result.parameter = fraction;
    validateAnnotationAnchor(result);
    const auto point = localPoint(body(doc, owner), result);
    require(point.has_value(), "Annotation edge does not exist");
    result.fallback = doc.worldTransform(owner).point(*point);
    validateAnnotationAnchor(result);
    return result;
}
AnnotationAnchor faceAnchor(const Document &doc, Id owner, Id face, Vec3 local) {
    checkPoint(local);
    const auto result = onFace(doc, owner, face, local);
    require(result.has_value(), "Annotation point must lie on its face");
    validateAnnotationAnchor(*result);
    return *result;
}
ResolvedAnchor resolveAnnotationAnchor(const Document &doc, const AnnotationAnchor &anchor) {
    validateAnnotationAnchor(anchor);
    if (anchor.kind == AnchorKind::Point || anchor.state != AnchorState::Resolved)
        return {anchor.fallback, anchor.state};
    if (!doc.bodies().contains(anchor.body))
        return {anchor.fallback, AnchorState::Missing};
    const auto point = localPoint(*doc.bodies().at(anchor.body), anchor);
    if (!point ||
        (anchor.kind == AnchorKind::Face && !onFace(doc, anchor.body, anchor.entity, *point)))
        return {anchor.fallback, AnchorState::Missing};
    const auto world = doc.worldTransform(anchor.body).point(*point);
    checkPoint(world);
    return {world, AnchorState::Resolved};
}
AnnotationAnchor remapAnnotationAnchor(const Document &before, const Document &after,
                                       const std::map<Id, TopologyChanges> &changes,
                                       const AnnotationAnchor &anchor) {
    validateAnnotationAnchor(anchor);
    if (anchor.kind == AnchorKind::Point || anchor.state != AnchorState::Resolved)
        return anchor;
    require(before.identity() == after.identity(),
            "Annotation remap requires the same document identity");
    auto result = anchor;
    const auto previous = resolveAnnotationAnchor(before, anchor);
    result.fallback = previous.point;
    if (previous.state != AnchorState::Resolved || !after.bodies().contains(anchor.body)) {
        result.state = AnchorState::Missing;
        return result;
    }
    std::vector<Id> descendants{anchor.entity};
    if (const auto report = changes.find(anchor.body); report != changes.end()) {
        const auto &entities = anchor.kind == AnchorKind::Vertex ? report->second.vertices
                               : anchor.kind == AnchorKind::Edge ? report->second.edges
                                                                 : report->second.faces;
        if (const auto found = entities.descendants.find(anchor.entity);
            found != entities.descendants.end())
            descendants = found->second;
        else if (std::find(entities.deleted.begin(), entities.deleted.end(), anchor.entity) !=
                 entities.deleted.end())
            descendants.clear();
    }
    require(descendants.size() <= 32768, "Annotation lineage exceeds descendant budget");
    const auto &record = *after.bodies().at(anchor.body);
    const auto &oldRecord = *before.bodies().at(anchor.body);
    if (descendants == std::vector<Id>{anchor.entity} && record.surface == oldRecord.surface &&
        record.topology == oldRecord.topology) {
        const auto resolved = resolveAnnotationAnchor(after, anchor);
        result.fallback = resolved.point;
        result.state = resolved.state;
        return result;
    }
    std::vector<AnnotationAnchor> matches;
    size_t budget = 262144;
    require(std::set<Id>(descendants.begin(), descendants.end()).size() == descendants.size(),
            "Annotation lineage contains duplicate descendants");
    auto supportState = AnchorState::Missing;
    auto remappedVertex = [&](Id vertex) -> std::optional<Vec3> {
        if (const auto report = changes.find(anchor.body); report != changes.end()) {
            const auto &vertices = report->second.vertices;
            if (const auto mapped = vertices.descendants.find(vertex);
                mapped != vertices.descendants.end()) {
                if (mapped->second.size() != 1) {
                    if (mapped->second.size() > 1)
                        supportState = AnchorState::Ambiguous;
                    return {};
                }
                vertex = mapped->second.front();
            } else if (std::find(vertices.deleted.begin(), vertices.deleted.end(), vertex) !=
                       vertices.deleted.end())
                return {};
        }
        if (!record.surface.vertices.contains(vertex))
            return {};
        return record.surface.vertices.at(vertex);
    };
    Vec3 target;
    if (anchor.kind == AnchorKind::Edge) {
        const auto &edge = oldRecord.topology.edges.at(anchor.entity);
        const auto a = remappedVertex(edge.a), b = remappedVertex(edge.b);
        if (!a || !b) {
            result.state = supportState;
            return result;
        }
        target = *a + (*b - *a) * anchor.parameter;
    } else if (anchor.kind == AnchorKind::Face) {
        for (size_t i = 0; i < 3; ++i) {
            const auto vertex = remappedVertex(anchor.vertices[i]);
            if (!vertex) {
                result.state = supportState;
                return result;
            }
            target = target + *vertex * anchor.weights[i];
        }
    }
    for (const auto id : descendants) {
        if (anchor.kind == AnchorKind::Vertex) {
            if (record.surface.vertices.contains(id))
                matches.push_back(vertexAnchor(after, anchor.body, id));
        } else if (anchor.kind == AnchorKind::Edge) {
            if (descendants.size() == 1 && id == anchor.entity &&
                record.topology.edges.contains(id))
                matches.push_back(edgeAnchor(after, anchor.body, id, anchor.parameter));
            else if (const auto fraction = onEdge(record, id, target))
                matches.push_back(edgeAnchor(after, anchor.body, id, *fraction));
        } else if (const auto face = onFace(after, anchor.body, id, target, &budget))
            matches.push_back(*face);
    }
    if (matches.size() == 1)
        return matches.front();
    result.state = matches.empty() ? AnchorState::Missing : AnchorState::Ambiguous;
    return result;
}
} // namespace sketchy
