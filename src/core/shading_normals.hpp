#pragma once
#include "core/model.hpp"
namespace sketchy {
// Derived corner normals; never changes authoritative geometry or tessellation.
// Hidden faces contribute like visible faces. Only explicit smooth, consistently
// oriented two-face edges join a vertex fan; soft/hidden alone never do.
class ShadingNormals {
  public:
    explicit ShadingNormals(const Body &body);
    Vec3 corner(Id face, Id vertex) const;
    std::array<Vec3, 3> triangle(const Triangle &triangle) const;

  private:
    using Key = std::array<std::int64_t, 2>;
    struct FaceNormals {
        Vec3 normal, origin, u, v;
        std::map<Id, Vec3> corners;
        std::map<Key, Vec3> projected;
        Key key(Vec3 point) const;
    };
    std::map<Id, FaceNormals> faces_;
};
} // namespace sketchy
