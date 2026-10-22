#pragma once
// R096.f1 (UX_DESIGN §15.1–15.2, §15.7; ADR 0169 §5, §8): the terrain tools' options, shared by
// the viewport (viewport_sculpt.cpp), the tool options strip (terrain_tool_strip.cpp) and the
// window. Values are model metres; the strip and the Measurements box parse them with the box's
// syntax and units.
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
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
    // R096.f2 Stamp pad (UX §15.4): the grading and the pad offset below the footprint (a slab
    // build-up, remembered across sessions).
    TerrainStampMode padMode{TerrainStampMode::Daylight};
    double cutSlope{0.5}, fillSlope{0.5};      // rise over run, 0.01 to 100 (1:2 is 0.5)
    double maxDistance{100};                   // daylight: above 0, at most 1,000 m
    double padWidth{3};                        // fixed width: 0.01 to 1,000 m
    double padOffset{0};                       // 0 to 100 m below the footprint
    // R096.f2 Drape (UX §15.5).
    bool drapeOverlay{};                       // Imprint (false) or Overlay
    bool paintRegions{true};                   // Imprint: faces paint their region
    double lift{0.01};                         // Overlay: 0 to 1,000 m
    bool operator==(const TerrainToolOptions &) const = default;
};
// A slope as the strip and the Measurements box take it: rise:run ("1:2"), a ratio ("0.5") or an
// angle ("26.6°"), as rise over run from 0.01 to 100. Throws std::runtime_error with the reason.
double parseTerrainSlope(const QString &text);
// "1:2", "1:1.5", "2:1".
QString terrainSlopeText(double slope);
// Whether typed text is a slope rather than a length (it has ':' or '°').
inline bool looksLikeTerrainSlope(const QString &text) {
    return text.contains(':') || text.contains(QChar(0x00B0)) || text.endsWith("deg");
}
// Bounds the strip and the box enforce (the command's own bounds, UX §15.2 for strength).
inline constexpr double terrainToolStrengthMin = 0.05;
// `[` and `]` change the radius by 10 % (UX §15.1).
inline constexpr double terrainToolRadiusStep = 1.1;
} // namespace sketchy
