#include "core/entity_measure.hpp"
#include <algorithm>
namespace sketchy {
namespace {
void include(FrameMeasures &frame, Vec3 point) {
    if (!frame.bounds) {
        frame.bounds = EntityBounds{point, point};
        return;
    }
    auto &low = frame.bounds->low, &high = frame.bounds->high;
    low = {std::min(low.x, point.x), std::min(low.y, point.y), std::min(low.z, point.z)};
    high = {std::max(high.x, point.x), std::max(high.y, point.y), std::max(high.z, point.z)};
}
} // namespace
EntityMeasures measureEntity(const Document &doc, SelectedEntity entity) {
    if (!Selection{}.exists(doc, entity))
        throw std::runtime_error("Entity does not exist");
    EntityMeasures result;
    const auto worldFrame = doc.worldTransform(entity.body);
    const auto inverse = worldFrame.inverse();
    const auto parentId = doc.bodies().at(entity.body)->parent;
    const auto parentInverse = parentId ? doc.worldTransform(parentId).inverse() : Transform{};
    result.worldOrigin = worldFrame.point({});
    result.parentOrigin = doc.bodies().at(entity.body)->transform.point({});
    std::set<Id> bodies{entity.body};
    if (entity.kind == SelectionKind::Body)
        for (const auto &[id, body] : doc.bodies())
            for (auto parent = body->parent; parent; parent = doc.bodies().at(parent)->parent)
                if (parent == entity.body) {
                    bodies.insert(id);
                    break;
                }
    std::vector<Id> geometry;
    for (auto id : bodies) {
        const auto &body = *doc.bodies().at(id);
        const auto world = doc.worldTransform(id), local = inverse * world,
                   parent = parentInverse * world;
        ++result.records;
        std::set<Id> vertices, edges, faces;
        if (entity.kind == SelectionKind::Body) {
            for (const auto &[vertex, p] : body.surface.vertices)
                vertices.insert(vertex);
            for (const auto &[edge, record] : body.topology.edges)
                edges.insert(edge);
            for (const auto &[face, record] : body.surface.faces)
                faces.insert(face);
            result.guides += body.guides.size();
            if (!vertices.empty() || !faces.empty() || !edges.empty())
                geometry.push_back(id);
        } else if (entity.kind == SelectionKind::Face) {
            faces.insert(entity.entity);
            for (const auto &loop : body.surface.faces.at(entity.entity).loops)
                vertices.insert(loop.begin(), loop.end());
            const auto adjacency = body.topology.adjacency(body.surface);
            for (const auto &loop : adjacency.faceLoops.at(entity.entity))
                for (auto edge : loop)
                    edges.insert(edge.edge);
        } else if (entity.kind == SelectionKind::Edge) {
            edges.insert(entity.entity);
            const auto &edge = body.topology.edges.at(entity.entity);
            vertices.insert(edge.a);
            vertices.insert(edge.b);
        } else {
            const auto &guide = body.guides.at(entity.entity);
            result.guides = 1;
            result.worldOrigin = world.point(guide.origin);
            result.parentOrigin = body.transform.point(guide.origin);
            if (guide.kind == GuideKind::Point) {
                include(result.world, world.point(guide.origin));
                include(result.local, local.point(guide.origin));
                include(result.parent, parent.point(guide.origin));
            } else
                result.world.infiniteLength = result.parent.infiniteLength =
                    result.local.infiniteLength = true;
        }
        result.vertices += vertices.size();
        result.edges += edges.size();
        result.faces += faces.size();
        for (auto vertex : vertices) {
            const auto point = body.surface.vertices.at(vertex);
            include(result.world, world.point(point));
            include(result.local, local.point(point));
            include(result.parent, parent.point(point));
        }
        for (auto edge : edges) {
            const auto &record = body.topology.edges.at(edge);
            const auto delta =
                body.surface.vertices.at(record.b) - body.surface.vertices.at(record.a);
            result.world.length += length(world.vector(delta));
            result.local.length += length(local.vector(delta));
            result.parent.length += length(parent.vector(delta));
        }
        for (auto face : faces)
            for (const auto &triangle : body.surface.triangulate(face)) {
                const auto a = triangle.b - triangle.a, b = triangle.c - triangle.a;
                result.world.area += length(cross(world.vector(a), world.vector(b))) * .5;
                result.local.area += length(cross(local.vector(a), local.vector(b))) * .5;
                result.parent.area += length(cross(parent.vector(a), parent.vector(b))) * .5;
            }
    }
    if (entity.kind == SelectionKind::Body) {
        if (geometry.empty())
            result.solid = {"empty", {}};
        else if (geometry.size() != 1)
            result.solid = {"multiple_records", {}};
        else {
            result.solidBody = geometry.front();
            const auto &body = *doc.bodies().at(result.solidBody);
            result.solid = inspectSolid(body.surface, body.topology);
            if (result.solid.volume) {
                const auto world = doc.worldTransform(result.solidBody);
                result.world.volume = *result.solid.volume * std::abs(world.determinant());
                result.parent.volume =
                    *result.solid.volume * std::abs((parentInverse * world).determinant());
                result.local.volume =
                    *result.solid.volume * std::abs((inverse * world).determinant());
            }
        }
    }
    return result;
}
ChangeReport positionEntity(Document &doc, Id id, Vec3 position, bool world) {
    checkPoint(position);
    const auto &body = *doc.bodies().at(id);
    const auto parent = body.parent ? doc.worldTransform(body.parent) : Transform{};
    const auto target = world ? position : parent.point(position);
    const auto delta = target - doc.worldTransform(id).point({});
    if (length(delta) <= tolerance)
        return {};
    return transformSelected(doc, {{id, TransformKind::Context, 0}}, Transform::translation(delta))
        .changes;
}
ChangeReport dimensionEntity(Document &doc, Id id, Vec3 dimensions, bool world) {
    checkPoint(dimensions);
    const auto measured = measureEntity(doc, {id, SelectionKind::Body, 0});
    const auto &bounds = world ? measured.world.bounds : measured.parent.bounds;
    if (!bounds)
        throw std::runtime_error("Entity has no finite geometry bounds");
    const auto current = bounds->dimensions();
    const std::array<double, 3> source{current.x, current.y, current.z},
        target{dimensions.x, dimensions.y, dimensions.z};
    Vec3 scale;
    const std::array<double *, 3> output{&scale.x, &scale.y, &scale.z};
    bool changed = false;
    for (size_t i = 0; i < 3; ++i) {
        if (target[i] < 0)
            throw std::runtime_error("Dimensions cannot be negative");
        if (source[i] <= tolerance) {
            if (target[i] > tolerance)
                throw std::runtime_error("Use push/pull to give thickness to planar geometry");
            *output[i] = 1;
        } else {
            if (target[i] <= tolerance)
                throw std::runtime_error("Dimensions cannot collapse geometry");
            *output[i] = target[i] / source[i];
            changed |= std::abs(target[i] - source[i]) > tolerance;
        }
    }
    if (!changed)
        return {};
    const auto parent = doc.bodies().at(id)->parent;
    const auto basis = !world && parent ? doc.worldTransform(parent) : Transform{};
    const auto operation = Transform::translation(bounds->low) * Transform::scaling(scale) *
                           Transform::translation(bounds->low * -1);
    return transformSelected(doc, {{id, TransformKind::Context, 0}},
                             basis * operation * basis.inverse())
        .changes;
}
ChangeReport setEntityProperties(Document &doc, Id id, EntityProperties properties) {
    const auto old = doc.bodies().at(id);
    if (old->properties == properties)
        return {};
    auto body = std::make_shared<Body>(*old);
    body->properties = std::move(properties);
    return doc.apply({"Edit entity properties", {{id, old, body}}}, doc.revision());
}
} // namespace sketchy
