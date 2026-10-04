#include "core/consolidation.hpp"
#include "core/appearance.hpp"
#include "core/selection.hpp"
#include "core/tags.hpp"
#include <algorithm>
namespace sketchy {
std::vector<std::set<Id>> consolidationGroups(const Document &doc, Id context,
                                              std::optional<std::set<Id>> members) {
    Selection policy;
    policy.enter(doc, context);
    auto eligible = [&](Id id) {
        return policy.inContext(doc, id) && !policy.locked(doc, id) &&
               !policy.hidden(doc, {id, SelectionKind::Body, 0});
    };
    std::set<Id> sources;
    if (members) {
        for (auto id : *members) {
            if (!eligible(id))
                throw std::runtime_error("Merge member is outside the context or locked/hidden");
            sources.insert(id);
        }
    } else
        for (const auto &[id, body] : doc.bodies())
            if (eligible(id))
                sources.insert(id);
    std::map<std::set<Id>, std::set<Id>> groups;
    for (auto id : sources)
        groups[inheritedTags(doc, id)].insert(id);
    std::vector<std::set<Id>> result;
    for (auto &[tags, ids] : groups)
        result.push_back(std::move(ids));
    return result;
}
ConsolidationResult consolidateContext(Document &doc, Id context,
                                       std::optional<std::set<Id>> members) {
    const auto groups = consolidationGroups(doc, context, members);
    if (groups.empty())
        return {};
    if (groups.size() != 1)
        throw std::runtime_error(
            "Raw geometry with different tags must remain in separate records");
    const auto &sources = groups.front();
    const auto destination = context && sources.contains(context) ? context : *sources.begin();
    const auto old = doc.bodies().at(destination);
    auto merged = std::make_shared<Body>(*old);
    const auto inverse = doc.worldTransform(destination).inverse();
    ConsolidationResult result;
    result.destination = destination;
    auto allocate = [&] {
        if (merged->surface.nextId == UINT64_MAX)
            throw std::runtime_error("Geometry identity space exhausted");
        return merged->surface.nextId++;
    };
    size_t curves = 0, guides = 0;
    for (auto id : sources) {
        curves += doc.bodies().at(id)->curves.size();
        guides += doc.bodies().at(id)->guides.size();
    }
    if (curves > 1024 || guides > 1024)
        throw std::runtime_error("Merged context exceeds the curve or guide budget");
    for (auto id : sources) {
        const auto &source = *doc.bodies().at(id);
        auto &map = result.transfers[id];
        if (id == destination) {
            for (const auto &[vertex, point] : source.surface.vertices)
                map.vertices[vertex] = vertex;
            for (const auto &[face, record] : source.surface.faces)
                map.faces[face] = face;
            for (const auto &[curve, record] : source.curves)
                map.curves[curve] = curve;
            for (const auto &[guide, record] : source.guides)
                map.guides[guide] = guide;
            continue;
        }
        const auto frame = inverse * doc.worldTransform(id);
        for (const auto &[vertex, point] : source.surface.vertices) {
            const auto copy = allocate();
            map.vertices[vertex] = copy;
            merged->surface.vertices[copy] = frame.point(point);
        }
        for (const auto &[face, record] : source.surface.faces) {
            auto copy = record;
            copy.id = allocate();
            map.faces[face] = copy.id;
            for (auto &loop : copy.loops) {
                for (auto &vertex : loop)
                    vertex = map.vertices.at(vertex);
                if (frame.determinant() < 0)
                    std::reverse(loop.begin(), loop.end());
            }
            merged->surface.faces[copy.id] = copy;
            const auto color = faceColor(source, face);
            if (color != merged->color)
                merged->faceColors[copy.id] = color;
        }
        for (auto wire : source.surface.wires)
            merged->surface.wires.push_back({map.vertices.at(wire[0]), map.vertices.at(wire[1])});
        for (const auto &[guide, record] : source.guides) {
            const auto copy = allocate();
            map.guides[guide] = copy;
            merged->guides[copy] =
                record.kind == GuideKind::Point
                    ? guidePoint(frame.point(record.origin))
                    : guideLine(frame.point(record.origin), frame.vector(record.direction));
        }
        for (const auto &[curve, record] : source.curves) {
            auto copy = record;
            copy.center = frame.point(record.center);
            copy.xAxis = frame.vector(record.xAxis);
            copy.yAxis = frame.vector(record.yAxis);
            const auto curveId = allocate();
            map.curves[curve] = curveId;
            merged->curves[curveId] = copy;
        }
    }
    // Rebuild once after appending every record, then weld in the destination's
    // local tolerance. Original destination IDs win over newly allocated IDs.
    const auto appended = Topology::rebuild(merged->surface, old->topology);
    const auto cleaned = cleanupCoincident(merged->surface, appended);
    merged->surface = cleaned.surface;
    merged->topology = Topology::rebuild(merged->surface, appended);
    std::map<std::array<Id, 2>, Id> edges;
    for (const auto &[id, edge] : merged->topology.edges)
        edges[{edge.a, edge.b}] = id;
    for (auto sourceId : sources) {
        const auto &source = *doc.bodies().at(sourceId);
        auto &map = result.transfers.at(sourceId);
        for (auto &[id, target] : map.vertices)
            if (cleaned.vertices.contains(target))
                target = cleaned.vertices.at(target).front();
        for (const auto &[id, edge] : source.topology.edges) {
            const auto a = map.vertices.at(edge.a), b = map.vertices.at(edge.b);
            if (a != b)
                map.edges[id] = edges.at({std::min(a, b), std::max(a, b)});
        }
        for (const auto &[id, curve] : source.curves) {
            auto &copy = merged->curves.at(map.curves.at(id));
            copy.edges.clear();
            for (auto use : curve.edges) {
                if (!map.edges.contains(use.edge))
                    throw std::runtime_error("Merge would collapse an analytic curve edge");
                const auto &edge = source.topology.edges.at(use.edge);
                copy.edges.push_back(
                    {map.edges.at(use.edge),
                     use.reversed != (map.vertices.at(edge.a) > map.vertices.at(edge.b))});
            }
        }
    }
    Change target{destination, old, merged};
    for (const auto &[id, descendants] : cleaned.vertices)
        if (old->surface.vertices.contains(id))
            target.vertexDescendants[id] = descendants;
    for (const auto &[id, descendants] : cleaned.edges)
        if (old->topology.edges.contains(id))
            target.edgeDescendants[id] = descendants;
    Edit edit{"Merge editing context", {}};
    if (*merged != *old)
        edit.changes.push_back(std::move(target));
    std::set<Id> parents;
    for (const auto &[id, body] : doc.bodies())
        parents.insert(body->parent);
    for (auto id : sources) {
        if (id == destination)
            continue;
        const auto source = doc.bodies().at(id);
        if (!parents.contains(id) && id != context) {
            edit.changes.push_back({id, source, nullptr});
            continue;
        }
        // Keep frames with children: locked/nested descendants retain the same
        // hierarchy and world transforms even when their ancestor's raw mesh moves.
        auto empty = std::make_shared<Body>(*source);
        empty->surface.vertices.clear();
        empty->surface.faces.clear();
        empty->surface.wires.clear();
        empty->topology.edges.clear();
        empty->faceColors.clear();
        empty->curves.clear();
        empty->guides.clear();
        if (*empty != *source)
            edit.changes.push_back({id, source, empty});
    }
    if (!edit.changes.empty())
        result.changes = doc.apply(std::move(edit), doc.revision());
    return result;
}
} // namespace sketchy
