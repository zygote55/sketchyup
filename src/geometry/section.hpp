#pragma once
#include "core/transform.hpp"
#include <limits>
#include <optional>
#include <string>
namespace sketchy {
// The retained half-space is dot(normal, point) + offset >= 0.
struct SectionPlane {
    Vec3 normal{0, 0, 1};
    double offset{};
    bool operator==(const SectionPlane &) const = default;
    void validate() const;
    double distance(Vec3 point) const { return dot(normal, point) + offset; }
    static SectionPlane through(Vec3 point, Vec3 normal);
    SectionPlane transformed(const Transform &localToWorld) const;
};
struct SectionCut {
    Id id{};
    SectionPlane plane;
    bool operator==(const SectionCut &) const = default;
};
struct SectionVertex {
    Vec3 point;
    // Weights in the original input triangle, retained through successive cuts.
    // Generated cap vertices have zero weights and no source triangle.
    std::array<double, 3> weights{};
};
inline constexpr size_t noSectionSource = std::numeric_limits<size_t>::max();
struct SectionTriangle {
    std::array<SectionVertex, 3> vertices;
    Id face{};
    Id section{}; // Zero for original geometry; otherwise the cap's plane ID.
    size_t source{noSectionSource};
};
struct SectionEdge {
    Vec3 a, b;
    Id section{};
};
struct SectionMesh {
    std::vector<SectionTriangle> triangles;
    std::vector<SectionEdge> edges;
    // Open/ambiguous contours retain edges but deliberately receive no fill.
    std::vector<Id> unfilledSections;
};
inline constexpr size_t sectionPlaneLimit = 8, sectionInputTriangleLimit = 32768,
                        sectionOutputTriangleLimit = 262144;
// Derived geometry only: ordered half-space intersection, outward cap winding,
// even/odd nested holes, and original-triangle provenance for texture interpolation.
SectionMesh sectionMesh(const std::vector<Triangle> &source, const std::vector<SectionCut> &cuts);
bool sectionContains(Vec3 point, const std::vector<SectionCut> &cuts);
std::optional<std::array<Vec3, 2>> sectionSegment(Vec3 a, Vec3 b,
                                                  const std::vector<SectionCut> &cuts);
} // namespace sketchy
