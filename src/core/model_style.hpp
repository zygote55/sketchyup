#pragma once
#include <array>
#include <cmath>
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
    }
};
} // namespace sketchy
