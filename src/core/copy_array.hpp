#pragma once
#include "core/transform_selection.hpp"
namespace sketchy {
inline constexpr unsigned maxArrayCopies = 100;
enum class ArrayMode { Linear, Radial };
struct CopyArray {
    ArrayMode mode{ArrayMode::Linear};
    Vec3 delta{}, axis{0, 0, 1};
    double angle{};     // radians per step, or total sweep when divide is true
    unsigned copies{1}; // new copies; original makes total instances copies + 1
    bool divide{};      // copies equal intervals, including the endpoint
};
struct ArrayResult {
    ChangeReport changes;
    std::vector<TransformResult> instances; // ordered copies; each carries identity maps
};
ArrayResult copyArraySelected(Document &doc, const TransformTargets &targets,
                              const CopyArray &array, Vec3 pivot = {},
                              TransformSpace space = TransformSpace::World);
} // namespace sketchy
