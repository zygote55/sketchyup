// Object placements (ADR 0171 section 3.1): the worker composes the IfcLocalPlacement chain itself
// so that it can refuse what it cannot compose (grid and linear placements, cycles, non-finite
// numbers) instead of placing an element at the origin, and so that groups without geometry (a
// storey) have the same world matrix the library gives the elements in them.
#pragma once

#include <array>
#include <optional>
#include <string>

#include "ifc/access.hpp"

namespace sketchyup::ifc {

using Mat = std::array<double, 16>;  // row-major 4x4, translation in the last column

Mat identityMatrix();
Mat multiply(const Mat& a, const Mat& b);
// The inverse of an affine matrix (last row 0 0 0 1); nullopt when singular.
std::optional<Mat> inverseAffine(const Mat& m);
bool finiteMatrix(const Mat& m);

// The matrix of one IfcAxis2Placement3D or IfcAxis2Placement2D (a context's WorldCoordinateSystem, for
// example), in metres; nullopt when it cannot be read.
std::optional<Mat> axisPlacementMatrix(const Inst& placement, double metresPerUnit);

// The world matrix of an IfcObjectPlacement, in metres. nullopt with `reason` set when it cannot be
// composed: "gridPlacement", "linearPlacement", "placementCycle", "badPlacement", "unknownPlacement".
std::optional<Mat> placementWorld(const Inst& placement, double metresPerUnit, std::string& reason);

}  // namespace sketchyup::ifc
