#pragma once
#include "core/asset_records.hpp"
#include "core/texture_mapping.hpp"
#include <QByteArray>
#include <string_view>

namespace sketchy {
// Immutable decoded pixels: row zero is the top row; RGBA is unpremultiplied,
// RGB is sRGB, and alpha is linear coverage. Original managed bytes stay intact.
class TextureImage {
  public:
    // R115 (2026-11-07): 8192 by 8192, the largest image a 64 MiB asset can hold in a PNG, which is the
    // largest asset (AssetPayload::limit). Decoded RGBA is 256 MiB at that size, and the viewport's
    // decoded budget (R118) does not hold one such image yet.
    static constexpr int dimensionLimit = 8192;
    static constexpr size_t byteLimit = size_t(dimensionLimit) * size_t(dimensionLimit) * 4;
    TextureImage(int width, int height, std::vector<std::uint8_t> rgba);
    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint8_t> &rgba() const { return rgba_; }
    bool hasTransparency() const { return transparent_; }
    // Repeat addressing and bilinear interpolation, decoding RGB to linear
    // before filtering. UVs are unwrapped; negative repeats are supported.
    std::array<double, 4> sampleLinear(TextureCoordinate uv) const;

  private:
    int width_, height_;
    const std::vector<std::uint8_t> rgba_;
    bool transparent_{};
};
enum class TextureImageStatus { Ready, Missing, Unsupported, Invalid, TooLarge };
std::string_view textureImageStatusName(TextureImageStatus status);
struct TextureImageResult {
    TextureImageStatus status;
    std::shared_ptr<const TextureImage> image;
};
// Only complete static PNG (at most 8 bits/channel) and JPEG assets are decoded.
// No filename, URL, external lookup, global codec setting or GUI is involved.
TextureImageResult decodeTextureImage(const AssetRecord &asset);
// PNG without color/orientation metadata, suitable for an embedded glTF image.
QByteArray encodeTexturePng(const TextureImage &image);
} // namespace sketchy
