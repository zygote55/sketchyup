#include "io/texture_image.hpp"
#include "io/assets.hpp"
#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QImageReader>
#include <QtEndian>
#include <algorithm>
#include <cstring>

namespace sketchy {
namespace {
bool bounded(int width, int height) {
    return width > 0 && height > 0 && width <= TextureImage::dimensionLimit &&
           height <= TextureImage::dimensionLimit &&
           size_t(width) * size_t(height) * 4 <= TextureImage::byteLimit;
}
double linear(std::uint8_t byte) {
    const double value = byte / 255.;
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
TextureImageStatus pngEnvelope(const QByteArray &bytes) {
    if (!bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
        return TextureImageStatus::Invalid;
    qsizetype offset = 8;
    int chunks = 0;
    bool header = false, data = false;
    while (offset + 12 <= bytes.size() && ++chunks <= 4096) {
        const quint32 length = qFromBigEndian<quint32>(bytes.constData() + offset);
        if (length > quint32(bytes.size() - offset - 12))
            return TextureImageStatus::Invalid;
        const auto type = QByteArrayView(bytes.constData() + offset + 4, 4);
        if (!header && (type != "IHDR" || length != 13))
            return TextureImageStatus::Invalid;
        if (type == "IHDR") {
            if (header || length != 13)
                return TextureImageStatus::Invalid;
            const auto width = qFromBigEndian<quint32>(bytes.constData() + offset + 8);
            const auto height = qFromBigEndian<quint32>(bytes.constData() + offset + 12);
            if (!width || !height)
                return TextureImageStatus::Invalid;
            if (width > TextureImage::dimensionLimit || height > TextureImage::dimensionLimit)
                return TextureImageStatus::TooLarge;
            if (std::uint8_t(bytes[offset + 16]) > 8)
                return TextureImageStatus::Unsupported;
            header = true;
        }
        if (type == "acTL" || type == "fcTL" || type == "fdAT")
            return TextureImageStatus::Unsupported;
        if (type == "IDAT")
            data = true;
        offset += qsizetype(length) + 12;
        if (type == "IEND")
            return data && length == 0 && offset == bytes.size() ? TextureImageStatus::Ready
                                                                 : TextureImageStatus::Invalid;
    }
    return TextureImageStatus::Invalid;
}
} // namespace
TextureImage::TextureImage(int width, int height, std::vector<std::uint8_t> rgba)
    : width_(width), height_(height), rgba_(std::move(rgba)) {
    if (!bounded(width, height) || rgba_.size() != size_t(width) * size_t(height) * 4)
        throw std::runtime_error("Texture pixels must fit 4096 by 4096 and 64 MiB RGBA");
    for (size_t i = 3; i < rgba_.size(); i += 4)
        transparent_ |= rgba_[i] != 255;
}
std::array<double, 4> TextureImage::sampleLinear(TextureCoordinate uv) const {
    if (!std::isfinite(uv.u) || !std::isfinite(uv.v))
        throw std::runtime_error("Texture sample coordinates must be finite");
    const double x = (uv.u - std::floor(uv.u)) * width_ - .5,
                 y = (uv.v - std::floor(uv.v)) * height_ - .5;
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double fx = x - x0, fy = y - y0;
    std::array<double, 4> result{};
    for (int row = 0; row < 2; ++row)
        for (int column = 0; column < 2; ++column) {
            const auto index = size_t(((y0 + row + height_) % height_) * width_ +
                                      (x0 + column + width_) % width_) *
                               4;
            const double weight = (column ? fx : 1 - fx) * (row ? fy : 1 - fy);
            for (int channel = 0; channel < 4; ++channel)
                result[channel] += weight * (channel == 3 ? rgba_[index + channel] / 255.
                                                          : linear(rgba_[index + channel]));
        }
    return result;
}
std::string_view textureImageStatusName(TextureImageStatus status) {
    switch (status) {
    case TextureImageStatus::Ready:
        return "ready";
    case TextureImageStatus::Missing:
        return "missing";
    case TextureImageStatus::Unsupported:
        return "unsupported";
    case TextureImageStatus::Invalid:
        return "invalid";
    case TextureImageStatus::TooLarge:
        return "too_large";
    }
    throw std::runtime_error("Unknown texture image status");
}
TextureImageResult decodeTextureImage(const AssetRecord &asset) {
    auto failure = [](TextureImageStatus status) { return TextureImageResult{status, {}}; };
    if (!asset.payload)
        return failure(TextureImageStatus::Missing);
    QByteArray format;
    if (asset.mediaType == "image/png")
        format = "png";
    else if (asset.mediaType == "image/jpeg")
        format = "jpeg";
    else
        return failure(TextureImageStatus::Unsupported);
    auto bytes = assetByteArray(asset.payload);
    if (format == "png") {
        const auto status = pngEnvelope(bytes);
        if (status != TextureImageStatus::Ready)
            return failure(status);
    } else if (!bytes.startsWith(QByteArray::fromHex("ffd8ff")) ||
               !bytes.endsWith(QByteArray::fromHex("ffd9")))
        return failure(TextureImageStatus::Invalid);
    QBuffer source(&bytes);
    if (!source.open(QIODevice::ReadOnly))
        return failure(TextureImageStatus::Invalid);
    QImageReader reader(&source, format);
    reader.setAutoDetectImageFormat(false);
    reader.setAutoTransform(false); // Stored pixel axes define UV; ignore EXIF orientation.
    const auto size = reader.size();
    if (!size.isValid() || size.isEmpty())
        return failure(TextureImageStatus::Invalid);
    if (!bounded(size.width(), size.height()))
        return failure(TextureImageStatus::TooLarge);
    auto image = reader.read();
    if (image.isNull() || image.size() != size)
        return failure(TextureImageStatus::Invalid);
    if (image.colorSpace().isValid())
        image = image.convertedToColorSpace(QColorSpace::SRgb, QImage::Format_RGBA8888);
    else
        image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
        return failure(TextureImageStatus::Invalid);
    std::vector<std::uint8_t> pixels(size_t(size.width()) * size_t(size.height()) * 4);
    for (int row = 0; row < size.height(); ++row)
        std::memcpy(pixels.data() + size_t(row) * size_t(size.width()) * 4,
                    image.constScanLine(row), size_t(size.width()) * 4);
    return {TextureImageStatus::Ready,
            std::make_shared<const TextureImage>(size.width(), size.height(), std::move(pixels))};
}
QByteArray encodeTexturePng(const TextureImage &image) {
    const QImage view(image.rgba().data(), image.width(), image.height(), image.width() * 4,
                      QImage::Format_RGBA8888);
    QByteArray bytes;
    QBuffer output(&bytes);
    if (!output.open(QIODevice::WriteOnly) || !view.save(&output, "PNG"))
        throw std::runtime_error("Cannot encode normalized texture PNG");
    return bytes;
}
} // namespace sketchy
