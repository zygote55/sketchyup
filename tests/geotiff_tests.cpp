// R107.b1 (ADR 0181 §2.8, §7.2, §7.3, §8 V3, V5, V7): the bounded GeoTIFF reader and the elevation
// window import, on the committed synthetic fixtures (tests/fixtures/geotiff, made by its
// generator). The analytic plane is checked at every post (V3: elevation within 1e-6 m, position
// within 1e-9 m); each §7.3 refusal names its feature; a corrupted file is refused, never crashes.
// R107.b1.1 (ADR 0181 §7.2): LZW fixtures read exactly, the noise surface that uses every code width
// and a ClearCode, and each malformed LZW stream refused by name when its block is decoded.
// R107.b1.2 (ADR 0181 §7.2, §7.3): reduced-resolution overviews are skipped, and any other further
// image, a loop, a link outside the file, or more than sixteen further images is refused by name.
#include "core/terrain_records.hpp"
#include "core/model.hpp"
#include "core/terrain.hpp"
#include "geo/geographic.hpp"
#include "io/document_io.hpp"
#include "io/geotiff.hpp"
#include "io/geotiff_terrain.hpp"
#include "io/native_format.hpp"
#include "io/terrain_import.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace sketchy;

namespace {

const std::string fixtures = std::string(SOURCE_DIR) + "/tests/fixtures/geotiff/";

void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const std::string &message) {
    if (!(std::abs(actual - expected) <= tolerance)) {
        std::cerr << message << ": " << actual << " != " << expected << '\n';
        throw std::runtime_error(message);
    }
}

bool contains(const std::string &text, const std::string &part) {
    return text.find(part) != std::string::npos;
}

// The message of an action that must be refused by name.
template <class F> std::string refusal(F action, const std::string &message) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error(message);
}

