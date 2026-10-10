#pragma once
#include "geometry/inference.hpp"
// Shared query core for InferenceIndex. Exposed only so test oracles can drive
// the identical candidate logic over an independently expanded primitive set.
namespace sketchy::inference_detail {
using Planes = std::array<std::array<double, 4>, 6>;
Box bounds(Vec3 a, Vec3 b, Vec3 c);
Box join(Box a, Box b);
Planes frustum(const InferenceCamera &camera, double x, double y, double radius);
bool intersects(Box box, const Planes &planes);
// Geometry a scene body exposes to visibility and guide projection. `geometry`
// supplies curves (identical to the scene record's), `record` supplies guides.
struct BodyView {
    const std::map<Id, std::vector<Id>> *vertexEdges{};
    const Body *geometry{}, *record{};
    const Transform *world{};
};
// Calls `fn` for every primitive whose world box intersects the planes, within
// the context (zero for all). Results must not depend on the visiting order.
using Visitor = std::function<void(const Planes &, Id,
                                   const std::function<void(Id, const Primitive &)> &)>;
InferenceResult runQuery(const InferenceQuery &query, const Visitor &visit,
                         const std::function<BodyView(Id)> &body);
} // namespace sketchy::inference_detail
