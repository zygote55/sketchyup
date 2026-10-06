#pragma once
#include "geometry/topology.hpp"
#include <set>
#include <string>
namespace sketchy {
class OrientationError : public std::runtime_error {
    std::string code_;
    Id face_{}, edge_{};

  public:
    OrientationError(std::string code, std::string message, Id face = 0, Id edge = 0)
        : std::runtime_error(std::move(message)), code_(std::move(code)), face_(face), edge_(edge) {
    }
    const std::string &code() const { return code_; }
    Id face() const { return face_; }
    Id edge() const { return edge_; }
};
struct FaceOrientationResult {
    Surface surface;
    std::vector<Id> reversed;
    std::vector<Id> connected;
};
// Reverse all loops of explicit faces. Geometry/edge identities are unchanged;
// the publication layer must swap front/back materials for each reversed face.
FaceOrientationResult reverseFaces(const Surface &surface, const Topology &topology,
                                   const std::set<Id> &faces);
// Preserve the seed face's winding and orient its edge-connected component.
// Open sheets are supported. Non-manifold edges and contradictory orientation
// cycles reject atomically. This never guesses outward/cavity orientation.
FaceOrientationResult orientFaces(const Surface &surface, const Topology &topology, Id seed);
} // namespace sketchy