std::vector<std::uint8_t> bytesOf(const std::string &name) {
    QFile file(QString::fromStdString(fixtures + name));
    check(file.open(QIODevice::ReadOnly), "fixture " + name + " cannot be opened");
    const auto data = file.readAll();
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

GeoTiffMemory memoryOf(const std::string &name) { return GeoTiffMemory(bytesOf(name)); }

// The analytic surfaces of the generator (tests/fixtures/geotiff/make_geotiff_fixtures.py).
double plane(double col, double row) { return 10 + 2 * col + 3 * row; }
double paraboloid(double col, double row) {
    return 50 + (col - 24) * (col - 24) + (row - 20) * (row - 20);
}
// The noise surface (make_geotiff_fixtures.py): a 32-bit hash of the position, as int16.
double noise(double col, double row) {
    std::uint32_t h = std::uint32_t(col) * 0x9E3779B1u + std::uint32_t(row) * 0x85EBCA77u + 0x27D4EB2Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return double(std::int32_t(h & 0xFFFFu) - 32768);
}

// The fixtures' geography: the upper-left post at 0.2 W, 51.6 N; one arc-second posts.
constexpr double tieLongitude = -0.2, tieLatitude = 51.6;

// ---- The reader ----------------------------------------------------------------------------

void testReaderExactAtPosts(const char *name, double (*surface)(double, double), bool float32) {
    const auto memory = memoryOf(name);
    const auto header = readGeoTiffHeader(memory);
    check(header.width == 48 && header.height == 40, std::string(name) + ": size");
    check((header.sampleType == GeoTiffSampleType::Float32) == float32, std::string(name) + ": sample type");
    GeoTiffRaster raster(memory, header);
    for (std::uint32_t row = 0; row < header.height; ++row)
        for (std::uint32_t col = 0; col < header.width; ++col) {
            bool isVoid = false;
            const double value = raster.sample(col, row, isVoid);
            check(!isVoid, std::string(name) + ": a post is void");
            if (value != surface(col, row))
                throw std::runtime_error(std::string(name) + ": post " + std::to_string(col) + "," +
                                         std::to_string(row) + " is " + std::to_string(value));
        }
    // Between posts: the bilinear of the four surrounding posts (exact for a plane, and the
    // definition of the interpolant for the paraboloid).
    bool isVoid = false;
    const double fc = 0.5, fr = 0.25;
    const double expected = (1 - fc) * (1 - fr) * surface(10, 7) + fc * (1 - fr) * surface(11, 7) +
                            (1 - fc) * fr * surface(10, 8) + fc * fr * surface(11, 8);
    near(raster.sample(10.5, 7.25, isVoid), expected, 1e-12, std::string(name) + ": bilinear");
    check(!isVoid, std::string(name) + ": bilinear touched a void");
}

void testReaderFixtures() {
    testReaderExactAtPosts("plane-f32-strip-none-le.tif", plane, true);
    testReaderExactAtPosts("plane-f32-strip-none-be.tif", plane, true);
    testReaderExactAtPosts("plane-f32-tile-deflate-pred3-le.tif", plane, true);
    testReaderExactAtPosts("plane-f32-tile-deflate-pred3-be.tif", plane, true);
    testReaderExactAtPosts("plane-i16-strip-deflate-pred2-le.tif", plane, false);
    testReaderExactAtPosts("plane-i16-tile-none-be.tif", plane, false);
    testReaderExactAtPosts("paraboloid-f32-tile-deflate-pred3-le.tif", paraboloid, true);

    // Pixel-is-point (GLO-30 tiles): the tie is the centre of the first post, not its corner.
    const auto glo = memoryOf("glo30-like-pixel-point-f32.tif");
    const auto gloHeader = readGeoTiffHeader(glo);
    check(!gloHeader.pixelIsArea && gloHeader.geographicKey == 4326 && gloHeader.horizontalDatum == "WGS84",
          "GLO-30-like: pixel-is-point, WGS84");
    check(gloHeader.verticalReference.empty(), "GLO-30-like names no vertical reference");
    double lon = 0, lat = 0;
    gloHeader.fromPixel(0, 0, lon, lat);
    near(lon, tieLongitude, 1e-12, "pixel-is-point: first post is at the tie");
    near(lat, tieLatitude, 1e-12, "pixel-is-point: first post is at the tie (latitude)");
    double column = 0, row = 0;
    gloHeader.toPixel(lon, lat, column, row);
    near(column, 0, 1e-9, "pixel-is-point: round trip column");
    near(row, 0, 1e-9, "pixel-is-point: round trip row");
    GeoTiffRaster gloRaster(glo, gloHeader);
    bool isVoid = false;
    near(gloRaster.sample(3, 4, isVoid), plane(3, 4), 0, "pixel-is-point: sample");

    // NAD83 and the nodata value.
    const auto dep = memoryOf("dep-like-nad83-f32.tif");
    const auto depHeader = readGeoTiffHeader(dep);
    check(depHeader.horizontalDatum == "NAD83" && depHeader.geographicKey == 4269, "3DEP-like: NAD83");
    check(depHeader.noData && *depHeader.noData == -999999, "3DEP-like: GDAL_NODATA");
    check(depHeader.pixelIsArea, "3DEP-like: pixel-is-area");

    // The vertical reference a file names.
    const auto navd = memoryOf("with-vertical-navd88-f32.tif");
    check(readGeoTiffHeader(navd).verticalReference == "NAVD88", "NAVD88 read from the GeoKey");
}

// R107.b1.1 (ADR 0181 §7.2): LZW, stripped and tiled, int16 with predictor 2 and float32 with
// predictor 3, in both byte orders; each is exact at every post, as the Deflate fixtures are.
void testLzwFixtures() {
    const char *names[] = {"plane-i16-strip-lzw-pred2-le.tif", "plane-i16-strip-lzw-pred2-be.tif",
                           "plane-i16-tile-lzw-pred2-le.tif", "plane-i16-tile-lzw-pred2-be.tif",
                           "plane-f32-strip-lzw-pred3-le.tif", "plane-f32-strip-lzw-pred3-be.tif",
                           "plane-f32-tile-lzw-pred3-le.tif", "plane-f32-tile-lzw-pred3-be.tif"};
    for (const char *name : names) {
        check(readGeoTiffHeader(memoryOf(name)).compression == 5, std::string(name) + ": LZW (5)");
        testReaderExactAtPosts(name, plane, std::string(name).find("-f32-") != std::string::npos);
    }
}

// The noise surface fills and clears LZW's table: every code width from 9 to 12 bits is read, and
// a ClearCode arrives in the middle of the block (the generator's count: 5 clears, 9,012 twelve-bit
// codes).
void testLzwNoise() {
    const auto memory = memoryOf("noise-i16-strip-lzw-pred2-le.tif");
    const auto header = readGeoTiffHeader(memory);
    check(header.compression == 5 && header.predictor == 2 && header.sampleType == GeoTiffSampleType::Int16,
          "noise: LZW, predictor 2, int16");
    check(header.width == 96 && header.height == 96, "noise: size");
    GeoTiffRaster raster(memory, header);
    for (std::uint32_t row = 0; row < header.height; ++row)
        for (std::uint32_t col = 0; col < header.width; ++col) {
            bool isVoid = false;
            const double value = raster.sample(col, row, isVoid);
            check(!isVoid, "noise: a post is void");
            if (value != noise(col, row))
                throw std::runtime_error("noise: post " + std::to_string(col) + "," + std::to_string(row) +
                                         " is " + std::to_string(value));
        }
}

void testVoids() {
    const auto memory = memoryOf("void-f32-tile-deflate-pred3-le.tif");
    const auto header = readGeoTiffHeader(memory);
    check(header.noData && *header.noData == -9999, "void fixture: nodata");
    GeoTiffRaster raster(memory, header);
    bool isVoid = false;
    raster.sample(20, 15, isVoid);
    check(isVoid, "the void post is void");
    isVoid = false;
    raster.sample(19.5, 14.5, isVoid); // its four neighbours carry weight: void
    check(isVoid, "a sample leaning on the void is void");
    isVoid = false;
    near(raster.sample(5, 5, isVoid), plane(5, 5), 0, "a sample away from the void");
    check(!isVoid, "a sample away from the void is not void");
}

// Each §7.3 refusal the reader can meet names its feature.
void testRefusals() {
    const auto refuses = [](const char *name, const char *part) {
        const auto memory = memoryOf(name);
        const auto message = refusal([&] { readGeoTiffHeader(memory); },
                                     std::string(name) + " is accepted");
        check(contains(message, part), std::string(name) + " refused without naming '" + part +
                                           "': " + message);
    };
    refuses("refuse-multiband.tif", "samples per pixel");
    refuses("refuse-float64.tif", "64-bit");
    refuses("refuse-jpeg.tif", "JPEG (7)");
    refuses("refuse-two-images.tif", "More than one image");
    refuses("refuse-mask-image.tif", "transparency mask without the reduced-resolution bit");
    refuses("refuse-ifd-loop.tif", "loop");
    refuses("refuse-overviews-17.tif", "more than 16 images follow the first");
    refuses("refuse-reduced-first.tif", "The first image is reduced-resolution");
    refuses("refuse-mask-first.tif", "The first image is a transparency mask");
    refuses("refuse-osgb36.tif", "OSGB36");
    refuses("refuse-etrs89.tif", "ETRS89");
    refuses("refuse-projected.tif", "projected");
    refuses("refuse-transform.tif", "transformation");
    refuses("refuse-vertical-code.tif", "vertical reference");
    refuses("refuse-bigtiff.tif", "BigTIFF");
}

// A malformed LZW stream sits in a block the header does not read: the file opens, and the
// refusal names the stream when its block is decoded (ADR 0181 §7.3, R107.b1.1).
void testLzwRefusals() {
    const auto refusesBlock = [](const char *name, const char *part) {
        const auto memory = memoryOf(name);
        const auto header = readGeoTiffHeader(memory);
        const auto message = refusal([&] {
            GeoTiffRaster raster(memory, header);
            bool isVoid = false;
            raster.sample(0, 0, isVoid);
        }, std::string(name) + " decodes");
        check(contains(message, part), std::string(name) + " refused without naming '" + part + "': " + message);
    };
    refusesBlock("refuse-lzw-bad-code.tif", "beyond the table");
    refusesBlock("refuse-lzw-table-overflow.tif", "fills the LZW table past 4,096 codes");
    refusesBlock("refuse-lzw-truncated.tif", "truncated LZW stream");
    refusesBlock("refuse-lzw-short.tif", "are expected");
    refusesBlock("refuse-lzw-more-output.tif", "more than its 2048 bytes");
    refusesBlock("refuse-lzw-old-style.tif", "old-style LZW");
}

// The first image's link to the next directory, rewritten to `link` (a little-endian classic file).
std::vector<std::uint8_t> withFirstLink(std::vector<std::uint8_t> bytes, std::uint32_t link) {
    const std::size_t ifd = std::size_t(bytes[4]) | std::size_t(bytes[5]) << 8 |
                            std::size_t(bytes[6]) << 16 | std::size_t(bytes[7]) << 24;
    const std::size_t count = std::size_t(bytes[ifd]) | std::size_t(bytes[ifd + 1]) << 8;
    const std::size_t at = ifd + 2 + 12 * count;
    for (std::size_t k = 0; k < 4; ++k)
        bytes[at + k] = std::uint8_t(link >> (8 * k));
    return bytes;
}

// R107.b1.2 (ADR 0181 §7.2, §7.3): a 3DEP tile has reduced-resolution overviews after its full image.
// The full image is read exactly, at its own pixel scale. The overviews are skipped, and their blocks
// are never decoded (the undecodable file is read too). Sixteen overviews are read; seventeen, a loop,
// and a second full-resolution image are refused by name.
void testOverviews() {
    testReaderExactAtPosts("plane-f32-tile-lzw-pred3-overviews-le.tif", plane, true);
    testReaderExactAtPosts("plane-f32-tile-lzw-pred3-overviews-undecodable-le.tif", plane, true);
    testReaderExactAtPosts("plane-f32-strip-none-overviews16-le.tif", plane, true);
    const auto header = readGeoTiffHeader(memoryOf("plane-f32-tile-lzw-pred3-overviews-le.tif"));
    near(header.scaleX, 1.0 / 3600.0, 1e-15, "overviews: the full image's pixel scale, not an overview's");
    check(header.tiled && header.compression == 5, "overviews: the full image is the LZW tiles");
    check(header.offsets.size() == 9, "overviews: the full image has its own 9 tiles");

    // A second full-resolution image names the image and says why it is refused.
    const auto message = refusal([] {
        const auto memory = memoryOf("refuse-two-images.tif");
        readGeoTiffHeader(memory);
    }, "refuse-two-images.tif is accepted");
    check(contains(message, "image 2 (directory at offset") && contains(message, "full-resolution image"),
          "a second image is named by number and reason: " + message);

    // A link from the first image past the end of the file is refused by name.
    GeoTiffMemory outside(withFirstLink(bytesOf("plane-f32-tile-lzw-pred3-overviews-le.tif"), 0xFFFFFFF0u));
    check(contains(refusal([&] { readGeoTiffHeader(outside); }, "accepted"), "outside the file"),
          "a link past the end of the file is refused");
}

// Corrupted and truncated files are refused by name, or read; never a crash or another error. The
// LZW files are corrupted too: their streams are decoded on every sample, so an edit reaches them.
void corruptionOf(const char *name) {
    const auto original = bytesOf(name);
    std::uint32_t seed = 12345;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (std::size_t cut = 0; cut < original.size(); cut += 37) {
        GeoTiffMemory memory(std::vector<std::uint8_t>(original.begin(), original.begin() + cut));
        try {
            const auto header = readGeoTiffHeader(memory);
            GeoTiffRaster raster(memory, header);
            bool isVoid = false;
            raster.sample(1, 1, isVoid);
        } catch (const GeoTiffRefusal &) {
        }
    }
    for (int trial = 0; trial < 400; ++trial) {
        auto bytes = original;
        const int edits = 1 + int(next() % 4);
        for (int e = 0; e < edits; ++e)
            bytes[next() % bytes.size()] = std::uint8_t(next());
        GeoTiffMemory memory(bytes);
        try {
            const auto header = readGeoTiffHeader(memory);
            GeoTiffRaster raster(memory, header);
            for (int k = 0; k < 4; ++k) {
                bool isVoid = false;
                raster.sample(double(next() % header.width), double(next() % header.height), isVoid);
            }
        } catch (const GeoTiffRefusal &) {
        }
    }
}

void testCorruption() {
    corruptionOf("plane-f32-tile-deflate-pred3-le.tif");
    corruptionOf("plane-f32-tile-lzw-pred3-le.tif");
    corruptionOf("noise-i16-strip-lzw-pred2-le.tif");
    corruptionOf("plane-f32-tile-lzw-pred3-overviews-le.tif"); // the overview links are edited too
    const auto original = bytesOf("plane-f32-tile-deflate-pred3-le.tif");
    // A directory offset outside the file and a non-TIFF file are refused by name.
    auto bytes = original;
    bytes[4] = 0xFF;
    bytes[5] = 0xFF;
    bytes[6] = 0xFF;
    bytes[7] = 0x7F;
    GeoTiffMemory outside(bytes);
    check(contains(refusal([&] { readGeoTiffHeader(outside); }, "accepted"), "directory offset"),
          "a directory outside the file is refused");
    GeoTiffMemory notTiff(std::vector<std::uint8_t>(64, 'x'));
    check(contains(refusal([&] { readGeoTiffHeader(notTiff); }, "accepted"), "Not a TIFF"),
          "a non-TIFF file is refused");
}

// ---- The elevation window -------------------------------------------------------------------

// The UTM centre of a fixture post (zone 30N, the fixtures' longitude).
UtmPosition utmOf(double longitude, double latitude) { return geoToUtm({latitude, longitude}, 30); }

GeoTiffTerrainOptions optionsAround(const UtmPosition &centre, double half) {
    GeoTiffTerrainOptions options;
    options.product = DemProduct::Dep3;
    options.zone = 30;
    options.north = true;
    options.easting0 = centre.easting - half;
    options.northing0 = centre.northing - half;
    options.easting1 = centre.easting + half;
    options.northing1 = centre.northing + half;
    options.vertical = "NAVD88";
    return options;
}

// V3: the analytic plane, at every grid post of a window, within 1e-6 m in elevation and 1e-9 m
// in position (the grid is UTM; the file is geographic).
void testAlignment() {
    const auto path = QString::fromStdString(fixtures + "plane-f32-strip-none-le.tif");
    const auto memory = memoryOf("plane-f32-strip-none-le.tif");
    const auto header = readGeoTiffHeader(memory);
    const auto centre = utmOf(tieLongitude + 24.0 / 3600.0, tieLatitude - 20.0 / 3600.0);
    const auto options = optionsAround(centre, 150);
    const auto set = readGeoTiffTerrain(path, options);
    check(set.format == "geotiff" && set.dem && set.points.size() > 50, "a window of posts");
    check(set.dem->spacing == set.dem->nominalSpacing, "the default spacing is the nominal post size");
    const double spacing = set.dem->spacing;
    const auto columns = set.dem->columns;
    double worstZ = 0, worstXy = 0;
    for (const auto &point : set.points) {
        const double easting = options.easting0 + double(point.ref.body) * spacing;
        const double northing = options.northing0 + double(point.ref.value) * spacing;
        worstXy = std::max({worstXy, std::abs(point.position.x - easting), std::abs(point.position.y - northing)});
        const auto at = utmToGeo({options.zone, options.north, easting, northing});
        double column = 0, row = 0;
        header.toPixel(at.longitude, at.latitude, column, row);
        worstZ = std::max(worstZ, std::abs(point.position.z - plane(column, row)));
    }
    check(columns == std::uint64_t(std::floor(300.0 / spacing + 1e-9) + 1), "grid columns");
    check(worstXy <= 1e-9, "position within 1e-9 m at the posts");
    check(worstZ <= 1e-6, "elevation within 1e-6 m at the posts");
    std::cout << "V3 plane: " << set.points.size() << " posts, worst position " << worstXy
              << " m, worst elevation " << worstZ << " m\n";

    // The source record and the labels.
    const auto &dem = *set.dem;
    check(dem.product == "USGS 3DEP" && dem.credit == "Credit: U.S. Geological Survey", "3DEP credit");
    check(dem.datum == "WGS84" && dem.vertical == "NAVD88" && dem.elevation == "bare earth",
          "labels of the record");
    check(dem.sourceName == "plane-f32-strip-none-le.tif", "the file's name, not its path");
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "fixture opens");
    const auto digest = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    check(set.sha256 == digest.toStdString(), "the file's SHA-256");
    check(set.dem->elevation == "bare earth" && set.format == "geotiff", "3DEP is bare earth");
}

