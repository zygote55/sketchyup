#pragma once
#include <QByteArray>
#include <cstdint>

namespace sketchy {

// The six generated texture presets (ADR 0174 Q1, R100.b). Each makes a 512 by 512 neutral
// luminance tile that repeats seamlessly, from a fixed seed, and encodes it as JPEG at quality 85
// through Qt. The tint in the material carries the colour.
enum class ProceduralTexture : std::uint8_t {
    BrickBond,
    ConcreteCast,
    FloorTile600,
    PlasterFine,
    MetalBrushed,
    GlassReeded,
};

// The same bytes on every run and every machine with the same Qt JPEG encoder. The fixture
// SHA-256 values in tests/texture_preset_tests.cpp pin them.
QByteArray proceduralTextureJpeg(ProceduralTexture texture);

} // namespace sketchy
