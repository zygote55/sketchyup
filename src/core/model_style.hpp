#pragma once
#include "geometry/surface.hpp"
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace sketchy {
enum class ModelStyleMode { Textured, Shaded, Monochrome, Wireframe, XRay };
inline std::string_view styleModeCode(ModelStyleMode mode) {
    switch (mode) {
    case ModelStyleMode::Textured:
        return "textured";
    case ModelStyleMode::Shaded:
        return "shaded";
    case ModelStyleMode::Monochrome:
        return "monochrome";
    case ModelStyleMode::Wireframe:
        return "wireframe";
    case ModelStyleMode::XRay:
        return "xray";
    }
    throw std::runtime_error("Unknown model display mode");
}
inline ModelStyleMode parseStyleMode(std::string_view code) {
    for (auto mode : {ModelStyleMode::Textured, ModelStyleMode::Shaded, ModelStyleMode::Monochrome,
                      ModelStyleMode::Wireframe, ModelStyleMode::XRay})
        if (styleModeCode(mode) == code)
            return mode;
    throw std::runtime_error("Choose textured, shaded, monochrome, wireframe or xray");
}
// The named dash patterns of the edge stroke (ADR 0174 §2). A bounded run list is not offered.
enum class EdgeDash { Solid, Dashed, Dotted, DashDot };
inline std::string_view edgeDashCode(EdgeDash dash) {
    switch (dash) {
    case EdgeDash::Solid:
        return "solid";
    case EdgeDash::Dashed:
        return "dashed";
    case EdgeDash::Dotted:
        return "dotted";
    case EdgeDash::DashDot:
        return "dashDot";
    }
    throw std::runtime_error("Unknown edge dash pattern");
}
inline EdgeDash parseEdgeDash(std::string_view code) {
    for (auto dash : {EdgeDash::Solid, EdgeDash::Dashed, EdgeDash::Dotted, EdgeDash::DashDot})
        if (edgeDashCode(dash) == code)
            return dash;
    throw std::runtime_error("Choose solid, dashed, dotted or dashDot for the edge dash");
}
// The extended edge stroke (ADR 0174 §2), stored as the optional `edgeStroke` field of the style.
// Its width uses profileWidth semantics. `texture` is a managed asset id, zero when none; a missing
// or mismatched texture is reported by the style commands and never replaced (ADR 0174 §5).
struct EdgeStroke {
    double width{2};
    double opacity{1};
    EdgeDash dash{EdgeDash::Solid};
    Id texture{};
    bool operator==(const EdgeStroke &) const = default;
    void validate() const {
        if (!std::isfinite(width) || width < 1 || width > 8)
            throw std::runtime_error("Edge width must be from one to eight logical pixels");
        if (!std::isfinite(opacity) || opacity < .01 || opacity > 1)
            throw std::runtime_error("Edge opacity must be from 0.01 to 1");
        edgeDashCode(dash);
    }
};
// Document-owned presentation. RGB channels are display sRGB, not application theme colors.
// Lengths are metres; profileWidth is logical screen pixels.
struct ModelStyle {
    ModelStyleMode mode{ModelStyleMode::Textured};
    std::array<float, 3> background{240.f / 255, 241.f / 255, 236.f / 255};
    std::array<float, 3> ground{.86f, .87f, .82f};
    std::array<float, 3> front{.82f, .84f, .8f}, back{.55f, .62f, .68f};
    std::array<float, 3> edge{.18f, .22f, .2f};
    bool groundVisible{}, gridVisible{true}, axesVisible{true}, edgesVisible{true}, profiles{};
    double groundHeight{}, profileWidth{2}, xrayOpacity{.25};
    // Optional since schema 31 (edge-styles-v1). Absent means the 14-field record alone.
    std::optional<EdgeStroke> edgeStroke;
    bool operator==(const ModelStyle &) const = default;
    void validate() const {
        styleModeCode(mode);
        for (const auto &color : {background, ground, front, back, edge})
            for (auto channel : color)
                if (!std::isfinite(channel) || channel < 0 || channel > 1)
                    throw std::runtime_error(
                        "Style colors require finite RGB channels from zero to one");
        if (!std::isfinite(groundHeight) || std::abs(groundHeight) > 1e6)
            throw std::runtime_error("Style ground height must be within one million metres");
        if (!std::isfinite(profileWidth) || profileWidth < 1 || profileWidth > 8)
            throw std::runtime_error("Profile width must be from one to eight logical pixels");
        if (!std::isfinite(xrayOpacity) || xrayOpacity < .01 || xrayOpacity > .95)
            throw std::runtime_error("X-ray opacity must be from 0.01 to 0.95");
        if (edgeStroke)
            edgeStroke->validate();
    }
};
} // namespace sketchy