void testGloRecord() {
    // A GLO-30-like file: pixel-is-point, WGS84, no vertical key (so the option names it).
    const auto path = QString::fromStdString(fixtures + "glo30-like-pixel-point-f32.tif");
    const auto memory = memoryOf("glo30-like-pixel-point-f32.tif");
    const auto header = readGeoTiffHeader(memory);
    double lon = 0, lat = 0;
    header.fromPixel(20, 15, lon, lat);
    auto options = optionsAround(utmOf(lon, lat), 60);
    options.product = DemProduct::Glo30;
    options.vertical = "EGM2008";
    const auto set = readGeoTiffTerrain(path, options);
    const auto &dem = *set.dem;
    check(dem.product == "Copernicus DEM GLO-30" && dem.elevation == "surface", "GLO-30 is a surface");
    check(contains(dem.credit, "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014"),
          "GLO-30 credit wording (licence Article 6(b), the adapted-data notice)");
    check(dem.disclaimer == "The organisations in charge of the Copernicus programme by law or by delegation do not incur any liability for any use of the Copernicus WorldDEM-30",
          "GLO-30 liability sentence (Article 6(c))");
    check(dem.vertical == "EGM2008" && dem.datum == "WGS84", "GLO-30 labels");
    check(set.problems.count("surface-elevation") == 1, "a GLO-30 notice names the surface");
}

