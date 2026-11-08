#pragma once
#include "geometry/surface.hpp"
#include "geometry/topology.hpp"
#include <cstddef>
#include <string>
namespace sketchy {
// The counts one imported body holds (R113, D-102). A body takes at most Surface::bodyLimit faces
// and vertices, and Topology::edgeLimit edges. The edge count is bounded above by the loop
// corners plus the wire segments, since each corner and each wire adds at most one edge. A mesh
// over one body does not refuse: it continues in the next body (see continuedBodyName).
struct BodyBudget {
    std::size_t faces{}, vertices{}, edges{};
    // Whether this many more faces, vertices and edges fit in the body.
    bool admits(std::size_t moreFaces, std::size_t moreVertices, std::size_t moreEdges) const {
        return faces + moreFaces <= Surface::bodyLimit &&
               vertices + moreVertices <= Surface::bodyLimit &&
               edges + moreEdges <= Topology::edgeLimit;
    }
    void add(std::size_t moreFaces, std::size_t moreVertices, std::size_t moreEdges) {
        faces += moreFaces;
        vertices += moreVertices;
        edges += moreEdges;
    }
};
// The name of body `index` (from 1) of a mesh that the one-body limit split: "Name", then
// "Name (2)", "Name (3)" and so on.
inline std::string continuedBodyName(const std::string &name, std::size_t index) {
    return index <= 1 ? name : name + " (" + std::to_string(index) + ")";
}
} // namespace sketchy
