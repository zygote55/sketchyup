#pragma once
#include "geometry/surface.hpp"
namespace sketchy {
struct MeasuredPage {
    double widthMm{297}, heightMm{210}, marginMm{10}, scaleDenominator{100};
    // Orthonormal frame: right/up in the page; larger depth is nearer the viewer.
    Vec3 origin{}, right{1, 0, 0}, up{0, 1, 0}, towardEye{0, 0, 1};
    bool includeHidden{};
    void validate() const;
    // x/y in physical page millimetres; z remains world depth in metres.
    Vec3 project(Vec3 world) const;
};
struct MeasuredEdge {
    Vec3 a, b;
    size_t source{};
};
struct MeasuredTriangle {
    std::array<Vec3, 3> vertices;
};
struct MeasuredLine {
    Vec3 a, b;
    size_t source{};
    bool hidden{};
};
struct MeasuredLines {
    std::vector<MeasuredLine> lines;
    size_t inputEdges{}, occluders{}, hiddenIntervals{}, collapsedEdges{}, candidateChecks{};
};
// Exact interval clipping against projected opaque triangles, with bounded grid/work budgets.
MeasuredLines measuredHiddenLines(const MeasuredPage &page, const std::vector<MeasuredEdge> &edges,
                                  const std::vector<MeasuredTriangle> &opaqueTriangles);
} // namespace sketchy