void testDatumNotice() {
    const auto path = QString::fromStdString(fixtures + "dep-like-nad83-f32.tif");
    const auto memory = memoryOf("dep-like-nad83-f32.tif");
    const auto header = readGeoTiffHeader(memory);
    double lon = 0, lat = 0;
    header.fromPixel(25, 25, lon, lat);
    const auto options = optionsAround(utmOf(lon, lat), 40);
    const auto set = readGeoTiffTerrain(path, options);
    check(set.dem->datum == "NAD83", "NAD83 is labelled");
    check(set.problems.count("datum-approximation") == 1, "NAD83 is a named notice");
}

// R107.b1.1: USGS writes the 1/3 arc-second step as 9.25925927753796e-05, about 2e-9 from 1/10800.
// It is 3DEP (a post of about 10 m), not a refused post.
void testUsgsThirdArcSecond() {
    const auto path = QString::fromStdString(fixtures + "dep13-usgs-scale-f32.tif");
    const auto memory = memoryOf("dep13-usgs-scale-f32.tif");
    double lon = 0, lat = 0;
    readGeoTiffHeader(memory).fromPixel(20, 15, lon, lat);
    const auto set = readGeoTiffTerrain(path, optionsAround(utmOf(lon, lat), 40));
    check(set.dem->product == "USGS 3DEP", "the USGS 1/3 arc-second step is 3DEP");
    check(set.dem->nominalSpacing >= 9 && set.dem->nominalSpacing <= 11,
          "a 1/3 arc-second post is about 10 m: " + std::to_string(set.dem->nominalSpacing));
}

