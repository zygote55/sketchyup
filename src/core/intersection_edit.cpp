#include "core/intersection_edit.hpp"
#include "core/appearance.hpp"
#include "geometry/intersection.hpp"
#include <algorithm>
namespace sketchy {
namespace {
[[noreturn]] void fail(const char *code, const char *message) {
    throw IntersectionError(code, message);
}
struct Sample {
    SelectedEntity entity;
    Surface surface;
    Vec3 low, high;
};
Sample sample(const Document &doc, SelectedEntity entity, size_t &vertices) {
    const auto &body = *doc.bodies().at(entity.body);
    Sample result;
    result.entity = entity;
    result.surface.faces.emplace(entity.entity, body.surface.faces.at(entity.entity));
    const auto world = doc.worldTransform(entity.body);
    for (const auto &loop : result.surface.faces.at(entity.entity).loops)
        for (auto vertex : loop) {
            if (++vertices > 16384)
                fail("INTERSECTION_LIMIT",
                     "Intersection references exceed 16384 boundary vertices; narrow the scope");
            result.surface.vertices[vertex] = world.point(body.surface.vertices.at(vertex));
        }
    result.low = result.high = result.surface.vertices.begin()->second;
    for (const auto &[id, p] : result.surface.vertices) {
        result.low = {std::min(result.low.x, p.x), std::min(result.low.y, p.y),
                      std::min(result.low.z, p.z)};
        result.high = {std::max(result.high.x, p.x), std::max(result.high.y, p.y),
                       std::max(result.high.z, p.z)};
    }
    return result;
}
bool overlaps(const Sample &a, const Sample &b) {
    return a.low.x <= b.high.x + tolerance && b.low.x <= a.high.x + tolerance &&
           a.low.y <= b.high.y + tolerance && b.low.y <= a.high.y + tolerance &&
           a.low.z <= b.high.z + tolerance && b.low.z <= a.high.z + tolerance;
}
bool beneath(const Document &doc, Id body, Id root) {
    if (!root)
        return false;
    for (; body; body = doc.bodies().at(body)->parent)
        if (body == root)
            return true;
    return false;
}
struct PlaneEdges {
    Vec3 origin, normal;
    std::vector<std::array<Vec3, 2>> edges;
};
} // namespace
ChangeReport intersectSelected(Document &doc, const SelectionSet &targets, IntersectionMode mode,
                               Id context, const Document *outerScene, Id excludedInstance) {
    if (targets.empty() || targets.size() > 128)
        fail("INTERSECTION_LIMIT", "Select 1–128 target faces to intersect");
    if (mode != IntersectionMode::Selected && mode != IntersectionMode::Context &&
        mode != IntersectionMode::Model)
        fail("INTERSECTION_MODE", "Choose selected, context or model intersection mode");
    Selection policy;
    policy.enter(doc, context);
    for (auto target : targets)
        if (target.kind != SelectionKind::Face || !policy.selectable(doc, target))
            fail("INTERSECTION_SCOPE",
                 "Intersection targets must be editable faces in the specified context");
    std::map<SelectedEntity, Sample> own;
    std::vector<Sample> external;
    size_t vertices{};
    for (auto target : targets)
        own.emplace(target, sample(doc, target, vertices));
    if (mode != IntersectionMode::Selected)
        for (const auto &[id, body] : doc.bodies()) {
            if ((mode == IntersectionMode::Context && !policy.inContext(doc, id)) ||
                policy.hidden(doc, {id, SelectionKind::Body, 0}))
                continue;
            for (const auto &[face, record] : body->surface.faces) {
                const SelectedEntity entity{id, SelectionKind::Face, face};
                if (own.contains(entity))
                    continue;
                if (own.size() + external.size() >= 2048)
                    fail("INTERSECTION_LIMIT",
                         "Intersection references exceed 2048 faces; narrow the scope");
                own.emplace(entity, sample(doc, entity, vertices));
            }
        }
    if (mode == IntersectionMode::Model && outerScene) {
        Selection outerPolicy;
        outerPolicy.sync(*outerScene);
        for (const auto &[id, body] : outerScene->bodies()) {
            if (beneath(*outerScene, id, excludedInstance) ||
                outerPolicy.hidden(*outerScene, {id, SelectionKind::Body, 0}))
                continue;
            for (const auto &[face, record] : body->surface.faces) {
                if (own.size() + external.size() >= 2048)
                    fail("INTERSECTION_LIMIT",
                         "Intersection references exceed 2048 faces; narrow the scope");
                external.push_back(sample(*outerScene, {id, SelectionKind::Face, face}, vertices));
            }
        }
    }
    std::map<Id, std::vector<PlaneEdges>> planes;
    size_t pairs = 100000, work = 4000000, output{};
    for (auto target : targets) {
        const auto &a = own.at(target);
        const auto &source = *doc.bodies().at(target.body);
        const auto inverse = doc.worldTransform(target.body).inverse();
        const auto origin =
            source.surface.vertices.at(source.surface.faces.at(target.entity).loops[0][0]);
        const auto normal = source.surface.normal(target.entity);
        auto intersect = [&](const Sample &b) {
            if (!pairs--)
                fail("INTERSECTION_LIMIT",
                     "Intersection exceeds 100000 face candidates; narrow the scope");
            if (!overlaps(a, b))
                return;
            const auto cost = a.surface.vertices.size() * b.surface.vertices.size();
            if (cost > work)
                fail("INTERSECTION_LIMIT",
                     "Intersection exceeds its geometry work budget; narrow the scope");
            work -= cost;
            const auto result =
                intersectFaces(a.surface, target.entity, b.surface, b.entity.entity);
            if (result.edges.empty())
                return;
            auto &groups = planes[target.body];
            auto found = std::find_if(groups.begin(), groups.end(), [&](const auto &plane) {
                if (std::abs(dot(plane.normal, normal)) < 1 - 1e-12 ||
                    std::abs(dot(origin - plane.origin, plane.normal)) > tolerance)
                    return false;
                for (auto edge : result.edges)
                    for (auto p : edge)
                        if (std::abs(dot(inverse.point(p) - plane.origin, plane.normal)) >
                            tolerance)
                            return false;
                return true;
            });
            if (found == groups.end()) {
                groups.push_back({origin, normal, {}});
                found = std::prev(groups.end());
            }
            for (auto edge : result.edges) {
                for (auto &p : edge)
                    p = inverse.point(p);
                const bool duplicate =
                    std::any_of(found->edges.begin(), found->edges.end(), [&](auto old) {
                        return (length(old[0] - edge[0]) <= tolerance &&
                                length(old[1] - edge[1]) <= tolerance) ||
                               (length(old[1] - edge[0]) <= tolerance &&
                                length(old[0] - edge[1]) <= tolerance);
                    });
                if (duplicate)
                    continue;
                if (++output > 8192 || found->edges.size() >= 1024)
                    fail("INTERSECTION_LIMIT",
                         "Intersection exceeds the output edge budget; narrow the scope");
                found->edges.push_back(edge);
            }
        };
        for (const auto &[entity, b] : own)
            if (entity != target)
                intersect(b);
        for (const auto &b : external)
            intersect(b);
    }
    Edit edit{"Intersect faces", {}};
    for (const auto &[id, groups] : planes) {
        const auto old = doc.bodies().at(id);
        auto body = std::make_shared<Body>(*old);
        std::map<Id, std::vector<Id>> lineage;
        for (const auto &[face, record] : old->surface.faces)
            lineage[face] = {face};
        for (const auto &plane : groups) {
            const auto result =
                insertPlanarEdges(body->surface, plane.origin, plane.normal, plane.edges);
            if (result.surface == body->surface)
                continue;
            const auto before = *body;
            body->surface = result.surface;
            inheritFaceAppearance(before, *body, result.faces);
            for (auto &[source, descendants] : lineage) {
                std::vector<Id> next;
                for (auto face : descendants) {
                    const auto found = result.faces.find(face);
                    if (found == result.faces.end())
                        next.push_back(face);
                    else
                        next.insert(next.end(), found->second.begin(), found->second.end());
                }
                std::sort(next.begin(), next.end());
                next.erase(std::unique(next.begin(), next.end()), next.end());
                descendants = std::move(next);
            }
        }
        if (body->surface != old->surface)
            edit.changes.push_back({id, old, body, std::move(lineage)});
    }
    if (edit.changes.empty())
        return {};
    return doc.apply(std::move(edit), doc.revision());
}
} // namespace sketchy
