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
// (docs/verification/R082w-memory-budget-v2.md). The unique values were raised in 1.7.0
// (R110, ADR 0005 "Raised unique geometry limits"); the memory evidence for them is
// docs/verification/R082w-memory-budget-v3.md. The materialized values did not change.
struct DocumentLimits {
    static constexpr std::size_t scale = SKETCHYUP_BENCHMARK_LIMIT_SCALE;
    static constexpr LimitCounts unique{0,
                                        10000 * scale,
                                        300000 * scale,
                                        300000 * scale,
                                        300000 * scale,
                                        900000 * scale,
                                        10000 * scale,
                                        10000 * scale};
    static constexpr LimitCounts materialized{20000 * scale,  40000 * scale,
                                              600000 * scale, 600000 * scale,
                                              600000 * scale, 900000 * scale,
                                              40000 * scale,  40000 * scale};
    // One component definition's own members (R110, 1.7.0, owner decision 2026-11-01). Editing a
    // component replaces its whole record, so the undo charge of one edit is at least the
    // definition's size, before and after. Vertices, faces, wires and edges are two thirds of the
    // document's unique values (200,000 and 600,000). Bodies, curves and guides keep the unique
    // values. The placement count is not checked here.
    static constexpr LimitCounts definition{0,
                                            10000 * scale,
                                            200000 * scale,
                                            200000 * scale,
                                            200000 * scale,
                                            600000 * scale,
                                            10000 * scale,
                                            10000 * scale};
};
static_assert(SKETCHYUP_BENCHMARK_LIMIT_SCALE >= 1);
// A document's unique geometry never exceeds its materialized totals, so the unique limit can
// be reached only in a document that also fits the materialized one.
static_assert(DocumentLimits::unique.placements <= DocumentLimits::materialized.placements);
static_assert(DocumentLimits::unique.bodies <= DocumentLimits::materialized.bodies);
static_assert(DocumentLimits::unique.vertices <= DocumentLimits::materialized.vertices);
static_assert(DocumentLimits::unique.faces <= DocumentLimits::materialized.faces);
static_assert(DocumentLimits::unique.wires <= DocumentLimits::materialized.wires);
static_assert(DocumentLimits::unique.edges <= DocumentLimits::materialized.edges);
static_assert(DocumentLimits::unique.curves <= DocumentLimits::materialized.curves);
static_assert(DocumentLimits::unique.guides <= DocumentLimits::materialized.guides);
// A definition's own members are part of the unique geometry, so no definition limit exceeds the
// unique limit it sits under.
static_assert(DocumentLimits::definition.bodies <= DocumentLimits::unique.bodies);
static_assert(DocumentLimits::definition.vertices <= DocumentLimits::unique.vertices);
static_assert(DocumentLimits::definition.faces <= DocumentLimits::unique.faces);
static_assert(DocumentLimits::definition.wires <= DocumentLimits::unique.wires);
static_assert(DocumentLimits::definition.edges <= DocumentLimits::unique.edges);
static_assert(DocumentLimits::definition.curves <= DocumentLimits::unique.curves);
static_assert(DocumentLimits::definition.guides <= DocumentLimits::unique.guides);
// Placement and unique geometry are document-wide classes; ComponentDefinition bounds one
// definition's own members (see DocumentLimits::definition).
enum class LimitClass { UniqueGeometry, Placement, ComponentDefinition };
// D06 diagnostics. Throws std::runtime_error naming the first exceeded quantity
// (placements, bodies, vertices, faces, wires, edges, curves, guides), its
// attempted and maximum values and the bounded context, e.g.
// "Component placement exceeds document placement limits: vertices would be
// 600,052 of 600,000 (placing definition 'Chair' #12 into group #3)". The component definition
// class reads the same way: "Component definition exceeds component definition limits: vertices
// would be 200,001 of 200,000 (definition 'Chair' #3)".
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