void testRefusedWindows() {
    const auto path = QString::fromStdString(fixtures + "plane-f32-strip-none-le.tif");
    const auto centre = utmOf(tieLongitude + 24.0 / 3600.0, tieLatitude - 20.0 / 3600.0);
    const auto refusedBy = [&](const GeoTiffTerrainOptions &options, const char *part) {
        const auto message = refusal([&] { readGeoTiffTerrain(path, options); }, "accepted");
        check(contains(message, part), std::string("refused without naming '") + part + "': " + message);
    };
    // Outside the file.
    auto far = optionsAround(centre, 150);
    far.easting0 += 5000;
    far.easting1 += 5000;
    refusedBy(far, "not covered by the file");
    // Finer than the post.
    auto fine = optionsAround(centre, 150);
    fine.spacing = 10;
    refusedBy(fine, "finer than the file's post");
    // The window beyond the UTM band (84°N).
    auto polar = optionsAround(centre, 150);
    polar.northing0 = 9450000;
    polar.northing1 = 9450300;
    refusedBy(polar, "");
    // Void posts in the window.
    const auto voidPath = QString::fromStdString(fixtures + "void-f32-tile-deflate-pred3-le.tif");
    const auto voidMemory = memoryOf("void-f32-tile-deflate-pred3-le.tif");
    const auto voidHeader = readGeoTiffHeader(voidMemory);
    double lon = 0, lat = 0;
    voidHeader.fromPixel(20, 15, lon, lat);
    const auto onVoid = optionsAround(utmOf(lon, lat), 100);
    const auto message = refusal([&] { readGeoTiffTerrain(voidPath, onVoid); }, "accepted");
    check(contains(message, "void"), "void posts refuse the window: " + message);
    // A vertical reference the file does not name, and no option.
    auto unlabelled = optionsAround(centre, 150);
    unlabelled.vertical.clear();
    unlabelled.product = DemProduct::Glo30; // the GLO-30-like file has the GLO-30 post
    const auto gloPath = QString::fromStdString(fixtures + "glo30-like-pixel-point-f32.tif");
    const auto gloMessage = refusal([&] {
        readGeoTiffTerrain(gloPath, unlabelled);
    }, "accepted");
    check(contains(gloMessage, "--dem-vertical"), "an unlabelled vertical is refused: " + gloMessage);
    // The file names NAVD88, and the option says EGM2008.
    const auto navdPath = QString::fromStdString(fixtures + "with-vertical-navd88-f32.tif");
    auto conflict = optionsAround(centre, 150);
    conflict.vertical = "EGM2008";
    check(contains(refusal([&] { readGeoTiffTerrain(navdPath, conflict); }, "accepted"), "file's vertical"),
          "a conflicting vertical option is refused");
    // A product whose post the file does not have: 3DEP at 1/2400 east-west.
    auto product = optionsAround(centre, 150);
    const auto gloShape = QString::fromStdString(fixtures + "glo30-like-pixel-point-f32.tif");
    check(contains(refusal([&] { readGeoTiffTerrain(gloShape, product); }, "accepted"), "arc-second"),
          "3DEP refuses a GLO-30 post");
    // A vertical offset needs a note.
    auto offsetNoNote = optionsAround(centre, 150);
    offsetNoNote.verticalOffset = 1.5;
    check(contains(refusal([&] { readGeoTiffTerrain(path, offsetNoNote); }, "accepted"), "note"),
          "an offset needs a note");
    // The zone and hemisphere of the window must be a zone (1 to 60).
    auto zone = optionsAround(centre, 150);
    zone.zone = 61;
    check(contains(refusal([&] { readGeoTiffTerrain(path, zone); }, "accepted"), "1 to 60"),
          "a zone outside 1 to 60 is refused");
}

