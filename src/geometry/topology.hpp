#pragma once
#include "core/document_limits.hpp"
#include "geometry/surface.hpp"
namespace sketchy {
struct EdgeRecord {
    Id a{}, b{};
    bool wire{};
    bool operator==(const EdgeRecord &) const = default;
};
struct OrientedEdge {
    Id edge{};
    bool reversed{};
    bool operator==(const OrientedEdge &) const = default;
};
struct Incidence {
    Id face{};
    size_t loop{}, position{};
    bool reversed{};
    bool operator==(const Incidence &) const = default;
};
struct Adjacency {
    std::map<Id, std::vector<Id>> vertexEdges;
    std::map<Id, std::vector<Incidence>> edgeFaces;
    std::map<Id, std::vector<std::vector<OrientedEdge>>> faceLoops;
};
// Edge IDs live in the body's editing context, in their own entity namespace.
// Vertex/face loops remain authoritative geometry; these persistent records must
// cover their boundaries and loose wires exactly. Radial incidence is unrestricted.
struct Topology {
    std::map<Id, EdgeRecord> edges;
    Id nextId{1};
    // Scales with the benchmark-only limit scale (R114); 2,400,000 at every product build (R115,
    // from 300,000).
    static constexpr size_t edgeLimit = 2400000 * DocumentLimits::scale;
    static Topology rebuild(const Surface &surface, const Topology &previous, Id floor = 1);
    Adjacency adjacency(const Surface &surface) const;
    void validate(const Surface &surface) const;
    bool operator==(const Topology &) const = default;
};
struct EntityChanges {
    std::vector<Id> created, deleted, modified;
    std::map<Id, std::vector<Id>> descendants;
};
struct TopologyChanges {
    EntityChanges vertices, edges, faces, curves, guides;
};
TopologyChanges compareTopology(const Surface &before, const Topology &beforeTopology,
                                const Surface &after, const Topology &afterTopology,
                                bool inferEdges = true);
// Split one edge in every incident oriented loop and loose wire, atomically.
Id splitEdge(Surface &surface, const EdgeRecord &edge, double fraction);
} // namespace sketchy
