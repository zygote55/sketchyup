#include "io/texture_image.hpp"
#include "io/assets.hpp"
#include <QBuffer>
#include <QColorSpace>
#include <QImage>
#include <QImageReader>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

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
    // ADR 0063 bounds the scan at 4,096 chunks. R118: a large file may hold one chunk per KiB, so that
    // an 8192 by 8192 image written with 8 KiB data chunks (about 7,100 chunks in 58 MB) is admitted.
    const qsizetype chunkLimit = std::max<qsizetype>(4096, bytes.size() / 1024);
    qsizetype chunks = 0;
    bool header = false, data = false;
    while (offset + 12 <= bytes.size() && ++chunks <= chunkLimit) {
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
// The format a decodable media type is read as, or empty for any other type.
QByteArray formatFor(const std::string &mediaType) {
    if (mediaType == "image/png")
        return "png";
    if (mediaType == "image/jpeg")
        return "jpeg";
    return {};
}
// The checks decode makes before it reads pixels. Reads the header only (R118).
TextureImageProbe probeEncoded(QByteArray &bytes, const QByteArray &format) {
    if (format == "png") {
        const auto status = pngEnvelope(bytes);
        if (status != TextureImageStatus::Ready)
            return {status, 0, 0};
    } else if (!bytes.startsWith(QByteArray::fromHex("ffd8ff")) ||
               !bytes.endsWith(QByteArray::fromHex("ffd9")))
        return {TextureImageStatus::Invalid, 0, 0};
    QBuffer source(&bytes);
    if (!source.open(QIODevice::ReadOnly))
        return {TextureImageStatus::Invalid, 0, 0};
    QImageReader reader(&source, format);
    reader.setAutoDetectImageFormat(false);
    reader.setAutoTransform(false); // Stored pixel axes define UV; ignore EXIF orientation.
    const auto size = reader.size();
    if (!size.isValid() || size.isEmpty())
        return {TextureImageStatus::Invalid, 0, 0};
    if (!bounded(size.width(), size.height()))
        return {TextureImageStatus::TooLarge, 0, 0};
    return {TextureImageStatus::Ready, size.width(), size.height()};
}
std::uint8_t toByte(double unit) {
    return std::uint8_t(std::clamp(std::lround(unit * 255.), 0L, 255L));
}
} // namespace
TextureImage::TextureImage(int width, int height, std::vector<std::uint8_t> rgba)
    : width_(width), height_(height), rgba_(std::move(rgba)) {
    if (!bounded(width, height) || rgba_.size() != size_t(width) * size_t(height) * 4)
        throw std::runtime_error("Texture pixels must fit " +
                                 std::to_string(TextureImage::dimensionLimit) + " by " +
                                 std::to_string(TextureImage::dimensionLimit) + " and " +
                                 std::to_string(TextureImage::byteLimit / (1024 * 1024)) +
                                 " MiB RGBA");
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
TextureImageProbe probeTextureImage(const AssetRecord &asset) {
    if (!asset.payload)
        return {TextureImageStatus::Missing, 0, 0};
    const auto format = formatFor(asset.mediaType);
    if (format.isEmpty())
        return {TextureImageStatus::Unsupported, 0, 0};
    // A view of the stored bytes. Only the header is read, so the payload is not copied.
    const auto &stored = asset.payload->bytes();
    auto view = QByteArray::fromRawData(reinterpret_cast<const char *>(stored.data()),
                                        qsizetype(stored.size()));
    return probeEncoded(view, format);
}

TextureImageResult decodeTextureImage(const AssetRecord &asset) {
    auto failure = [](TextureImageStatus status) { return TextureImageResult{status, {}}; };
    if (!asset.payload)
        return failure(TextureImageStatus::Missing);
    const auto format = formatFor(asset.mediaType);
    if (format.isEmpty())
        return failure(TextureImageStatus::Unsupported);
    // The stored bytes are read in place. The asset record keeps the payload alive for the decode.
    const auto &stored = asset.payload->bytes();
    auto bytes = QByteArray::fromRawData(reinterpret_cast<const char *>(stored.data()),
                                         qsizetype(stored.size()));
    const auto probe = probeEncoded(bytes, format);
    if (probe.status != TextureImageStatus::Ready)
        return failure(probe.status);
    QBuffer source(&bytes);
    if (!source.open(QIODevice::ReadOnly))
        return failure(TextureImageStatus::Invalid);
    QImageReader reader(&source, format);
    reader.setAutoDetectImageFormat(false);
    reader.setAutoTransform(false); // Stored pixel axes define UV; ignore EXIF orientation.
    const QSize size(probe.width, probe.height);
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

std::shared_ptr<const TextureImage> reduceTextureImage(const TextureImage &image, int width,
                                                       int height) {
    if (width < 1 || height < 1 || width > image.width() || height > image.height())
        throw std::runtime_error("A reduced texture must be at least one pixel and no larger "
                                 "than its source");
    // Output column x averages source columns [columns[x], columns[x + 1]), and rows likewise.
    // Every range holds at least one source pixel, because the output is no larger than the source.
    auto edges = [](int source, int target) {
        std::vector<int> result(size_t(target) + 1);
        for (int i = 0; i <= target; ++i)
            result[size_t(i)] = int(std::int64_t(i) * source / target);
        return result;
    };
    const auto columns = edges(image.width(), width), rows = edges(image.height(), height);
    const auto &source = image.rgba();
    const size_t sourceRow = size_t(image.width()) * 4;
    std::vector<std::uint8_t> pixels(size_t(width) * size_t(height) * 4);
    // Premultiplied colour and alpha sums for one output row: four values per output column.
    std::vector<double> sums(size_t(width) * 4);
    for (int y = 0; y < height; ++y) {
        std::fill(sums.begin(), sums.end(), 0.);
        for (int sy = rows[size_t(y)]; sy < rows[size_t(y) + 1]; ++sy)
            for (int x = 0; x < width; ++x)
                for (int sx = columns[size_t(x)]; sx < columns[size_t(x) + 1]; ++sx) {
                    const auto *p = &source[size_t(sy) * sourceRow + size_t(sx) * 4];
                    const double alpha = p[3] / 255.;
                    for (size_t c = 0; c < 3; ++c)
                        sums[size_t(x) * 4 + c] += p[c] / 255. * alpha;
                    sums[size_t(x) * 4 + 3] += alpha;
                }
        const double rowCount = rows[size_t(y) + 1] - rows[size_t(y)];
        for (int x = 0; x < width; ++x) {
            const double count = rowCount * (columns[size_t(x) + 1] - columns[size_t(x)]);
            const double alpha = sums[size_t(x) * 4 + 3];
            auto *out = &pixels[(size_t(y) * size_t(width) + size_t(x)) * 4];
            for (size_t c = 0; c < 3; ++c)
                out[c] = alpha > 0 ? toByte(sums[size_t(x) * 4 + c] / alpha) : std::uint8_t{0};
            out[3] = toByte(alpha / count);
        }
    }
    return std::make_shared<const TextureImage>(width, height, std::move(pixels));
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
