#pragma once
#include "geometry/drawing.hpp"
#include "geometry/topology.hpp"
namespace sketchy {
enum class CurveKind { Circle, Arc, Pie };
// Parameter frame is affine: world-space construction inside a scaled context
// retains its original radius while the two axes carry the inverse transform.
struct Curve {
    CurveKind kind{CurveKind::Circle};
    Vec3 center{}, xAxis{1, 0, 0}, yAxis{0, 1, 0};
    double radius{1}, startAngle{}, sweepAngle{2 * 3.14159265358979323846};
    unsigned segments{48};
    std::vector<OrientedEdge> edges;
    Vec3 point(double angle) const;
    Vec3 tangent(double angle) const;
    std::vector<std::array<Vec3, 2>> chords() const;
    bool operator==(const Curve &) const = default;
};
Curve centerCurve(CurveKind kind, const DrawingPlane &plane, double radius, double start,
                  double sweep, unsigned segments);
Curve twoPointArc(Vec3 start, Vec3 end, Vec3 normal, double bulge, unsigned segments);
Curve threePointArc(Vec3 start, Vec3 through, Vec3 end, unsigned segments);
// Returns false when editable geometry no longer covers the derived outline.
// Work is bounded across all curves in a body by the caller's shared budget.
bool bindCurve(Curve &curve, const Surface &surface, const Topology &topology, size_t &budget);
void validateCurves(const std::map<Id, Curve> &curves, const Surface &surface,
                    const Topology &topology);
EntityChanges compareCurves(const std::map<Id, Curve> &before, const std::map<Id, Curve> &after);
} // namespace sketchy
