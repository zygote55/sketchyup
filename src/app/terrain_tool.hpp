#pragma once
// R096.f1 (UX_DESIGN §15.1–15.2, §15.7; ADR 0169 §5, §8): the terrain tools' options, shared by
// the viewport (viewport_sculpt.cpp), the tool options strip (terrain_tool_strip.cpp) and the
// window. Values are model metres; the strip and the Measurements box parse them with the box's
// syntax and units.
#include "core/terrain_sculpt.hpp"
#include <QString>
namespace sketchy {
struct TerrainToolOptions {
    enum class Scope { Brush, Region };
    Scope scope{Scope::Brush};
    double radius{5};                          // brush radius, 0.01 to 1,000 m
    TerrainFalloff falloff{TerrainFalloff::Smooth};
    double amount{0.5};                        // Sculpt: raise by this (Ctrl lowers), metres
    double strength{0.5};                      // Smooth: 0.05 to 1
    int passes{1};                             // Smooth: 1 to 8 iterations
    double falloffWidth{1};                    // Region: band outside the polygon, 0 to 1,000 m
    bool operator==(const TerrainToolOptions &) const = default;
};
// Bounds the strip and the box enforce (the command's own bounds, UX §15.2 for strength).
inline constexpr double terrainToolStrengthMin = 0.05;
// `[` and `]` change the radius by 10 % (UX §15.1).
inline constexpr double terrainToolRadiusStep = 1.1;
} // namespace sketchy
