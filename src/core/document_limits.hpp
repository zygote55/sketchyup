#pragma once
#include <cstddef>
// Benchmark-only hook: the opt-in placement-scaling harness compiles a private
// copy of the core with a larger factor. Product targets never define it, so
// every production limit below is unchanged.
#ifndef SKETCHYUP_BENCHMARK_LIMIT_SCALE
#define SKETCHYUP_BENCHMARK_LIMIT_SCALE 1
#endif
namespace sketchy {
// Aggregate limits for materialized document records. Geometry kernel limits
// remain independent: an aggregate allowance must not widen a single-body edit.
struct DocumentLimits {
    static constexpr std::size_t scale = SKETCHYUP_BENCHMARK_LIMIT_SCALE;
    static constexpr std::size_t bodies = 10000 * scale;
    static constexpr std::size_t vertices = 100000 * scale;
    static constexpr std::size_t faces = 100000 * scale;
    static constexpr std::size_t wires = 100000 * scale;
    static constexpr std::size_t edges = 300000 * scale;
    static constexpr std::size_t curves = 10000 * scale;
    static constexpr std::size_t guides = 10000 * scale;
};
static_assert(SKETCHYUP_BENCHMARK_LIMIT_SCALE >= 1);
} // namespace sketchy
