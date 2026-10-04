#include "core/geometry_subset.hpp"
#include <algorithm>
namespace sketchy {

void includeGeometryFace(const Body &body, GeometrySubset &part, Id face) {
    part.faces.insert(face);
    for (const auto &loop : body.surface.faces.at(face).loops)
        part.vertices.insert(loop.begin(), loop.end());
}
std::shared_ptr<Body> extractGeometry(const Body &source, const GeometrySubset &part) {
    auto result = std::make_shared<Body>(source);
    auto &surface = result->surface;
    std::erase_if(surface.faces,
                  [&](const auto &item) { return !part.faces.contains(item.first); });
    std::erase_if(result->faceColors,
                  [&](const auto &entry) { return !part.faces.contains(entry.first); });
    std::erase_if(surface.vertices,
                  [&](const auto &item) { return !part.vertices.contains(item.first); });
    surface.wires.clear();
    const auto adjacency = source.topology.adjacency(source.surface);
    std::set<Id> boundaries;
    for (auto face : part.faces)
        for (const auto &loop : adjacency.faceLoops.at(face))
            for (auto edge : loop)
                boundaries.insert(edge.edge);
    for (auto edge : part.edges)
        if (!boundaries.contains(edge)) {
            const auto &record = source.topology.edges.at(edge);
            surface.wires.push_back({record.a, record.b});
        }
    result->topology = Topology::rebuild(surface, source.topology, source.topology.nextId);
    for (auto vertex : part.explicitVertices)
        if (std::none_of(result->topology.edges.begin(), result->topology.edges.end(),
                         [&](const auto &item) {
                             return item.second.a == vertex || item.second.b == vertex;
                         }))
            throw std::runtime_error(
                "Copy vertices with their edges or faces; use guide points for independent points");
    std::erase_if(result->guides,
                  [&](const auto &item) { return !part.guides.contains(item.first); });
    std::erase_if(result->curves, [&](const auto &item) {
        return std::any_of(item.second.edges.begin(), item.second.edges.end(),
                           [&](auto edge) { return !result->topology.edges.contains(edge.edge); });
    });
    return result;
}
} // namespace sketchy
