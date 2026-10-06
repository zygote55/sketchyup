#pragma once
#include "core/transform.hpp"
#include "geometry/drawing.hpp"
#include <string>
namespace sketchy {
class PlacementError : public std::runtime_error {
  public:
    PlacementError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
struct FacePlacementOptions {
    Vec3 anchor;           // Host-local point, snapped only within plane tolerance.
    Vec3 tangent{1, 0, 0}; // Explicit host-local direction defining in-plane zero rotation.
    double rotationRadians{};
    Vec3 scale{1, 1, 1}; // Signed component scale in the orthonormal glue frame.
};
struct FacePlacement {
    Transform world;
    DrawingPlane hostFrame; // World-space orthonormal frame, physical face normal.
    Vec3 hostAnchor;        // Actual host-local anchor after plane projection.
    bool boundary{};
};
// Immutable placement only: this does not create bindings, cut holes or edit a
// document. Host placement affects the anchor/orientation, not component size.
// The source glue frame must be canonical (unit, orthogonal, right-handed).
// Existing face boundaries are allowed; points inside holes or outside reject.
FacePlacement componentPlacementOnFace(const Surface &host, Id face, const Transform &hostToWorld,
                                       const DrawingPlane &componentGlueFrame,
                                       const FacePlacementOptions &options);
} // namespace sketchy
