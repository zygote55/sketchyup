#pragma once
#include <cstddef>
#include <string>
#include <string_view>
// Benchmark-only hook: the opt-in placement-scaling harness compiles a private
// copy of the core with a larger factor. Product targets never define it, so
// every production limit below is unchanged.
#ifndef SKETCHYUP_BENCHMARK_LIMIT_SCALE
#define SKETCHYUP_BENCHMARK_LIMIT_SCALE 1
#endif
namespace sketchy {
// Record counts compared with a limit class. Unique geometry ignores placements.
struct LimitCounts {
    std::size_t placements{}, bodies{}, vertices{}, faces{}, wires{}, edges{}, curves{}, guides{};
};
// Aggregate document limits (ADR 0005, R082.gg). Geometry kernel limits remain
// independent: an aggregate allowance must not widen a single-body edit.
//
// Unique geometry counts records outside every component placement plus each
// definition's members once; placement roots and projected members do not count.
// Materialized totals count every record after expanding all placements, nested
// placements included; `placements` counts materialized component instances.
// The materialized values bound the worst-case document's memory
// (docs/verification/R082w-memory-budget-v2.md).
struct DocumentLimits {
    static constexpr std::size_t scale = SKETCHYUP_BENCHMARK_LIMIT_SCALE;
    static constexpr LimitCounts unique{0,
                                        10000 * scale,
                                        100000 * scale,
                                        100000 * scale,
                                        100000 * scale,
                                        300000 * scale,
                                        10000 * scale,
                                        10000 * scale};
    static constexpr LimitCounts materialized{20000 * scale,  40000 * scale,
                                              600000 * scale, 600000 * scale,
                                              600000 * scale, 900000 * scale,
                                              40000 * scale,  40000 * scale};
};
static_assert(SKETCHYUP_BENCHMARK_LIMIT_SCALE >= 1);
enum class LimitClass { UniqueGeometry, Placement };
// D06 diagnostics. Throws std::runtime_error naming the first exceeded quantity
// (placements, bodies, vertices, faces, wires, edges, curves, guides), its
// attempted and maximum values and the bounded context, e.g.
// "Component placement exceeds document placement limits: vertices would be
// 600,052 of 600,000 (placing definition 'Chair' #12 into group #3)".
// Messages never exceed limitMessageBytes.
inline constexpr std::size_t limitMessageBytes = 256;
inline constexpr std::size_t limitNameBytes = 64;
// The first exceeded quantity's name, or empty when every count is within limits.
std::string_view exceededLimit(const LimitCounts &attempted, LimitClass limits);
void checkLimits(const LimitCounts &attempted, LimitClass limits, std::string_view operation,
                 std::string_view context);
// The terrain limit class (ADR 0168 decision 3): terrain triangles per document,
// counted apart from the modelling budget above. Throws std::runtime_error
// "<operation> exceeds the document terrain limit: terrain triangles would be
// 250,001 of 250,000".
inline constexpr std::size_t terrainTriangleLimit = 250000;
void checkTerrainLimit(std::size_t triangles, std::string_view operation);
// Locale-independent thousands separators ("250,000").
std::string groupedCount(std::size_t value);
// A UTF-8-safe name truncated to limitNameBytes with control characters replaced. Results that
// carry model names (R111) use it too.
std::string boundedName(std::string_view name);
// A single-quoted, UTF-8-safe name truncated to limitNameBytes with control
// characters replaced; the only user content a limit message may carry.
std::string quotedLimitName(std::string_view name);
// "definition 'Name' #12"
std::string definitionLimitContext(std::string_view name, std::size_t id);
} // namespace sketchy
