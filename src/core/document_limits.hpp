#pragma once
#include <cstddef>
namespace sketchy {
// Aggregate limits for materialized document records. Geometry kernel limits
// remain independent: an aggregate allowance must not widen a single-body edit.
struct DocumentLimits {
    static constexpr std::size_t bodies = 10000;
    static constexpr std::size_t vertices = 100000;
    static constexpr std::size_t faces = 100000;
    static constexpr std::size_t wires = 100000;
    static constexpr std::size_t edges = 300000;
    static constexpr std::size_t curves = 10000;
    static constexpr std::size_t guides = 10000;
};
} // namespace sketchy