// V5: a vertical reference that differs from the site's is refused unless an offset and a note
// are given; with them, the heights are offset, and the note is kept.
void testOffset() {
    const auto path = QString::fromStdString(fixtures + "plane-f32-strip-none-le.tif");
    const auto centre = utmOf(tieLongitude + 24.0 / 3600.0, tieLatitude - 20.0 / 3600.0);
    auto options = optionsAround(centre, 150);
    options.verticalOffset = 1.5;
    options.verticalNote = "the EGM2008 to NAVD88 difference at this site, measured by survey";
    const auto base = readGeoTiffTerrain(path, optionsAround(centre, 150));
    const auto shifted = readGeoTiffTerrain(path, options);
    check(shifted.points.size() == base.points.size(), "the offset keeps the posts");
    near(shifted.points[0].position.z - base.points[0].position.z, 1.5, 1e-12, "the offset is applied");
    check(shifted.dem->verticalNote == options.verticalNote && shifted.dem->verticalOffset == 1.5,
          "the offset and its note are in the record");
}

// Decimation: a window whose grid exceeds the triangle limit is decimated to a reported spacing.
void testDecimation() {
    const auto path = QString::fromStdString(fixtures + "plane-f32-tile-deflate-pred3-large-le.tif");
    const auto memory = memoryOf("plane-f32-tile-deflate-pred3-large-le.tif");
    const auto header = readGeoTiffHeader(memory);
    double lon = 0, lat = 0;
    header.fromPixel(350, 350, lon, lat);
    const auto options = optionsAround(utmOf(lon, lat), 6000);
    const auto set = readGeoTiffTerrain(path, options);
    const auto &dem = *set.dem;
    check(dem.spacing > dem.nominalSpacing, "the window decimates");
    const auto triangles = 2 * (dem.columns - 1) * (dem.rows - 1);
    check(triangles <= TerrainLimits::triangles, "the decimated grid fits the triangle limit");
    check(set.points.size() == dem.columns * dem.rows, "every post of the decimated grid is read");
    std::cout << "decimation: nominal " << dem.nominalSpacing << " m, used " << dem.spacing << " m, "
              << dem.columns << " by " << dem.rows << " posts, " << triangles << " triangles\n";
}

