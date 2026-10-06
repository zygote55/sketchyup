#pragma once
#include "geometry/surface.hpp"
#include <QString>
namespace sketchy {
struct TextGeometrySettings {
    QString text;
    QString family{"DejaVu Sans"};
    QString style;
    double height{.1};       // Nominal font em height in metres.
    double depth{};          // Zero yields planar faces; positive values extrude along +Z.
    double lineSpacing{1.2}; // Baseline spacing in em heights.
    bool allowSubstitution{};
};
struct TextFontUse {
    QString family, style, fingerprint;
    size_t glyphs{};
};
struct TextGeometry {
    std::vector<Surface> regions;
    std::vector<TextFontUse> fonts;
    QString requestedFamily, actualFamily, actualStyle;
    bool substituted{}, fallback{};
    size_t glyphs{}, contours{}, points{}, holes{}, lines{};
    double curveTolerance{}, advanceWidth{};
};
// Uses Qt's full Unicode shaping and local fonts. Requires a QGuiApplication
// and stays in its calling thread; the result contains no live font objects.
TextGeometry shapeTextGeometry(const TextGeometrySettings &settings);
} // namespace sketchy
