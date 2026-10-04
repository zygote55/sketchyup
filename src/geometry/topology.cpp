#include "geometry/topology.hpp"
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
using Key = std::array<Id, 2>;
Key key(Id a, Id b) { return {std::min(a, b), std::max(a, b)}; }
std::map<Key, bool> boundaries(const Surface &surface) {
    std::map<Key, bool> records;
    for (const auto &[id, face] : surface.faces)
        for (const auto &loop : face.loops)
            for (size_t i = 0; i < loop.size(); ++i)
                records.try_emplace(key(loop[i], loop[(i + 1) % loop.size()]), false);
    std::set<Key> wires;
    for (auto wire : surface.wires) {
        auto endpoints = key(wire[0], wire[1]);
        if (!wires.insert(endpoints).second)
            throw std::runtime_error("Duplicate loose edge");
        records[endpoints] = true;
    }
    if (records.size() > Topology::edgeLimit)
        throw std::runtime_error("Too many topology edges");
    return records;
}
template <class T>
EntityChanges compare(const std::map<Id, T> &before, const std::map<Id, T> &after) {
    EntityChanges result;
    for (const auto &[id, record] : before) {
        if (!after.contains(id)) {
            result.deleted.push_back(id);
            result.descendants[id] = {};
        } else {
            result.descendants[id] = {id};
            if (record != after.at(id))
                result.modified.push_back(id);
        }
    }
    for (const auto &[id, record] : after)
        if (!before.contains(id))
            result.created.push_back(id);
    return result;
}
} // namespace
Topology Topology::rebuild(const Surface &surface, const Topology &previous, Id floor) {
    Topology result;
    result.nextId = std::max(previous.nextId, floor);
    std::map<Key, Id> existing;
    for (const auto &[id, edge] : previous.edges)
        existing.emplace(key(edge.a, edge.b), id);
    for (const auto &[endpoints, wire] : boundaries(surface)) {
        Id id;
        if (auto it = existing.find(endpoints); it != existing.end())
            id = it->second;
        else {
            if (result.nextId == UINT64_MAX)
                throw std::runtime_error("Edge ID space exhausted");
            id = result.nextId++;
        }
        result.edges.emplace(id, EdgeRecord{endpoints[0], endpoints[1], wire});
    }
    return result;
}
void Topology::validate(const Surface &surface) const {
    if (!nextId || edges.size() > edgeLimit)
        throw std::runtime_error("Invalid edge allocator");
    auto expected = boundaries(surface);
    if (expected.size() != edges.size())
        throw std::runtime_error("Incomplete topology edge records");
    for (const auto &[id, edge] : edges) {
        auto found = expected.find({edge.a, edge.b});
        if (!id || id >= nextId || edge.a >= edge.b || !surface.vertices.contains(edge.a) ||
            !surface.vertices.contains(edge.b) || found == expected.end() ||
            found->second != edge.wire)
            throw std::runtime_error("Invalid topology edge record");
        if (length(surface.vertices.at(edge.a) - surface.vertices.at(edge.b)) < tolerance)
            throw std::runtime_error("Degenerate topology edge");
        expected.erase(found);
    }
}
Adjacency Topology::adjacency(const Surface &surface) const {
    validate(surface);
    Adjacency result;
    std::map<Key, Id> lookup;
    for (const auto &[id, point] : surface.vertices)
        result.vertexEdges[id];
    for (const auto &[id, edge] : edges) {
        lookup[{edge.a, edge.b}] = id;
        result.vertexEdges[edge.a].push_back(id);
        result.vertexEdges[edge.b].push_back(id);
        result.edgeFaces[id];
    }
    for (const auto &[id, face] : surface.faces) {
        auto &loops = result.faceLoops[id];
        for (size_t l = 0; l < face.loops.size(); ++l) {
            const auto &loop = face.loops[l];
            loops.emplace_back();
            for (size_t i = 0; i < loop.size(); ++i) {
                const auto a = loop[i], b = loop[(i + 1) % loop.size()];
                const auto edge = lookup.at(key(a, b));
                loops.back().push_back({edge, a > b});
                result.edgeFaces[edge].push_back({id, l, i, a > b});
            }
        }
    }
    return result;
}
TopologyChanges compareTopology(const Surface &before, const Topology &beforeTopology,
                                const Surface &after, const Topology &afterTopology,
                                bool inferEdges) {
    if (before == after && beforeTopology == afterTopology)
        return {};
    TopologyChanges result{compare(before.vertices, after.vertices),
                           compare(beforeTopology.edges, afterTopology.edges),
                           compare(before.faces, after.faces),
                           {}};
    const auto oldAdjacency = beforeTopology.adjacency(before);
    const auto nextAdjacency = afterTopology.adjacency(after);
    for (const auto &[id, edge] : beforeTopology.edges) {
        if (!afterTopology.edges.contains(id))
            continue;
        const auto &next = afterTopology.edges.at(id);
        if (before.vertices.at(edge.a) != after.vertices.at(next.a) ||
            before.vertices.at(edge.b) != after.vertices.at(next.b) ||
            oldAdjacency.edgeFaces.at(id) != nextAdjacency.edgeFaces.at(id))
            result.edges.modified.push_back(id);
    }
    std::sort(result.edges.modified.begin(), result.edges.modified.end());
    result.edges.modified.erase(
        std::unique(result.edges.modified.begin(), result.edges.modified.end()),
        result.edges.modified.end());
    if (!inferEdges)
        return result;
    constexpr size_t lineagePairLimit = 1000000;
    if (!result.edges.deleted.empty() &&
        result.edges.created.size() > lineagePairLimit / result.edges.deleted.size())
        throw std::runtime_error("Topology lineage exceeds the one-million comparison budget");
    // Only new edges can descend from a retired edge. Coincident existing edges
    // are not arbitrarily assigned a new lineage. Split and merged spans overlap.
    for (auto id : result.edges.deleted) {
        const auto &edge = beforeTopology.edges.at(id);
        const auto origin = before.vertices.at(edge.a);
        const auto delta = before.vertices.at(edge.b) - origin;
        const auto size = length(delta);
        if (size < tolerance)
            continue;
        const auto direction = delta * (1 / size);
        for (auto candidate : result.edges.created) {
            const auto &next = afterTopology.edges.at(candidate);
            const auto a = after.vertices.at(next.a) - origin,
                       b = after.vertices.at(next.b) - origin;
            if (length(cross(a, direction)) > tolerance || length(cross(b, direction)) > tolerance)
                continue;
            auto low = dot(a, direction), high = dot(b, direction);
            if (low > high)
                std::swap(low, high);
            if (std::min(size, high) - std::max(0.0, low) > tolerance)
                result.edges.descendants[id].push_back(candidate);
        }
    }
    return result;
}
Id splitEdge(Surface &surface, const EdgeRecord &edge, double fraction) {
    if (!std::isfinite(fraction) || fraction <= 0 || fraction >= 1)
        throw std::runtime_error("Edge split fraction must be strictly between zero and one");
    Surface staged = surface;
    const auto a = surface.vertices.at(edge.a), b = surface.vertices.at(edge.b);
    const auto point = a + (b - a) * fraction;
    if (length(point - a) < tolerance || length(point - b) < tolerance)
        throw std::runtime_error("Edge split is too close to an endpoint");
    const auto vertex = staged.vertex(point);
    const auto target = key(edge.a, edge.b);
    bool found = false;
    for (auto &[id, face] : staged.faces)
        for (auto &loop : face.loops) {
            std::vector<Id> updated;
            for (size_t i = 0; i < loop.size(); ++i) {
                updated.push_back(loop[i]);
                if (key(loop[i], loop[(i + 1) % loop.size()]) == target) {
                    updated.push_back(vertex);
                    found = true;
                }
            }
            loop = std::move(updated);
        }
    std::vector<Key> wires;
    for (auto wire : staged.wires) {
        if (key(wire[0], wire[1]) == target) {
            wires.push_back({wire[0], vertex});
            wires.push_back({vertex, wire[1]});
            found = true;
        } else
            wires.push_back(wire);
    }
    if (!found)
        throw std::runtime_error("Edge does not belong to this editing context");
    staged.wires = std::move(wires);
    staged.validate();
    surface = std::move(staged);
    return vertex;
}
} // namespace sketchy