void testFileNotRead() {
    const auto message = refusal([] {
        GeoTiffTerrainOptions options;
        options.zone = 30;
        options.easting0 = 0;
        options.northing0 = 0;
        options.easting1 = 1;
        options.northing1 = 1;
        options.vertical = "NAVD88";
        readGeoTiffTerrain(QString::fromStdString(fixtures + "no-such-file.tif"), options);
    }, "accepted");
    check(!message.empty(), "a missing file is refused");
}

// R107.b1 (ADR 0181 §3.2, V9): the source record survives a save and a reopen, and the document
// is schema 34 with terrain-dem-v1. A document without a GeoTIFF terrain keeps its schema.
void testRecordRoundTrip() {
    const auto path = QString::fromStdString(fixtures + "plane-f32-strip-none-le.tif");
    const auto centre = utmOf(tieLongitude + 24.0 / 3600.0, tieLatitude - 20.0 / 3600.0);
    const auto set = readGeoTiffTerrain(path, optionsAround(centre, 150));
    TerrainImportProvenance provenance{"plane-f32-strip-none-le.tif", "EPSG:32630"};
    auto result = runTerrainImport(set, TerrainImportOptions{}, std::nullopt, provenance);
    check(result.ok, "the window imports: " + result.message);
    check(result.record.provenance.dem && *result.record.provenance.dem == *set.dem,
          "the record carries the source record");
    Document doc;
    importTerrain(doc, result.mesh, result.record, result.establish, "Terrain");
    check(nativeDocumentVersionFor(doc) == nativeDemTerrainDocumentVersion, "schema 34 for a GeoTIFF terrain");
    const auto reopened = decodeContainer(encodeContainer(doc));
    check(nativeDocumentVersionFor(reopened) == nativeDemTerrainDocumentVersion, "schema 34 on reopen");
    std::size_t found = 0;
    for (const auto &[id, body] : reopened.bodies())
        if (body->terrain && body->terrain->provenance.dem) {
            ++found;
            check(*body->terrain->provenance.dem == *set.dem, "the source record survives a reopen");
        }
    check(found == 1, "one GeoTIFF terrain reopens");

    // A terrain without the record keeps the schema it had (no GeoTIFF terrain is schema 34).
    TerrainImportResult plain = result;
    plain.record.provenance.dem.reset();
    Document other;
    importTerrain(other, plain.mesh, plain.record, plain.establish, "Terrain");
    check(nativeDocumentVersionFor(other) == nativeTerrainDocumentVersion,
          "a terrain without the record is written as before (schema 29)");
}

} // namespace

