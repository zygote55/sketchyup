#pragma once
#include "geometry/drawing.hpp"
#include "geometry/topology.hpp"
namespace sketchy {
enum class GuideKind { Point, Line };
// Construction geometry is separate from Surface/Topology. A line is infinite;
// origin is bounded like a model point and direction is a canonical unit vector.
struct Guide {
    GuideKind kind{GuideKind::Point};
    Vec3 origin{}, direction{};
    void validate() const;
    bool operator==(const Guide &) const = default;
};
Guide guidePoint(Vec3 point);
Guide guideLine(Vec3 origin, Vec3 direction);
// Finite portion inside the editable world cube, for rendering and acquisition.
std::array<Vec3, 2> boundedGuideLine(const Guide &line);
Guide offsetGuide(const Guide &line, Vec3 normal, double distance);
Guide angledGuide(const DrawingPlane &plane, double angle);
double measureDistance(Vec3 start, Vec3 end);
double measureAngle(Vec3 origin, Vec3 first, Vec3 second, Vec3 normal);
void validateGuides(const std::map<Id, Guide> &guides, const Surface &surface);
EntityChanges compareGuides(const std::map<Id, Guide> &before, const std::map<Id, Guide> &after);
} // namespace sketchy
