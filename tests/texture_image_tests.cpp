#include "core/assets.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include "io/texture_image.hpp"
#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QtEndian>
#include <future>
#include <iostream>
#include <limits>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool failed = false;
    try {
        operation();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "Invalid pixel or sample input rejects");
}
void near(double actual, double expected, double epsilon = 1e-12) {
    check(std::abs(actual - expected) <= epsilon, "Independent pixel/sample oracle");
}
QByteArray encoded(const QImage &image, const char *format = "PNG") {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    check(buffer.open(QIODevice::WriteOnly) &&
              image.save(&buffer, format, QByteArrayView(format) == "PNG" ? -1 : 100),
          "Create synthetic fixture");
    return bytes;
}
AssetRecord asset(const QByteArray &bytes, std::string type = "image/png") {
    return {1, "Synthetic texture", std::move(type), assetPayload(bytes)};
}
std::shared_ptr<const TextureImage> ready(const AssetRecord &record) {
    const auto result = decodeTextureImage(record);
    check(result.status == TextureImageStatus::Ready && result.image, "Supported image decodes");
    return result.image;
}
void status(const AssetRecord &record, TextureImageStatus expected) {
    const auto result = decodeTextureImage(record);
    check(result.status == expected && !result.image, "Explicit failure without partial pixels");
}
void put32(QByteArray &bytes, qsizetype offset, quint32 value) {
    qToBigEndian(value, bytes.data() + offset);
}
QByteArray chunk(const QByteArray &type, const QByteArray &data) {
    QByteArray result(4, 0);
    put32(result, 0, data.size());
    result += type + data;
    // Independent IEEE CRC-32 fixture writer, including PNG ancillary chunks.
    quint32 crc = 0xffffffffu;
    for (auto byte : type + data) {
        crc ^= std::uint8_t(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    QByteArray checksum(4, 0);
    put32(checksum, 0, ~crc);
    return result + checksum;
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); // No display server or QGuiApplication.
    try {
        const int allocationLimit = QImageReader::allocationLimit();
        const std::vector<std::uint8_t> pixels{255, 0, 0,   255, 0,   255, 0,   128,
                                               0,   0, 255, 0,   255, 255, 255, 64};
        const TextureImage original(2, 2, pixels);
        const auto png = encodeTexturePng(original);
        const auto record = asset(png);
        const auto decoded = ready(record);
        check(decoded->width() == 2 && decoded->height() == 2 && decoded->rgba() == pixels &&
                  decoded->hasTransparency(),
              "Top-left row order, RGB under zero alpha, and straight alpha are retained");
        check(assetByteArray(record.payload) == png, "Decode never changes source bytes");
        check(ready(asset(encodeTexturePng(*decoded)))->rgba() == pixels,
              "Normalized embedded PNG reopens exactly");
        const auto red = decoded->sampleLinear({.25, .25});
        check(red == std::array<double, 4>{1, 0, 0, 1}, "Exact top-left texel center");
        const auto green = decoded->sampleLinear({.75, .25});
        near(green[0], 0);
        near(green[1], 1);
        near(green[2], 0);
        near(green[3], 128 / 255.);
        const auto blue = decoded->sampleLinear({.25, .75});
        check(blue == std::array<double, 4>{0, 0, 1, 0}, "Bottom row has increasing V");
        for (const auto uv : {TextureCoordinate{.5, .5}, {0, 0}, {1, 1}, {-2, -3}}) {
            const auto average = decoded->sampleLinear(uv);
            for (int channel = 0; channel < 3; ++channel)
                near(average[channel], .5);
            near(average[3], (255. + 128 + 64) / (4 * 255));
        }
        check(decoded->sampleLinear({-1.75, 1000000.25}) == red,
              "Negative and large repeat phases agree");
        const TextureImage grey(2, 1, {0, 0, 0, 255, 128, 128, 128, 255});
        near(grey.sampleLinear({.5, .5})[0], .10793025005694963);
        check(!grey.hasTransparency(), "RGB alpha coverage is opaque");
        const TextureImage single(1, 1, {255, 0, 0, 128});
        for (const auto uv : {TextureCoordinate{0, 0}, {.99, -.01}, {-100, 900}})
            near(single.sampleLinear(uv)[3], 128 / 255.);

        QImage palette(3, 1, QImage::Format_Indexed8);
        palette.setColorTable(
            {qRgba(10, 20, 30, 255), qRgba(40, 50, 60, 127), qRgba(70, 80, 90, 0)});
        for (int x = 0; x < 3; ++x)
            palette.setPixel(x, 0, x);
        check(ready(asset(encoded(palette)))->rgba() ==
                  std::vector<std::uint8_t>({10, 20, 30, 255, 40, 50, 60, 127, 70, 80, 90, 0}),
              "Palette and tRNS decode to straight RGBA");
        QImage monochrome(3, 1, QImage::Format_Mono);
        monochrome.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
        monochrome.fill(0);
        monochrome.setPixel(1, 0, 1);
        const auto mono = ready(asset(encoded(monochrome)));
        check(mono->rgba()[0] == 0 && mono->rgba()[4] == 255 && mono->rgba()[8] == 0 &&
                  !mono->hasTransparency(),
              "One-bit PNG expands without row-padding leakage");

        QImage profile(1, 1, QImage::Format_RGBA8888);
        profile.setPixelColor(0, 0, QColor(128, 128, 128, 64));
        profile.setColorSpace(QColorSpace::SRgbLinear);
        const auto profiled = ready(asset(encoded(profile)));
        for (int channel = 0; channel < 3; ++channel)
            near(profiled->rgba()[channel], 188, 1);
        check(profiled->rgba()[3] == 64, "Profile conversion preserves linear alpha");
        const auto normalized = encodeTexturePng(*profiled);
        check(!normalized.contains("iCCP") && !normalized.contains("eXIf") &&
                  ready(asset(normalized))->rgba() == profiled->rgba(),
              "Export normalization avoids a second metadata color/orientation transform");

        QImage jpegSource(12, 8, QImage::Format_RGB32);
        jpegSource.fill(QColor(96, 160, 224));
        const auto jpeg = encoded(jpegSource, "JPEG");
        const auto jpg = ready(asset(jpeg, "image/jpeg"));
        check(jpg->width() == 12 && jpg->height() == 8 && !jpg->hasTransparency(),
              "JPEG dimensions and opaque alpha");
        for (int channel = 0; channel < 3; ++channel)
            near(jpg->rgba()[channel], 96 + channel * 64, 2);
        // APP1 EXIF little-endian TIFF with orientation=6 (90 degrees clockwise).
        const auto exif = QByteArray::fromHex(
            "ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
        const auto rotated = jpeg.left(2) + exif + jpeg.mid(2);
        QBuffer exifSource;
        exifSource.setData(rotated);
        exifSource.open(QIODevice::ReadOnly);
        QImageReader exifReader(&exifSource, "jpeg");
        check(exifReader.transformation() != QImageIOHandler::TransformationNone,
              "Orientation fixture contains recognized EXIF transform");
        const auto ignoredOrientation = ready(asset(rotated, "image/jpeg"));
        check(ignoredOrientation->width() == 12 && ignoredOrientation->height() == 8 &&
                  ignoredOrientation->rgba() == jpg->rgba(),
              "Stored JPEG pixel axes define UV regardless of EXIF orientation");

        status({1, "Missing", "image/png", {}}, TextureImageStatus::Missing);
        for (const auto type :
             {"image/svg+xml", "image/gif", "image/webp", "application/octet-stream"})
            status(asset(png, type), TextureImageStatus::Unsupported);
        status(asset(png, "image/jpeg"), TextureImageStatus::Invalid);
        status(asset(jpeg), TextureImageStatus::Invalid);
        status(asset(jpeg.chopped(2), "image/jpeg"), TextureImageStatus::Invalid);
        status(asset("not an image"), TextureImageStatus::Invalid);
        status(asset(png.chopped(1)), TextureImageStatus::Invalid);
        status(asset(png + 'x'), TextureImageStatus::Invalid);
        auto damaged = png;
        const auto idat = damaged.indexOf("IDAT");
        check(idat > 0, "Fixture contains compressed pixels");
        damaged[idat + 5] = char(damaged[idat + 5] ^ 0x55);
        status(asset(damaged), TextureImageStatus::Invalid);
        auto overflow = png;
        put32(overflow, 8, 0xffffffffu);
        status(asset(overflow), TextureImageStatus::Invalid);
        auto huge = png;
        put32(huge, 16, 0xffffffffu);
        status(asset(huge), TextureImageStatus::TooLarge);
        auto zero = png;
        put32(zero, 20, 0);
        status(asset(zero), TextureImageStatus::Invalid);
        auto animated = png;
        animated.insert(33, chunk("acTL", QByteArray::fromHex("0000000200000000")));
        status(asset(animated), TextureImageStatus::Unsupported);
        auto manyChunks = png;
        const auto text = chunk("tEXt", QByteArray("Key\0Value", 9));
        manyChunks.insert(33, text.repeated(4096));
        status(asset(manyChunks), TextureImageStatus::Invalid);
        QImage sixteen(2, 2, QImage::Format_RGBA64);
        sixteen.fill(Qt::red);
        status(asset(encoded(sixteen)), TextureImageStatus::Unsupported);
        QImage wide(4097, 1, QImage::Format_RGB32);
        wide.fill(Qt::white);
        status(asset(encoded(wide)), TextureImageStatus::TooLarge);
        status(asset(encoded(wide, "JPEG"), "image/jpeg"), TextureImageStatus::TooLarge);
        {
            QImage maximum(4096, 4096, QImage::Format_RGBA8888);
            maximum.fill(QColor(7, 8, 9, 127));
            const auto compressed = encoded(maximum);
            maximum = {};
            const auto limit = ready(asset(compressed));
            check(limit->rgba().size() == TextureImage::byteLimit && limit->rgba().back() == 127 &&
                      limit->hasTransparency(),
                  "Exact 64 MiB and 4096-square boundary decodes without rescaling");
        }
        rejects([] { TextureImage(0, 1, {}); });
        rejects([] { TextureImage(4097, 1, {}); });
        rejects([] { TextureImage(1, 1, {1, 2, 3}); });
        rejects([&] { decoded->sampleLinear({std::numeric_limits<double>::infinity(), 0}); });
        rejects([&] { decoded->sampleLinear({0, std::numeric_limits<double>::quiet_NaN()}); });
        for (const auto code : {TextureImageStatus::Ready, TextureImageStatus::Missing,
                                TextureImageStatus::Unsupported, TextureImageStatus::Invalid,
                                TextureImageStatus::TooLarge})
            check(!textureImageStatusName(code).empty(), "Stable diagnostic code");

        Document doc;
        const auto id = createAsset(doc, "Relocatable checker", "image/png", record.payload);
        const auto captured = doc.assets().at(id);
        const auto native = encodeContainer(doc);
        const auto relocated = decodeContainer(native);
        check(ready(*relocated.assets().at(id))->rgba() == pixels,
              "Decode uses only relocated packaged bytes");
        replaceAsset(doc, id, {});
        status(*doc.assets().at(id), TextureImageStatus::Missing);
        check(ready(*captured)->rgba() == pixels, "Captured payload survives later replacement");
        doc.undo();
        check(ready(*doc.assets().at(id))->rgba() == pixels, "Undo restores decodable pixels");
        std::vector<std::future<std::shared_ptr<const TextureImage>>> tasks;
        for (int i = 0; i < 8; ++i)
            tasks.push_back(std::async(std::launch::async, [record] { return ready(record); }));
        for (auto &task : tasks)
            check(task.get()->rgba() == pixels, "Concurrent decode is exact");
        check(QImageReader::allocationLimit() == allocationLimit,
              "Per-image validation does not change global decoder allocation policy");
        std::cout << "Texture image decoding, sampling and relocation checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
