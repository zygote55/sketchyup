#pragma once
#include "geometry/surface.hpp"
#include <string>
namespace sketchy {
class IntersectionError : public std::runtime_error {
  public:
    IntersectionError(std::string code, const std::string &message)
        : std::runtime_error(message), code_(std::move(code)) {}
    const std::string &code() const { return code_; }

  private:
    std::string code_;
};
struct FaceIntersection {
    bool coplanar{};
    std::vector<std::array<Vec3, 2>> edges;
};
// Both faces must already be in the same coordinate frame. Returns the common
// line intervals, or common region boundary/contact edges for coplanar faces.
// Point contacts do not create zero-length geometry. Inputs remain unchanged.
FaceIntersection intersectFaces(const Surface &a, Id faceA, const Surface &b, Id faceB);
} // namespace sketchy
