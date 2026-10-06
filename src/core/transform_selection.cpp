#include "core/transform_selection.hpp"
#include "core/appearance.hpp"
#include "core/face_textures.hpp"
#include "core/geometry_subset.hpp"
#include "core/hosted_components.hpp"
#include <algorithm>
namespace sketchy {
namespace {
using Part = GeometrySubset;
void transformPart(Body &body, const Part &part, const Transform &matrix) {
    for (auto vertex : part.vertices)
        body.surface.vertices.at(vertex) = matrix.point(body.surface.vertices.at(vertex));
    for (auto &[id, face] : body.surface.faces) {
        bool complete = true;
        for (const auto &loop : face.loops)
            for (auto vertex : loop)
                complete &= part.vertices.contains(vertex);
        if (complete) {
            const auto mapping = transformTextureMappings(faceTextureMappings(body, id), matrix);
            setFaceTextureMappings(body, id, mapping);
            if (matrix.determinant() < 0)
                for (auto &loop : face.loops)
                    std::reverse(loop.begin(), loop.end());
        }
    }
    for (auto it = body.curves.begin(); it != body.curves.end();) {
        bool any = false, all = true;
        for (auto edge : it->second.edges) {
            const auto &record = body.topology.edges.at(edge.edge);
            for (auto vertex : {record.a, record.b}) {
                any |= part.vertices.contains(vertex);
                all &= part.vertices.contains(vertex);
            }
        }
        if (any && !all)
            it = body.curves.erase(it);
        else {
            if (all) {
                auto &curve = it->second;
                curve.center = matrix.point(curve.center);
                curve.xAxis = matrix.vector(curve.xAxis);
                curve.yAxis = matrix.vector(curve.yAxis);
            }
            ++it;
        }
    }
    for (auto guide : part.guides) {
        auto &record = body.guides.at(guide);
        record = record.kind == GuideKind::Point
                     ? guidePoint(matrix.point(record.origin))
                     : guideLine(matrix.point(record.origin), matrix.vector(record.direction));
    }
    // Validate all incident faces, including ones outside the explicit selection.
    body.surface.validate();
    body.topology.validate(body.surface);
}
Change appendCopy(const BodyPtr &old, const Body &geometry, GeometryCopies &mapping) {
    auto body = std::make_shared<Body>(*old);
    auto allocate = [&] {
        if (body->surface.nextId == UINT64_MAX)
            throw std::runtime_error("Geometry identity space exhausted");
        return body->surface.nextId++;
    };
    for (const auto &[id, point] : geometry.surface.vertices) {
        const auto copy = allocate();
        mapping.vertices[id] = copy;
        body->surface.vertices[copy] = point;
    }
    for (const auto &[id, face] : geometry.surface.faces) {
        auto record = face;
        record.id = allocate();
        mapping.faces[id] = record.id;
        const auto materials = faceMaterials(geometry, id);
        if (materials != body->materials)
            body->faceMaterials[record.id] = materials;
        const auto color = faceColor(geometry, id);
        if (color != body->color)
            body->faceColors[record.id] = color;
        for (auto &loop : record.loops)
            for (auto &vertex : loop)
                vertex = mapping.vertices.at(vertex);
        const auto copiedFace = record.id;
        body->surface.faces[copiedFace] = std::move(record);
        setFaceTextureMappings(*body, copiedFace, faceTextureMappings(geometry, id));
    }
    for (auto wire : geometry.surface.wires)
        body->surface.wires.push_back({mapping.vertices.at(wire[0]), mapping.vertices.at(wire[1])});
    body->topology = Topology::rebuild(body->surface, old->topology, old->topology.nextId);
    std::map<std::array<Id, 2>, Id> edgeIds;
    for (const auto &[id, edge] : body->topology.edges)
        edgeIds[{edge.a, edge.b}] = id;
    for (const auto &[id, edge] : geometry.topology.edges) {
        auto a = mapping.vertices.at(edge.a), b = mapping.vertices.at(edge.b);
        mapping.edges[id] = edgeIds.at({std::min(a, b), std::max(a, b)});
        if (geometry.edgeAppearances.contains(id))
            body->edgeAppearances[mapping.edges[id]] = geometry.edgeAppearances.at(id);
    }
    for (const auto &[id, curve] : geometry.curves) {
        auto record = curve;
        for (auto &edge : record.edges)
            edge.edge = mapping.edges.at(edge.edge);
        const auto copy = allocate();
        mapping.curves[id] = copy;
        body->curves[copy] = std::move(record);
    }
    for (const auto &[id, guide] : geometry.guides) {
        const auto copy = allocate();
        mapping.guides[id] = copy;
        body->guides[copy] = guide;
    }
    Change change{old->id, old, body};
    auto descendants = [](const auto &copies, auto &mapping) {
        for (const auto &[source, copied] : copies)
            mapping[source] = {source, copied};
    };
    descendants(mapping.vertices, change.vertexDescendants);
    descendants(mapping.faces, change.faceDescendants);
    descendants(mapping.edges, change.edgeDescendants);
    return change;
}
} // namespace
TransformResult transformSelected(Document &doc, const TransformTargets &targets,
                                  const Transform &operation, Vec3 pivot, TransformSpace space,
                                  bool copy) {
    if (targets.empty() || targets.size() > 10000)
        throw std::runtime_error("Transform requires 1–10000 typed targets");
    if (space != TransformSpace::World && space != TransformSpace::Local)
        throw std::runtime_error("Unknown transform frame");
    operation.validate();
    checkPoint(pivot);
    const auto around =
        Transform::translation(pivot) * operation * Transform::translation(pivot * -1);
    std::map<Id, Part> parts;
    for (auto target : targets) {
        if (!doc.bodies().contains(target.body))
            throw std::runtime_error("Transform context does not exist");
        const auto &body = *doc.bodies().at(target.body);
        auto &part = parts[target.body];
        switch (target.kind) {
        case TransformKind::Context:
            if (target.entity)
                throw std::runtime_error("Whole-context transform requires entity zero");
            part.whole = true;
            break;
        case TransformKind::Face:
            if (!body.surface.faces.contains(target.entity))
                throw std::runtime_error("Transform face does not exist");
            includeGeometryFace(body, part, target.entity);
            break;
        case TransformKind::Edge:
            if (!body.topology.edges.contains(target.entity))
                throw std::runtime_error("Transform edge does not exist");
            part.edges.insert(target.entity);
            part.vertices.insert(body.topology.edges.at(target.entity).a);
            part.vertices.insert(body.topology.edges.at(target.entity).b);
            break;
        case TransformKind::Vertex:
            if (!body.surface.vertices.contains(target.entity))
                throw std::runtime_error("Transform vertex does not exist");
            part.vertices.insert(target.entity);
            part.explicitVertices.insert(target.entity);
            break;
        case TransformKind::Guide:
            if (!body.guides.contains(target.entity))
                throw std::runtime_error("Transform guide does not exist");
            part.guides.insert(target.entity);
            break;
        default:
            throw std::runtime_error("Unknown transform target kind");
        }
    }
    std::set<Id> roots;
    for (const auto &[id, part] : parts)
        if (part.whole)
            roots.insert(id);
    auto covered = [&](Id body) {
        for (auto parent = doc.bodies().at(body)->parent; parent;
             parent = doc.bodies().at(parent)->parent)
            if (roots.contains(parent))
                return true;
        return false;
    };
    std::erase_if(parts, [&](const auto &item) { return covered(item.first); });
    if (!copy && around == Transform{})
        return {};
    TransformResult result;
    Edit edit{copy ? "Copy selected geometry" : "Transform selected geometry", {}};
    Id next = doc.nextId();
    auto allocate = [&](Id source) {
        if (next == UINT64_MAX)
            throw std::runtime_error("Context identity space exhausted");
        result.copies[source] = next++;
    };
    if (copy) {
        for (const auto &[id, body] : doc.bodies())
            if ((parts.contains(id) && parts.at(id).whole) || covered(id))
                allocate(id);
    }
    for (const auto &[id, part] : parts) {
        const auto old = doc.bodies().at(id);
        auto body =
            copy && !part.whole ? extractGeometry(*old, part) : std::make_shared<Body>(*old);
        const auto world = doc.worldTransform(id);
        if (part.whole) {
            const auto transformed =
                space == TransformSpace::World ? around * world : world * around;
            const auto parent = old->parent ? doc.worldTransform(old->parent) : Transform{};
            body->transform = parent.inverse() * transformed;
        } else {
            const auto local =
                space == TransformSpace::Local ? around : world.inverse() * around * world;
            transformPart(*body, part, local);
        }
        if (copy && !part.whole) {
            edit.changes.push_back(appendCopy(old, *body, result.geometryCopies[id]));
        } else if (copy) {
            body->id = result.copies.at(id);
            if (body->name.size() <= 1019)
                body->name += " copy";
            edit.changes.push_back({body->id, nullptr, body});
        } else if (*body != *old)
            edit.changes.push_back({id, old, body});
    }
    if (copy)
        for (const auto &[id, newId] : result.copies) {
            if (parts.contains(id))
                continue;
            auto body = std::make_shared<Body>(*doc.bodies().at(id));
            body->id = newId;
            body->parent = result.copies.at(body->parent);
            edit.changes.push_back({newId, nullptr, body});
        }
    if (copy)
        for (const auto &[root, instance] : doc.instances())
            if (result.copies.contains(root)) {
                auto binding = std::make_shared<ComponentInstance>(*instance);
                for (auto &[member, target] : binding->members)
                    target = result.copies.at(target);
                edit.instances.push_back({result.copies.at(root), nullptr, binding});
            }
    if (copy)
        expandHostedCopies(doc, edit, result.copies);
    if (!edit.changes.empty())
        result.changes = doc.apply(std::move(edit), doc.revision());
    return result;
}
} // namespace sketchy
