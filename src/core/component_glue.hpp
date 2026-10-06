#pragma once
#include "core/component_records.hpp"
#include "geometry/drawing.hpp"
namespace sketchy {
struct ResolvedComponentGlue {
    DrawingPlane frame;        // Orthonormal definition-local physical face frame.
    std::vector<Vec3> profile; // Definition-local outer outline, only for cutting behavior.
};
// The reference must name a face on a direct canonical member (not a nested
// definition reference). Anchors may lie in a ring's hole, but must be on its plane.
ResolvedComponentGlue resolveComponentGlue(const ComponentDefinition &definition);
} // namespace sketchy
