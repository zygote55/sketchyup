#pragma once
#include "geometry/inference.hpp"
namespace sketchy {
enum class DirectionKind {
    RedAxis,
    GreenAxis,
    BlueAxis,
    Parallel,
    Perpendicular,
    Tangent,
    FromPoint
};
const char *directionLabel(DirectionKind kind);
struct DirectionConstraint {
    DirectionKind kind{};
    Vec3 origin{}, direction{1, 0, 0};
    Id body{}, entity{};
    bool operator==(const DirectionConstraint &) const = default;
};
struct DirectionCandidate {
    DirectionConstraint constraint;
    Vec3 point;
    double pixels{};
};
// Closest point on the projected infinite line; the returned world point lies
// exactly on its constraint. No pixel radius participates in geometry tolerance.
std::optional<DirectionCandidate> projectDirection(const DirectionConstraint &constraint,
                                                   const InferenceCamera &camera, double x,
                                                   double y);
std::vector<DirectionCandidate>
directionCandidates(const InferenceCamera &camera, double x, double y, Vec3 anchor,
                    const DrawingPlane &plane,
                    const std::vector<DirectionConstraint> &references = {},
                    std::optional<Vec3> fromPoint = {}, double radius = 8);
// A positive length from anchor, staying on the constrained line and choosing
// the solution nearest the current preview. Rejects an unreachable length.
Vec3 constrainedLength(const DirectionConstraint &constraint, Vec3 anchor, Vec3 preview,
                       double length);
std::vector<DirectionConstraint> edgeDirections(const Document &doc,
                                                const InferenceCandidate &reference, Vec3 anchor,
                                                const DrawingPlane &plane);
class DirectionLocks {
  public:
    std::optional<DirectionConstraint> current() const { return held_ ? held_ : persistent_; }
    bool holding() const { return bool(held_); }
    void toggle(DirectionConstraint constraint);
    void hold(DirectionConstraint constraint);
    void release() { held_.reset(); }
    void clear() {
        held_.reset();
        persistent_.reset();
    }

  private:
    std::optional<DirectionConstraint> persistent_, held_;
};
} // namespace sketchy
