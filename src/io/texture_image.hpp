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
    // largest asset (AssetPayload::limit). Decoded RGBA is 256 MiB at that size. The viewport's decoded
    // budget (R118, 1 GiB) holds one such image at full size; a model with more reduces the largest.
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
// The stored size of an image, read from its header without decoding pixels (R118). The status is the
// one decodeTextureImage reports before it reads pixels; the size is zero unless it is Ready.
struct TextureImageProbe {
    TextureImageStatus status;
    int width{}, height{};
};
TextureImageProbe probeTextureImage(const AssetRecord &asset);
// A copy of `image` at width by height, which must not exceed the source in either dimension. Each
// output pixel averages the source pixels it covers, weighted by alpha, so transparent pixels do not
// darken their neighbours (R118).
std::shared_ptr<const TextureImage> reduceTextureImage(const TextureImage &image, int width,
                                                       int height);
// PNG without color/orientation metadata, suitable for an embedded glTF image.
QByteArray encodeTexturePng(const TextureImage &image);
} // namespace sketchy
