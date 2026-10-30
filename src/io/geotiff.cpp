#include "io/geotiff.hpp"

#include "miniz_tinfl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

namespace sketchy {
namespace {

// TIFF field types (TIFF 6.0 section 2) and the GeoTIFF keys the reader reads.
enum : std::uint16_t {
    typeByte = 1,
    typeAscii = 2,
    typeShort = 3,
    typeLong = 4,
    typeDouble = 12,
};
constexpr std::uint16_t tagImageWidth = 256, tagImageLength = 257, tagBitsPerSample = 258,
                        tagCompression = 259, tagStripOffsets = 273, tagSamplesPerPixel = 277,
                        tagRowsPerStrip = 278, tagStripByteCounts = 279, tagPredictor = 317,
                        tagTileWidth = 322, tagTileLength = 323, tagTileOffsets = 324,
                        tagTileByteCounts = 325, tagSubIfds = 330, tagSampleFormat = 339,
                        tagModelPixelScale = 33550, tagModelTiepoint = 33922,
                        tagModelTransformation = 34264, tagGeoKeys = 34735,
                        tagGeoDoubles = 34736, tagGeoAscii = 34737, tagGdalNoData = 42113;
constexpr std::uint16_t keyModelType = 1024, keyRasterType = 1025, keyGeographicType = 2048,
                        keyProjectedType = 3072, keyVerticalType = 4096, keyVerticalUnits = 4099;
constexpr std::uint16_t compressionNone = 1, compressionDeflate = 8, compressionAdobeDeflate = 32946;

// The bytes of one tag: its type, its count and where its values are.
struct Tag {
    std::uint16_t type{};
    std::uint64_t count{};
    std::uint64_t valueSize{};
    std::uint64_t offset{};           // in the file, when valueSize exceeds the 4-byte field
    std::array<std::uint8_t, 4> inlineBytes{};
    bool inl{};
};

std::uint64_t typeSize(std::uint16_t type) {
    switch (type) {
    case typeByte:
    case typeAscii:
        return 1;
    case typeShort:
        return 2;
    case typeLong:
        return 4;
    case typeDouble:
        return 8;
    default:
        return 0;
    }
}

std::string where(std::uint64_t offset) { return " at offset " + std::to_string(offset); }

// Endian-aware reads of a checked range.
class Reader {
  public:
    Reader(const GeoTiffBytes &bytes, bool little) : bytes_(bytes), little_(little) {}
    std::vector<std::uint8_t> bytesAt(std::uint64_t offset, std::uint64_t count) const {
        if (count > bytes_.size() || offset > bytes_.size() || count > bytes_.size() - offset)
            throw GeoTiffRefusal("Truncated GeoTIFF: " + std::to_string(count) +
                                 " bytes are needed" + where(offset));
        std::vector<std::uint8_t> out(count);
        if (count != 0 && !bytes_.read(offset, count, out.data()))
            throw GeoTiffRefusal("Truncated GeoTIFF: the read failed" + where(offset));
        return out;
    }
    std::uint16_t u16(const std::uint8_t *p) const {
        return little_ ? std::uint16_t(p[0] | p[1] << 8) : std::uint16_t(p[0] << 8 | p[1]);
    }
    std::uint32_t u32(const std::uint8_t *p) const {
        if (little_)
            return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
                   std::uint32_t(p[3]) << 24;
        return std::uint32_t(p[3]) | std::uint32_t(p[2]) << 8 | std::uint32_t(p[1]) << 16 |
               std::uint32_t(p[0]) << 24;
    }
    double f64(const std::uint8_t *p) const {
        std::uint64_t bits = 0;
        for (int k = 0; k < 8; ++k) {
            const int byte = little_ ? k : 7 - k;
            bits |= std::uint64_t(p[byte]) << (8 * k);
        }
        double value{};
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

  private:
    const GeoTiffBytes &bytes_;
    bool little_;
};

// The raw bytes of a tag's values, read from the file when they do not fit in the entry.
std::vector<std::uint8_t> tagBytes(const Reader &reader, const Tag &tag) {
    if (tag.inl)
        return std::vector<std::uint8_t>(tag.inlineBytes.begin(),
                                         tag.inlineBytes.begin() + tag.valueSize);
    return reader.bytesAt(tag.offset, tag.valueSize);
}

std::vector<std::uint64_t> tagIntegers(const Reader &reader, const Tag &tag) {
    if (tag.type != typeShort && tag.type != typeLong)
        throw GeoTiffRefusal("Malformed TIFF: a tag that should hold integers has type " +
                             std::to_string(tag.type));
    const auto raw = tagBytes(reader, tag);
    std::vector<std::uint64_t> values;
    values.reserve(tag.count);
    const auto size = typeSize(tag.type);
    for (std::uint64_t i = 0; i < tag.count; ++i) {
        const auto *p = raw.data() + i * size;
        if (tag.type == typeShort)
            values.push_back(reader.u16(p));
        else
            values.push_back(reader.u32(p));
    }
    return values;
}

std::vector<double> tagDoubles(const Reader &reader, const Tag &tag) {
    const auto raw = tagBytes(reader, tag);
    std::vector<double> values;
    values.reserve(tag.count);
    for (std::uint64_t i = 0; i < tag.count; ++i)
        values.push_back(reader.f64(raw.data() + 8 * i));
    return values;
}

// A geographic key's value when it is stored inline (TIFFTagLocation 0), or nullopt.
struct GeoKey {
    std::uint16_t id{};
    std::uint16_t value{};
};

std::string compressionName(std::uint64_t code) {
    switch (code) {
    case 5:
        return "LZW (5)";
    case 6:
        return "old-style JPEG (6)";
    case 7:
        return "JPEG (7)";
    case 32773:
        return "PackBits (32773)";
    case 50000:
        return "ZSTD (50000)";
    case 50001:
        return "WebP (50001)";
    case 34887:
        return "LERC (34887)";
    case 50674:
        return "LERC (50674)";
    default:
        return "code " + std::to_string(code);
    }
}

// The vertical reference a GeoKey names, or empty for an unlabelled key (none, or 0).
std::string verticalLabel(std::uint64_t code) {
    switch (code) {
    case 5703:
        return "NAVD88";
    case 3855:
        return "EGM2008";
    case 5773:
        return "EGM96";
    default:
        return "";
    }
}

// The one image's tags, read from its directory. Refuses a second image.
std::map<std::uint16_t, Tag> readDirectory(const GeoTiffBytes &bytes, const Reader &reader,
                                           std::uint64_t ifd) {
    const auto count = reader.u16(reader.bytesAt(ifd, 2).data());
    if (count == 0 || count > GeoTiffBounds::ifdEntries)
        throw GeoTiffRefusal("GeoTIFF directory has " + std::to_string(count) +
                             " entries (the limit is " +
                             std::to_string(GeoTiffBounds::ifdEntries) + ")" + where(ifd));
    const auto entries = reader.bytesAt(ifd + 2, 12u * count + 4);
    std::map<std::uint16_t, Tag> tags;
    for (std::uint16_t i = 0; i < count; ++i) {
        const auto *p = entries.data() + 12 * i;
        const auto tag = reader.u16(p);
        Tag entry;
        entry.type = reader.u16(p + 2);
        entry.count = reader.u32(p + 4);
        const auto size = typeSize(entry.type);
        if (size == 0)
            continue; // a type this reader does not use; the tag is ignored
        entry.valueSize = size * entry.count;
        if (entry.valueSize <= 4) {
            entry.inl = true;
            std::memcpy(entry.inlineBytes.data(), p + 8, 4);
        } else {
            entry.offset = reader.u32(p + 8);
            if (entry.valueSize > GeoTiffBounds::tagValueBytes)
                throw GeoTiffRefusal("GeoTIFF tag " + std::to_string(tag) + " has " +
                                     std::to_string(entry.valueSize) +
                                     " bytes of values (the limit is 8 MiB)");
        }
        tags.emplace(tag, entry);
    }
    const auto next = reader.u32(entries.data() + 12 * count);
    if (next != 0)
        throw GeoTiffRefusal("More than one image in the file: the reader takes the first image "
                             "only (ADR 0181 §7.3)");
    return tags;
}

} // namespace

bool GeoTiffMemory::read(std::uint64_t offset, std::size_t count, std::uint8_t *out) const {
    if (offset > bytes_.size() || count > bytes_.size() - offset)
        return false;
    std::memcpy(out, bytes_.data() + offset, count);
    return true;
}

void GeoTiffHeader::toPixel(double longitude, double latitude, double &column,
                            double &row) const {
    const double half = pixelIsArea ? 0.5 : 0.0;
    column = (longitude - tieX) / scaleX + tieI - half;
    row = (tieY - latitude) / scaleY + tieJ - half;
}

void GeoTiffHeader::fromPixel(double column, double row, double &longitude,
                              double &latitude) const {
    const double half = pixelIsArea ? 0.5 : 0.0;
    longitude = tieX + (column + half - tieI) * scaleX;
    latitude = tieY - (row + half - tieJ) * scaleY;
}

GeoTiffHeader readGeoTiffHeader(const GeoTiffBytes &bytes) {
    if (bytes.size() < 8)
        throw GeoTiffRefusal("Not a TIFF file: it is shorter than the 8-byte header");
    const auto head = [&bytes] {
        std::vector<std::uint8_t> out(8);
        if (!bytes.read(0, 8, out.data()))
            throw GeoTiffRefusal("Not a TIFF file: the header cannot be read");
        return out;
    }();
    bool little{};
    if (head[0] == 'I' && head[1] == 'I')
        little = true;
    else if (head[0] == 'M' && head[1] == 'M')
        little = false;
    else
        throw GeoTiffRefusal("Not a TIFF file: the byte-order mark is missing");
    const Reader probe(bytes, little);
    const auto magic = probe.u16(head.data() + 2);
    if (magic == 43)
        throw GeoTiffRefusal("BigTIFF (magic 43) is not in the supported subset: the 3DEP and GLO-30 "
                             "tiles are classic TIFF (ADR 0181 §7.2)");
    if (magic != 42)
        throw GeoTiffRefusal("Not a TIFF file: the magic number is " + std::to_string(magic));
    const Reader reader(bytes, little);
    const auto ifd = reader.u32(head.data() + 4);
    if (ifd < 8 || ifd >= bytes.size())
        throw GeoTiffRefusal("Malformed TIFF: the directory offset " + std::to_string(ifd) +
                             " is outside the file");
    const auto tags = readDirectory(bytes, reader, ifd);
    const auto scalar = [&](std::uint16_t tag, std::uint64_t fallback) -> std::uint64_t {
        const auto found = tags.find(tag);
        if (found == tags.end())
            return fallback;
        const auto values = tagIntegers(reader, found->second);
        if (values.size() != 1)
            throw GeoTiffRefusal("Malformed TIFF: tag " + std::to_string(tag) +
                                 " does not hold one value");
        return values.front();
    };
    const auto require = [&](std::uint16_t tag) -> const Tag & {
        const auto found = tags.find(tag);
        if (found == tags.end())
            throw GeoTiffRefusal("Malformed TIFF: tag " + std::to_string(tag) + " is missing");
        return found->second;
    };

    GeoTiffHeader header;
    header.littleEndian = little;
    const auto width = scalar(tagImageWidth, 0), height = scalar(tagImageLength, 0);
    if (width < 2 || height < 2 || width > GeoTiffBounds::maxDimension ||
        height > GeoTiffBounds::maxDimension)
        throw GeoTiffRefusal("The raster is " + std::to_string(width) + " by " +
                             std::to_string(height) +
                             " posts; each side must be 2 to 1,000,000 posts");
    header.width = std::uint32_t(width);
    header.height = std::uint32_t(height);

    const auto samples = scalar(tagSamplesPerPixel, 1);
    if (samples != 1)
        throw GeoTiffRefusal("The file has " + std::to_string(samples) +
                             " samples per pixel: multi-band rasters are refused (ADR 0181 §7.3)");
    const auto bits = scalar(tagBitsPerSample, 1);
    const auto format = scalar(tagSampleFormat, 1);
    if (bits == 16 && format == 2)
        header.sampleType = GeoTiffSampleType::Int16;
    else if (bits == 32 && format == 3)
        header.sampleType = GeoTiffSampleType::Float32;
    else if (bits == 64)
        throw GeoTiffRefusal("64-bit samples are refused (ADR 0181 §7.3); the subset is int16 "
                             "and float32");
    else
        throw GeoTiffRefusal("Samples of " + std::to_string(bits) + " bits in sample format " +
                             std::to_string(format) +
                             " are not in the subset (int16 signed 16-bit or float32)");
    const auto bytesPerSample = header.sampleType == GeoTiffSampleType::Int16 ? 2u : 4u;

    const auto compression = scalar(tagCompression, compressionNone);
    if (compression != compressionNone && compression != compressionDeflate &&
        compression != compressionAdobeDeflate)
        throw GeoTiffRefusal("Compression " + compressionName(compression) +
                             " is not in the supported subset (none or Deflate, ADR 0181 §7.3)");
    header.compression = compression == compressionNone ? compressionNone : compressionDeflate;

    const auto predictor = scalar(tagPredictor, 1);
    if (predictor == 2 && header.sampleType != GeoTiffSampleType::Int16)
        throw GeoTiffRefusal("Predictor 2 is for int16 samples; this file is float32");
    if (predictor == 3 && header.sampleType != GeoTiffSampleType::Float32)
        throw GeoTiffRefusal("Predictor 3 is for float32 samples; this file is int16");
    if (predictor != 1 && predictor != 2 && predictor != 3)
        throw GeoTiffRefusal("Predictor " + std::to_string(predictor) +
                             " is not in the supported subset (1, 2 or 3)");
    header.predictor = std::uint16_t(predictor);

    // Blocks: tiles, or strips of whole rows. A block holds at most 4 Mi samples.
    const bool tiled = tags.count(tagTileWidth) != 0;
    header.tiled = tiled;
    std::uint64_t blockWidth = 0, blockHeight = 0;
    if (tiled) {
        blockWidth = scalar(tagTileWidth, 0);
        blockHeight = scalar(tagTileLength, 0);
        if (blockWidth == 0 || blockHeight == 0 || blockWidth > GeoTiffBounds::maxDimension ||
            blockHeight > GeoTiffBounds::maxDimension ||
            blockWidth * blockHeight > GeoTiffBounds::blockSamples)
            throw GeoTiffRefusal("Tile size " + std::to_string(blockWidth) + " by " +
                                 std::to_string(blockHeight) +
                                 " is over the 4,194,304-sample block limit");
        header.blocksAcross = std::uint32_t((width + blockWidth - 1) / blockWidth);
        header.blocksDown = std::uint32_t((height + blockHeight - 1) / blockHeight);
        header.offsets = tagIntegers(reader, require(tagTileOffsets));
        header.byteCounts = tagIntegers(reader, require(tagTileByteCounts));
    } else {
        blockWidth = width;
        blockHeight = std::min<std::uint64_t>(scalar(tagRowsPerStrip, height), height);
        if (blockHeight == 0 || blockWidth * blockHeight > GeoTiffBounds::blockSamples)
            throw GeoTiffRefusal("A strip of " + std::to_string(blockWidth * blockHeight) +
                                 " samples is over the 4,194,304-sample block limit; the file "
                                 "needs tiles or shorter strips");
        header.blocksAcross = 1;
        header.blocksDown = std::uint32_t((height + blockHeight - 1) / blockHeight);
        header.offsets = tagIntegers(reader, require(tagStripOffsets));
        header.byteCounts = tagIntegers(reader, require(tagStripByteCounts));
    }
    header.blockWidth = std::uint32_t(blockWidth);
    header.blockHeight = std::uint32_t(blockHeight);
    const std::uint64_t blocks = std::uint64_t(header.blocksAcross) * header.blocksDown;
    if (blocks > GeoTiffBounds::blockCount || header.offsets.size() != blocks ||
        header.byteCounts.size() != blocks)
        throw GeoTiffRefusal("The file has " + std::to_string(header.offsets.size()) +
                             " blocks; the reader needs " + std::to_string(blocks) +
                             " and takes at most 1,000,000");
    for (std::uint64_t b = 0; b < blocks; ++b) {
        const auto row = b / header.blocksAcross;
        const auto rows = tiled ? blockHeight
                                : std::min<std::uint64_t>(blockHeight, height - row * blockHeight);
        const auto expected = blockWidth * rows * bytesPerSample;
        const auto stored = header.byteCounts[b];
        if (header.offsets[b] > bytes.size() || stored > bytes.size())
            throw GeoTiffRefusal("Block " + std::to_string(b) + " lies outside the file");
        if (header.compression == compressionNone ? stored != expected
                                                  : stored > expected + expected / 16 + 1024)
            throw GeoTiffRefusal("Block " + std::to_string(b) + " stores " +
                                 std::to_string(stored) + " bytes; " + std::to_string(expected) +
                                 " are expected");
        if (header.offsets[b] + stored > bytes.size())
            throw GeoTiffRefusal("Block " + std::to_string(b) + " runs past the end of the file");
    }

    // GeoKeys: the model, the raster type, the horizontal and vertical references.
    const auto keyDirectory = tags.find(tagGeoKeys);
    if (keyDirectory == tags.end())
        throw GeoTiffRefusal("The file has no GeoKey directory: it is not georeferenced");
    const auto directory = tagIntegers(reader, keyDirectory->second);
    if (directory.size() < 4 || directory[3] > GeoTiffBounds::geoKeys ||
        directory.size() < 4 + 4 * directory[3])
        throw GeoTiffRefusal("Malformed GeoKey directory");
    std::map<std::uint16_t, GeoKey> keys;
    for (std::uint64_t k = 0; k < directory[3]; ++k) {
        const auto at = 4 + 4 * k;
        const auto id = std::uint16_t(directory[at]);
        const auto location = directory[at + 1];
        if (location != 0)
            continue; // a double or ASCII key: none of the keys the reader uses
        keys[id] = GeoKey{id, std::uint16_t(directory[at + 3])};
    }
    const auto keyValue = [&](std::uint16_t id) -> std::optional<std::uint16_t> {
        const auto found = keys.find(id);
        if (found == keys.end())
            return std::nullopt;
        return found->second.value;
    };
    if (const auto model = keyValue(keyModelType); model && *model != 2) {
        if (*model == 1)
            throw GeoTiffRefusal("The raster is projected: the subset is geographic, EPSG:4326 "
                                 "or 4269 (ADR 0181 §7.3)");
        throw GeoTiffRefusal("The model type " + std::to_string(*model) +
                             " is not in the subset (geographic is required)");
    }
    if (const auto raster = keyValue(keyRasterType)) {
        if (*raster != 1 && *raster != 2)
            throw GeoTiffRefusal("The raster type " + std::to_string(*raster) +
                                 " is not pixel-is-area (1) or pixel-is-point (2)");
        header.pixelIsArea = *raster == 1;
    }
    if (keyValue(keyProjectedType) && *keyValue(keyProjectedType) != 32767)
        throw GeoTiffRefusal("The file names a projected CRS (EPSG:" +
                             std::to_string(*keyValue(keyProjectedType)) +
                             "): the subset is geographic (ADR 0181 §7.3)");
    const auto geographic = keyValue(keyGeographicType);
    if (!geographic)
        throw GeoTiffRefusal("The file names no geographic CRS, so its horizontal datum is "
                             "unlabelled");
    switch (*geographic) {
    case 4326:
        header.geographicKey = 4326;
        header.horizontalDatum = "WGS84";
        break;
    case 4269:
        header.geographicKey = 4269;
        header.horizontalDatum = "NAD83";
        break;
    case 4258:
        throw GeoTiffRefusal("ETRS89 (EPSG:4258) is refused: the product has no transformation to "
                             "WGS84 (ADR 0181 §2.3)");
    case 4277:
        throw GeoTiffRefusal("OSGB36 (EPSG:4277) is refused: the product has no national grid "
                             "transformation (ADR 0181 §2.3)");
    case 32767:
        throw GeoTiffRefusal("A user-defined geographic CRS is not in the supported subset");
    default:
        throw GeoTiffRefusal("Geographic CRS EPSG:" + std::to_string(*geographic) +
                             " is not in the supported subset (4326 WGS84 or 4269 NAD83)");
    }
    if (const auto units = keyValue(keyVerticalUnits); units && *units != 9001)
        throw GeoTiffRefusal("Elevations in units code " + std::to_string(*units) +
                             " are refused: only metres (9001) are in the subset");
    if (const auto vertical = keyValue(keyVerticalType)) {
        if (*vertical == 32767)
            throw GeoTiffRefusal("A user-defined vertical reference is not in the supported subset");
        if (*vertical != 0) {
            header.verticalReference = verticalLabel(*vertical);
            if (header.verticalReference.empty())
                throw GeoTiffRefusal("The vertical reference EPSG:" + std::to_string(*vertical) +
                                     " is not in the subset (NAVD88 5703, EGM2008 3855, EGM96 "
                                     "5773; ADR 0181 §2.7)");
        }
    }

    // Georeferencing: a pixel scale and one tie point, with no rotation.
    if (tags.count(tagModelTransformation))
        throw GeoTiffRefusal("A model transformation matrix (a rotated or sheared raster) is not in "
                             "the supported subset");
    const auto scale = tagDoubles(reader, require(tagModelPixelScale));
    const auto tie = tagDoubles(reader, require(tagModelTiepoint));
    if (scale.size() < 2 || tie.size() != 6)
        throw GeoTiffRefusal("The raster must have one pixel scale and one tie point");
    header.scaleX = scale[0];
    header.scaleY = scale[1];
    header.tieI = tie[0];
    header.tieJ = tie[1];
    header.tieX = tie[3];
    header.tieY = tie[4];
    const bool sane = std::isfinite(header.scaleX) && std::isfinite(header.scaleY) &&
                      header.scaleX > 0 && header.scaleY > 0 && std::isfinite(header.tieI) &&
                      std::isfinite(header.tieJ) && std::isfinite(header.tieX) &&
                      std::isfinite(header.tieY) && std::abs(header.tieX) <= 360 &&
                      std::abs(header.tieY) <= 90;
    if (!sane)
        throw GeoTiffRefusal("The raster's pixel scale or tie point is not a finite geographic "
                             "position");

    // GDAL_NODATA: an ASCII number, the void value of the file.
    if (const auto found = tags.find(tagGdalNoData); found != tags.end()) {
        const auto raw = tagBytes(reader, found->second);
        const std::string text(raw.begin(), std::find(raw.begin(), raw.end(), 0));
        char *end = nullptr;
        const double value = std::strtod(text.c_str(), &end);
        if (end == text.c_str())
            throw GeoTiffRefusal("GDAL_NODATA is not a number");
        if (std::isfinite(value))
            header.noData = value;
    }

    return header;
}

GeoTiffRaster::GeoTiffRaster(const GeoTiffBytes &bytes, GeoTiffHeader header)
    : bytes_(bytes), header_(std::move(header)) {}

const std::vector<float> &GeoTiffRaster::block(std::size_t index) {
    if (const auto found = lookup_.find(index); found != lookup_.end()) {
        cache_.splice(cache_.begin(), cache_, found->second);
        return cache_.front().samples;
    }
    // The block and the bytes it is decoded from are held together, so drop older blocks first.
    const auto bps = header_.sampleType == GeoTiffSampleType::Int16 ? 2u : 4u;
    const auto row = index / header_.blocksAcross;
    const auto rows = header_.tiled
                          ? std::size_t(header_.blockHeight)
                          : std::min<std::size_t>(header_.blockHeight,
                                                  header_.height - row * header_.blockHeight);
    const std::size_t samples = std::size_t(header_.blockWidth) * rows;
    const std::size_t stored = std::size_t(header_.byteCounts[index]);
    const std::size_t need = samples * sizeof(float) + samples * bps + stored;
    while (!cache_.empty() && held_ + need > GeoTiffBounds::rasterBytes) {
        held_ -= cache_.back().samples.size() * sizeof(float);
        lookup_.erase(cache_.back().index);
        cache_.pop_back();
    }
    peakHeld_ = std::max(peakHeld_, held_ + need);
    Block entry;
    entry.index = index;
    entry.samples = decode(index);
    held_ += entry.samples.size() * sizeof(float);
    peakHeld_ = std::max(peakHeld_, held_);
    ++decoded_;
    cache_.push_front(std::move(entry));
    lookup_[index] = cache_.begin();
    return cache_.front().samples;
}

std::vector<float> GeoTiffRaster::decode(std::size_t index) const {
    const auto &h = header_;
    const auto row = index / h.blocksAcross;
    const std::size_t rows = h.tiled ? h.blockHeight
                                     : std::min<std::size_t>(h.blockHeight,
                                                             h.height - row * h.blockHeight);
    const std::size_t width = h.blockWidth;
    const std::size_t samples = width * rows;
    const bool float32 = h.sampleType == GeoTiffSampleType::Float32;
    const std::size_t bps = float32 ? 4 : 2;
    const std::size_t expected = samples * bps;
    const std::size_t stored = std::size_t(h.byteCounts[index]);
    std::vector<std::uint8_t> raw(stored);
    if (stored != 0 && !bytes_.read(h.offsets[index], stored, raw.data()))
        throw GeoTiffRefusal("Block " + std::to_string(index) + " cannot be read");
    std::vector<std::uint8_t> out;
    if (h.compression == compressionDeflate) {
        out.resize(expected);
        const auto got = tinfl_decompress_mem_to_mem(out.data(), expected, raw.data(), stored,
                                                     TINFL_FLAG_PARSE_ZLIB_HEADER |
                                                         TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (got != expected)
            throw GeoTiffRefusal("Block " + std::to_string(index) +
                                 " is not a valid Deflate stream of " + std::to_string(expected) +
                                 " bytes");
    } else {
        out = std::move(raw);
    }
    const Reader reader(bytes_, h.littleEndian);
    std::vector<float> values(samples);
    if (float32 && h.predictor == 3) {
        // Floating-point predictor (ADR 0181 §7.2; GLO-30 uses it): the bytes of each row are
        // differenced across the whole row, then byte plane k holds byte k (most significant
        // first) of every sample in the row.
        const std::size_t rowBytes = width * 4;
        for (std::size_t r = 0; r < rows; ++r) {
            std::uint8_t *line = out.data() + r * rowBytes;
            for (std::size_t k = 1; k < rowBytes; ++k)
                line[k] = std::uint8_t(line[k] + line[k - 1]);
            for (std::size_t j = 0; j < width; ++j) {
                const std::uint8_t b[4] = {line[j], line[width + j], line[2 * width + j],
                                           line[3 * width + j]};
                const std::uint32_t bits = std::uint32_t(b[0]) << 24 | std::uint32_t(b[1]) << 16 |
                                           std::uint32_t(b[2]) << 8 | std::uint32_t(b[3]);
                std::memcpy(&values[r * width + j], &bits, sizeof(float));
            }
        }
        return values;
    }
    if (!float32) {
        std::vector<std::uint16_t> ints(samples);
        for (std::size_t s = 0; s < samples; ++s)
            ints[s] = reader.u16(out.data() + 2 * s);
        if (h.predictor == 2)
            for (std::size_t r = 0; r < rows; ++r)
                for (std::size_t j = 1; j < width; ++j)
                    ints[r * width + j] = std::uint16_t(ints[r * width + j] + ints[r * width + j - 1]);
        for (std::size_t s = 0; s < samples; ++s)
            values[s] = float(std::int16_t(ints[s]));
        return values;
    }
    for (std::size_t s = 0; s < samples; ++s) {
        const auto bits = reader.u32(out.data() + 4 * s);
        std::memcpy(&values[s], &bits, sizeof(float));
    }
    return values;
}

double GeoTiffRaster::sample(double column, double row, bool &isVoid) {
    const auto &h = header_;
    const double maxColumn = h.width - 1.0, maxRow = h.height - 1.0;
    if (!(column >= 0 && column <= maxColumn && row >= 0 && row <= maxRow))
        throw GeoTiffRefusal("The sample lies outside the file's posts");
    std::size_t c0 = std::size_t(std::floor(column)), r0 = std::size_t(std::floor(row));
    if (c0 >= h.width - 1)
        c0 = h.width - 2;
    if (r0 >= h.height - 1)
        r0 = h.height - 2;
    const double fc = column - double(c0), fr = row - double(r0);
    const double weights[4] = {(1 - fc) * (1 - fr), fc * (1 - fr), (1 - fc) * fr, fc * fr};
    const std::size_t cols[4] = {c0, c0 + 1, c0, c0 + 1};
    const std::size_t rows[4] = {r0, r0, r0 + 1, r0 + 1};
    double total = 0;
    for (int k = 0; k < 4; ++k) {
        if (weights[k] <= 0)
            continue;
        const std::size_t c = cols[k], r = rows[k];
        std::size_t index, offset;
        if (h.tiled) {
            const std::size_t bx = c / h.blockWidth, by = r / h.blockHeight;
            index = by * h.blocksAcross + bx;
            offset = (r - by * h.blockHeight) * h.blockWidth + (c - bx * h.blockWidth);
        } else {
            const std::size_t by = r / h.blockHeight;
            index = by;
            offset = (r - by * h.blockHeight) * h.blockWidth + c;
        }
        const float value = block(index)[offset];
        if (std::isnan(value) || (h.noData && double(value) == *h.noData)) {
            isVoid = true;
            continue;
        }
        total += weights[k] * double(value);
    }
    return isVoid ? 0.0 : total;
}

} // namespace sketchy
