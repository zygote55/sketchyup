#pragma once
#include "geometry/topology.hpp"
namespace sketchy {
struct TopologyEdit {
    Surface surface;
    std::map<Id, std::vector<Id>> faces, vertices, edges;
};
TopologyEdit eraseFace(const Surface &source, Id face);
TopologyEdit eraseEdge(const Surface &source, const Topology &topology, Id edge);
TopologyEdit cleanupCoincident(const Surface &source, const Topology &topology);
} // namespace sketchy