int main() {
    struct Case {
        const char *name;
        void (*run)();
    };
    const Case cases[]{
        {"reader fixtures", testReaderFixtures},
        {"lzw fixtures (R107.b1.1)", testLzwFixtures},
        {"lzw noise: every code width and a clear", testLzwNoise},
        {"voids", testVoids},
        {"refusals", testRefusals},
        {"lzw refusals (R107.b1.1)", testLzwRefusals},
        {"overviews and further images (R107.b1.2)", testOverviews},
        {"corruption", testCorruption},
        {"alignment (V3)", testAlignment},
        {"glo-30 record", testGloRecord},
        {"datum notice", testDatumNotice},
        {"USGS 1/3 arc-second step (R107.b1.1)", testUsgsThirdArcSecond},
        {"refused windows", testRefusedWindows},
        {"vertical offset (V5)", testOffset},
        {"decimation", testDecimation},
        {"missing file", testFileNotRead},
        {"record round trip (V9)", testRecordRoundTrip},
    };
    int failed = 0;
    for (const auto &item : cases) {
        try {
            item.run();
            std::cout << "ok: " << item.name << '\n';
        } catch (const std::exception &error) {
            ++failed;
            std::cout << "FAILED: " << item.name << ": " << error.what() << '\n';
        }
    }
    return failed == 0 ? 0 : 1;
}
