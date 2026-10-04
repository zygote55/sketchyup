#pragma once
#include "core/model.hpp"
#include <set>
namespace sketchy {
struct GeometrySubset {
    bool whole{};
    std::set<Id> faces, edges, vertices, explicitVertices, guides;
};
void includeGeometryFace(const Body &body, GeometrySubset &part, Id face);
std::shared_ptr<Body> extractGeometry(const Body &source, const GeometrySubset &part);
} // namespace sketchy
