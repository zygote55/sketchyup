#pragma once
#include "io/procedural_textures.hpp"
#include <QByteArray>
#include <QString>
#include <span>
#include <string_view>

namespace sketchy {

// One textured material preset (ADR 0174 Q1, the owner-approved list of 2026-10-27). The
// texture is either an embedded CC0 JPEG under :/sketchyup/textures/ or a generated one.
struct TexturePreset {
    std::string_view name;
    std::string_view licence;      // Shown next to the preset: source, licence and credit.
    std::string_view resource;     // File name under resources/textures; empty when generated.
    ProceduralTexture generated{}; // Used only when resource is empty.
    double tileWidth, tileHeight;  // Metres covered by one repeat of the texture.
    std::string_view tint;         // #RRGGBB, sRGB.
    double opacity;                // Percent, as in the colour presets.
};

// The fifteen textured presets, in the order the Materials library shows them under Textured.
std::span<const TexturePreset> texturePresets();

// The texture bytes of a preset: the embedded JPEG, or the generator's output. Each call returns
// the same bytes.
QByteArray texturePresetBytes(const TexturePreset &preset);

inline constexpr std::string_view texturePresetMediaType = "image/jpeg";

} // namespace sketchy
