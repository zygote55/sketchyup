#pragma once
#include "geometry/boolean.hpp"
namespace sketchy {
struct SolidSplitResult {
    BooleanResult targetOnly, toolOnly, overlap;
};
// Three mutually exclusive material regions. Provenance always references the
// original target (operand 0) or tool (operand 1), including tool-minus-target.
SolidSplitResult splitSolids(const Surface &target, const Surface &tool);
// Filled outer boundaries of the union. Removes enclosed cavities and redundant
// material islands; exterior through-holes and disconnected bodies are retained.
BooleanResult outerShellSolids(const Surface &target, const Surface &tool);
} // namespace sketchy
