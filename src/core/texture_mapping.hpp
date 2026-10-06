#pragma once
#include "core/transform.hpp"

namespace sketchy {
// Unwrapped repeats. (0,0) addresses the image's top-left; +V runs down the image.
// Keep these as doubles until a renderer/exporter chooses its vertex format.
struct TextureCoordinate {
    double u{}, v{};
    bool operator==(const TextureCoordinate &) const = default;
};

// Affine projection in the same coordinate system as the sampled geometry:
// uv(p) = offset + {dot(p-origin, uGradient), dot(p-origin, vGradient)}.
// Gradients are repeats/metre, not tangent vectors. Keeping a nearby origin
// avoids subtracting large independently rounded UV offsets at site coordinates.
struct TextureMapping {
    Vec3 origin{};
    Vec3 uGradient{1, 0, 0}, vGradient{0, 1, 0};
    TextureCoordinate offset{};
    void validate() const;
    TextureCoordinate coordinates(Vec3 point) const;
    bool operator==(const TextureMapping &) const = default;
};

// Construct an orthogonal projection. Signed sizes are metres per image repeat;
// negative sizes mirror that image axis. Positive rotation turns the image's
// +U direction toward +V around normal. Normal/tangent need not be unit vectors.
TextureMapping planarTextureMapping(Vec3 origin, Vec3 normal, Vec3 tangent, double width,
                                    double height, double rotationRadians = 0,
                                    TextureCoordinate offset = {});

// Three non-collinear pins define a general affine mapping in their plane.
// Projection along that plane's normal retains the same UV, including on other
// faces. Perspective/four-pin warps are deliberately outside this representation.
TextureMapping pinnedTextureMapping(const std::array<Vec3, 3> &points,
                                    const std::array<TextureCoordinate, 3> &coordinates);

// Re-express the mapping when geometry coordinates change by oldToNew.
// For every point p: result.coordinates(oldToNew.point(p)) == source.coordinates(p)
// within floating-point error, including non-uniform scale, shear and reflection.
// A normal body/instance placement does not need this conversion: sample local p.
TextureMapping transformTextureMapping(const TextureMapping &source, const Transform &oldToNew);
} // namespace sketchy
