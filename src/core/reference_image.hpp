#pragma once
#include "core/transform.hpp"
namespace sketchy {
// A visual image plane, with no modeled vertices, edges or faces. Managed asset
// identity is distinct from placement; resolving pixels belongs to the IO layer.
struct ReferenceImage {
    Id asset{};
    double width{1}, height{1}, opacity{1};
    void validate() const;
    bool operator==(const ReferenceImage &) const = default;
};
// Normalized image coordinates: (0,0) is top left, (1,1) is bottom right.
using ImagePoint = std::array<double, 2>;
Vec3 referenceImagePoint(const ReferenceImage &image, ImagePoint point);
// Lower left, lower right, upper right, upper left in world space.
std::array<Vec3, 4> referenceImageCorners(const ReferenceImage &image, const Transform &world);
struct CalibratedReferenceImage {
    ReferenceImage image;
    Transform local;
};
// Uniformly resize physical dimensions to match the measured world-space distance.
// The first anchor stays fixed; local axes and the full parent affine placement remain.
CalibratedReferenceImage calibrateReferenceImage(const ReferenceImage &image,
                                                 const Transform &local, const Transform &parent,
                                                 ImagePoint first, ImagePoint second,
                                                 double knownLength);
} // namespace sketchy
