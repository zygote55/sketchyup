#pragma once
#include "geometry/cleanup.hpp"
namespace sketchy {
// Distances are signed along the selected face's local normal. The operation is
// staged; unsupported intersections never modify the input surface.
TopologyEdit pushPull(const Surface &source, Id face, double distance);
} // namespace sketchy
