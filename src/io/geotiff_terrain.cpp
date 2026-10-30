#include "io/geotiff_terrain.hpp"

#include "core/terrain_records.hpp"
#include "geo/geographic.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sketchy {
namespace {

// The WGS84 ellipsoid (ADR 0181 §2.2): the semi-major axis and the first eccentricity squared.
constexpr double wgs84SemiMajor = 6378137.0, wgs84EccentricitySquared = 0.00669437999014;
constexpr double degreesToRadians = 3.14159265358979323846 / 180.0;

// The copy of the file the reader uses: each read seeks and reads, so the raster is never loaded
// whole (ADR 0181 §9).
class FileBytes final : public GeoTiffBytes {
  public:
    explicit FileBytes(const QString &path) : file_(path) {}
    bool open() { return file_.open(QIODevice::ReadOnly); }
    std::uint64_t size() const override { return std::uint64_t(file_.size()); }
    bool read(std::uint64_t offset, std::size_t count, std::uint8_t *out) const override {
        if (offset > size() || count > size() - offset || !file_.seek(qint64(offset)))
            return false;
        const auto got = file_.read(reinterpret_cast<char *>(out), qint64(count));
        return got == qint64(count);
    }

  private:
    mutable QFile file_;
};

// The size and modification time of the file. The posts, the header and the digest must describe
// the file in this state (R107.b3, UX §20.7 item 7): a change in between refuses the import.
struct FileState {
    qint64 size{};
    QDateTime modified;
    bool operator==(const FileState &other) const {
        return size == other.size && modified == other.modified;
    }
};
FileState fileStateOf(const QString &path) {
    const QFileInfo info(path);
    return {info.size(), info.lastModified()};
}

// The file's SHA-256, read in 1 MiB pieces with a parse checkpoint between them.
std::string fileSha256(const QString &path, TerrainProgress *progress) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw TerrainError("INPUT_ERROR", "The GeoTIFF file cannot be read", "unreadable",
                           "The file cannot be read.");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto total = std::max<qint64>(1, file.size());
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
        terrainCheckpoint(progress, TerrainStage::Parse, 0.5 * double(file.pos()) / double(total));
    }
    return hash.result().toHex().toStdString();
}

// The nominal post size in metres at a latitude, north-south and east-west, from the file's
// steps in degrees (ADR 0181 §2.8).
void postMetres(const GeoTiffHeader &header, double latitude, double &northSouth,
                double &eastWest) {
    const double phi = latitude * degreesToRadians;
    const double s = std::sin(phi);
    const double w = std::sqrt(1 - wgs84EccentricitySquared * s * s);
    const double meridional = wgs84SemiMajor * (1 - wgs84EccentricitySquared) / (w * w * w);
    const double prime = wgs84SemiMajor / w;
    northSouth = header.scaleY * degreesToRadians * meridional;
    eastWest = header.scaleX * degreesToRadians * prime * std::cos(phi);
}

// The post steps the subset accepts (ADR 0181 §7.2): 3DEP at 1/3 and 1 arc-second, and GLO-30 at
// 1 arc-second north-south and 1 or 1.5 arc-seconds east-west (GLO-30 tiles store 1.5).
// The steps are compared to the nominal values within a millionth. USGS writes its 1/3 arc-second
// step as 9.25925927753796e-05, about 2e-9 from 1/10800, so a tighter bound refused real tiles.
bool nearly(double value, double target) { return std::abs(value - target) <= 1e-6 * target; }
bool postAccepted(DemProduct product, const GeoTiffHeader &header) {
    const double third = 1.0 / 10800.0, one = 1.0 / 3600.0, oneAndHalf = 1.0 / 2400.0;
    if (product == DemProduct::Dep3)
        return (nearly(header.scaleY, third) || nearly(header.scaleY, one)) &&
               (nearly(header.scaleX, third) || nearly(header.scaleX, one));
    return nearly(header.scaleY, one) && (nearly(header.scaleX, one) || nearly(header.scaleX, oneAndHalf));
}

std::string labelOf(const std::string &name) {
    if (name == "NAVD88" || name == "navd88")
        return "NAVD88";
    if (name == "EGM2008" || name == "egm2008")
        return "EGM2008";
    if (name == "EGM96" || name == "egm96")
        return "EGM96";
    return {};
}

// The file's name, cut to the 256 bytes of a source record field (never the path).
std::string sourceNameOf(const QString &path) {
    auto name = QFileInfo(path).fileName().toUtf8();
    if (name.size() > int(TerrainDemSource::textBytes))
        name.truncate(int(TerrainDemSource::textBytes));
    return name.toStdString();
}

// A number as the sentences print it: 3 for 3.0, 0.333333 for one third.
std::string plainNumber(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

void requireWindow(const GeoTiffTerrainOptions &options) {
    const auto finite = [](double value) { return std::isfinite(value); };
    if (options.zone < 1 || options.zone > 60)
        throw TerrainError("INVALID_REQUEST", "The UTM zone is 1 to 60 (for example 30N)",
                           "window-zone", "The zone is 1 to 60, with N or S, such as 30N.");
    if (!finite(options.easting0) || !finite(options.northing0) || !finite(options.easting1) ||
        !finite(options.northing1))
        throw TerrainError("INVALID_REQUEST", "The window needs four finite UTM coordinates",
                           "window-coordinates", "The window needs four finite coordinates in metres.");
    if (!(options.easting1 > options.easting0) || !(options.northing1 > options.northing0))
        throw TerrainError("INVALID_REQUEST",
                           "The window is E0,N0,E1,N1 with E1 greater than E0 and N1 greater than N0",
                           "window-order",
                           "The east edge must be east of the west edge, and the north edge north of the south edge.");
}

// Converts a UTM position to geographic, naming the refusal (the band, the zone, an exception).
GeoPosition geographicOf(int zone, bool north, double easting, double northing) {
    try {
        return utmToGeo(UtmPosition{zone, north, easting, northing});
    } catch (const std::exception &error) {
        throw TerrainError("INVALID_REQUEST", std::string("The window is refused: ") + error.what(),
                           "window-place",
                           "This window cannot be placed in zone " + std::to_string(zone) +
                               (north ? "N" : "S") + ": " + error.what());
    }
}

// The header of the file, with a reader refusal as a TerrainError that keeps its code.
GeoTiffHeader readHeader(const GeoTiffBytes &bytes) {
    try {
        return readGeoTiffHeader(bytes);
    } catch (const GeoTiffRefusal &refusal) {
        throw TerrainError("INPUT_ERROR", refusal.what(), geoTiffRefusalName(refusal.code()),
                           refusal.sentence());
    }
}

[[noreturn]] void unreadable() {
    throw TerrainError("INPUT_ERROR", "The GeoTIFF file cannot be read", "unreadable",
                       "The file cannot be read.");
}

} // namespace

std::optional<DemProduct> demProduct(const QString &name) {
    if (name == "3dep")
        return DemProduct::Dep3;
    if (name == "glo30")
        return DemProduct::Glo30;
    return std::nullopt;
}

const char *demProductName(DemProduct product) {
    return product == DemProduct::Dep3 ? "3dep" : "glo30";
}

TerrainSourceSet readGeoTiffTerrain(const QString &path, const GeoTiffTerrainOptions &options,
                                    TerrainProgress *progress) {
    return readGeoTiffTerrainChecked(path, options, progress, {});
}

TerrainSourceSet readGeoTiffTerrainChecked(const QString &path, const GeoTiffTerrainOptions &options,
                                           TerrainProgress *progress,
                                           const std::function<void()> &afterDigest) {
    terrainCheckpoint(progress, TerrainStage::Parse, 0);
    requireWindow(options);
    if (options.verticalOffset && options.verticalNote.empty())
        throw TerrainError("INVALID_REQUEST",
                           "A vertical offset needs a note that says why (ADR 0181 §2.7)",
                           "vertical-note", "An offset needs a note that says why.");
    if (options.verticalOffset && (!std::isfinite(*options.verticalOffset) ||
                                   std::abs(*options.verticalOffset) > 10000))
        throw TerrainError("INVALID_REQUEST",
                           "The vertical offset is a finite number of metres, at most 10,000 m (ADR 0181 §3.2)",
                           "vertical-offset-value",
                           "The offset is a number of metres, at most 10,000 m in size.");
    // R107.b3 (ADR 0181 V5): an offset is a correction, so it is never zero. Zero with a note
    // would mix two references with nothing corrected.
    if (options.verticalOffset && *options.verticalOffset == 0)
        throw TerrainError("INVALID_REQUEST",
                           "A vertical offset of zero corrects nothing; leave it out, or enter the "
                           "difference (ADR 0181 §2.7, V5)",
                           "vertical-offset-zero",
                           "An offset of zero corrects nothing. Leave the offset out, or enter the difference.");
    if (!options.verticalNote.empty() && options.verticalNote.size() > TerrainDemSource::textBytes)
        throw TerrainError("INVALID_REQUEST", "The vertical note is at most 256 bytes",
                           "vertical-note-length", "The note is over 256 bytes. Shorten it.");
    if (!options.vertical.empty() && labelOf(options.vertical).empty())
        throw TerrainError("INVALID_REQUEST", "The vertical reference is NAVD88, EGM2008 or EGM96",
                           "vertical-choice", "Choose NAVD88, EGM2008 or EGM96.");

    // The file, as it is before anything is read (item 7): the header, the digest and the posts
    // must all describe this state, and a change before the posts are read refuses the import.
    const auto state = fileStateOf(path);
    FileBytes bytes(path);
    if (!bytes.open())
        unreadable();
    const GeoTiffHeader header = readHeader(bytes);
    if (!postAccepted(options.product, header))
        throw TerrainError(
            "INPUT_ERROR",
            std::string("The file's post is ") + std::to_string(header.scaleX * 3600) + " by " +
                std::to_string(header.scaleY * 3600) +
                " arc-seconds; the subset is 3DEP at 1/3 or 1 arc-second, and GLO-30 "
                "at 1 arc-second (ADR 0181 §7.2)",
            "post-size",
            "The posts are " + plainNumber(header.scaleX * 3600) + " by " +
                plainNumber(header.scaleY * 3600) +
                " arc-seconds. 3DEP is read at 1/3 or 1 arc-second, and GLO-30 at 1 arc-second.");

    // The vertical reference: the file's, or the option's when the file names none. A file that
    // names one and an option that disagrees is refused.
    std::string vertical = header.verticalReference;
    const std::string chosen = labelOf(options.vertical);
    if (!chosen.empty() && !vertical.empty() && chosen != vertical)
        throw TerrainError("INVALID_REQUEST", "The file's vertical reference is " + vertical +
                                                  "; the option names " + chosen,
                           "vertical-choice-differs",
                           "The file names " + vertical + " and you chose " + chosen + ".");
    if (vertical.empty())
        vertical = chosen;
    if (vertical.empty())
        throw TerrainError(
            "INPUT_ERROR",
            "The file names no vertical reference, so its heights cannot be labelled. Give "
            "--dem-vertical NAVD88 (3DEP over the conterminous US) or EGM2008 (GLO-30). "
            "(ADR 0181 §7.3)",
            "vertical-not-chosen", "Choose the vertical reference to import.");

    // The window: four corners in UTM, each converted (a window outside the UTM band is refused).
    for (const auto &[easting, northing] : {std::pair{options.easting0, options.northing0},
                                            std::pair{options.easting1, options.northing1}})
        geographicOf(options.zone, options.north, easting, northing);
    const auto centre = geographicOf(options.zone, options.north, (options.easting0 + options.easting1) / 2,
                                     (options.northing0 + options.northing1) / 2);

    // The nominal post size in metres, rounded to whole metres, and the spacing the user asks for.
    double northSouth = 0, eastWest = 0;
    postMetres(header, centre.latitude, northSouth, eastWest);
    const double nominal = std::round(std::max(northSouth, eastWest));
    const double requested = options.spacing.value_or(nominal);
    if (!std::isfinite(requested) || requested <= 0)
        throw TerrainError("INVALID_REQUEST", "The spacing is a positive number of metres",
                           "spacing", "The spacing is a positive number of metres.");
    if (requested < nominal)
        throw TerrainError("INVALID_REQUEST",
                           "The spacing is finer than the file's post (" + std::to_string(int(nominal)) +
                               " m); a finer grid would only interpolate, so choose at least " +
                               std::to_string(int(nominal)) + " m",
                           "spacing-fine",
                           "Finer than the file's posts (" + std::to_string(int(nominal)) +
                               " m) would only interpolate. Use " + std::to_string(int(nominal)) +
                               " m or more.");
    const double width = options.easting1 - options.easting0;
    const double height = options.northing1 - options.northing0;
    // The widest spacing with two posts across. A wider spacing leaves one post, so it is refused
    // (before R107.b3 the decimation looped on such a window).
    const double widest = std::min(width, height);
    if (requested > widest)
        throw TerrainError("INVALID_REQUEST", "The window is smaller than two posts across",
                           "window-small", "The window is smaller than two posts across.");

    // The room: the triangles the model's other terrains leave (item 4). Every grid has at least two
    // triangles, so with less room than that no spacing fits.
    const double limit = double(TerrainLimits::triangles);
    const double room = std::max(0.0, limit - double(options.otherTerrainTriangles));
    if (room < 2)
        throw TerrainError("LIMIT_EXCEEDED",
                           "The model has no room for the terrain: its other terrains take " +
                               std::to_string(options.otherTerrainTriangles) + " of " +
                               std::to_string(TerrainLimits::triangles) + " triangles",
                           "triangle-room",
                           "This model has " + terrainCountText(options.otherTerrainTriangles) +
                               " triangles in its terrains, and the limit is " +
                               terrainCountText(TerrainLimits::triangles) + ", so this window cannot fit.");
    // The grid: posts at whole multiples of the spacing from the window's west and south edges.
    // Decimate until the triangles of the grid (2 per cell) fit the room and its vertices fit the
    // terrain limit.
    // A grid fits a triangle cap when its posts are two or more across, its vertices fit the terrain
    // limit, and its triangles (two per cell) do not exceed the cap.
    const auto fitsWithin = [&](double s, double triangleCap) {
        const double nx = std::floor(width / s + 1e-9) + 1, ny = std::floor(height / s + 1e-9) + 1;
        return nx >= 2 && ny >= 2 && nx * ny <= double(TerrainLimits::sourceVertices) &&
               2 * (nx - 1) * (ny - 1) <= triangleCap;
    };
    // The smallest whole-metre spacing from the start that fits, or none within the window's two
    // posts across. The area estimate is a start: it never fits later than the first metre that does.
    const auto smallestFit = [&](double triangleCap) -> std::optional<double> {
        double s = std::max(std::ceil(requested), std::ceil(std::sqrt(2 * width * height / triangleCap)));
        while (s <= widest && !fitsWithin(s, triangleCap))
            s += 1;
        return s <= widest ? std::optional<double>(s) : std::nullopt;
    };
    double spacing = requested;
    bool decimated = false, roomLimited = false;
    if (!fitsWithin(spacing, room)) {
        decimated = true;
        const auto chosen = smallestFit(room);
        if (!chosen)
            throw TerrainError("LIMIT_EXCEEDED",
                               "No spacing for this window fits the model's room: " +
                                   std::to_string(options.otherTerrainTriangles) + " of " +
                                   std::to_string(TerrainLimits::triangles) + " triangles are taken",
                               "triangle-room",
                               "This model has " + terrainCountText(options.otherTerrainTriangles) +
                                   " triangles in its terrains, so no spacing for this window fits "
                                   "the limit of " + terrainCountText(TerrainLimits::triangles) + ".");
        spacing = *chosen;
        // The room set the spacing when the terrain limit alone would have allowed a finer one.
        const auto limitOnly = smallestFit(double(TerrainLimits::triangles));
        roomLimited = limitOnly && spacing > *limitOnly;
    }
    const auto columns = std::uint64_t(std::floor(width / spacing + 1e-9) + 1);
    const auto rows = std::uint64_t(std::floor(height / spacing + 1e-9) + 1);
    if (columns < 2 || rows < 2)
        throw TerrainError("INVALID_REQUEST", "The window is smaller than two posts across",
                           "window-small", "The window is smaller than two posts across.");

    // The digest of the whole file, then the posts. `afterDigest` is the seam the tests use; the
    // size and modification time are checked after the posts are read.
    const std::string digest = fileSha256(path, progress);
    if (afterDigest)
        afterDigest();

    // Each post: its UTM position, the file's bilinear value there, and the offset (V5). A post
    // outside the file, or one that leans on a void, refuses the window: nothing is filled.
    const double offset = options.verticalOffset.value_or(0.0);
    GeoTiffRaster raster(bytes, header);
    TerrainSourceSet set;
    set.format = "geotiff";
    set.unit = *terrainUnit("m");
    TerrainSourceLayer layer;
    layer.name = "posts";
    set.points.reserve(columns * rows);
    std::uint64_t outside = 0, voids = 0;
    std::vector<std::array<double, 2>> voidPositions;
    for (std::uint64_t row = 0; row < rows; ++row) {
        const double northing = options.northing0 + double(row) * spacing;
        for (std::uint64_t column = 0; column < columns; ++column) {
            const double easting = options.easting0 + double(column) * spacing;
            const auto at = geographicOf(options.zone, options.north, easting, northing);
            double x = 0, y = 0;
            header.toPixel(at.longitude, at.latitude, x, y);
            if (!(x >= 0 && x <= header.width - 1.0 && y >= 0 && y <= header.height - 1.0)) {
                ++outside;
                continue;
            }
            bool isVoid = false;
            double value = 0;
            try {
                value = raster.sample(x, y, isVoid);
            } catch (const GeoTiffRefusal &refusal) {
                throw TerrainError("INPUT_ERROR", refusal.what(), geoTiffRefusalName(refusal.code()),
                                   refusal.sentence());
            }
            if (isVoid) {
                ++voids;
                if (voidPositions.size() < geoTiffVoidsListed)
                    voidPositions.push_back({easting, northing});
                continue;
            }
            TerrainSourcePoint point;
            point.ref.kind = TerrainSourceRef::Kind::Post;
            point.ref.value = row;
            point.ref.body = column;
            point.position = {easting, northing, value + offset};
            set.points.push_back(point);
            if ((set.points.size() & 0x3fff) == 0)
                terrainCheckpoint(progress, TerrainStage::Parse,
                                  0.5 + 0.5 * double(set.points.size()) / double(columns * rows));
        }
    }
    if (fileStateOf(path) != state)
        throw TerrainError("INPUT_ERROR",
                           "The GeoTIFF file changed while it was read, so nothing was imported. "
                           "Choose the file again (ADR 0181 §3.2).",
                           "file-changed",
                           "The file changed while it was read, so nothing was imported. Choose the file again.");
    if (outside > 0)
        throw TerrainError("INVALID_REQUEST",
                           "The window is not covered by the file: " + std::to_string(outside) +
                               " of " + std::to_string(columns * rows) +
                               " grid posts lie outside the file's posts. Choose a window inside "
                               "the file (ADR 0181 §2.8).",
                           "window-outside",
                           terrainCountText(outside) + " of " + terrainCountText(columns * rows) +
                               " posts lie outside the file.");
    if (voids > 0)
        throw GeoTiffVoidRefusal(
            "The window is refused: " + std::to_string(voids) +
                " grid posts depend on void (no-data) posts of the file. The "
                "subset does not fill voids; choose a window without them "
                "(ADR 0181 §2.8).",
            terrainCountText(voids) + " posts depend on no-data posts of the file. SketchyUp does not fill them.",
            voids, std::move(voidPositions));
    layer.points = set.points.size();
    set.layers.push_back(layer);
    set.sha256 = QString::fromStdString(digest);

    // The source record (ADR 0181 §3.2): the product's own licence and credit wording.
    TerrainDemSource dem;
    const bool dep3 = options.product == DemProduct::Dep3;
    dem.product = dep3 ? "USGS 3DEP" : "Copernicus DEM GLO-30";
    dem.sourceName = sourceNameOf(path);
    dem.licence = dep3 ? "Free of charge, without use restrictions (USGS 3DEP program page); USGS-produced data are U.S. Public Domain (USGS copyrights and credits page)"
                       : "Licence for the use of the Copernicus WorldDEM-30 (COP-DEM-GLO-30-F)";
    dem.credit = dep3 ? "Credit: U.S. Geological Survey"
                      : "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and "
                        "© Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS "
                        "by the European Union and ESA; all rights reserved";
    dem.disclaimer = dep3 ? std::string()
                          : "The organisations in charge of the Copernicus programme by law or by "
                            "delegation do not incur any liability for any use of the Copernicus "
                            "WorldDEM-30";
    dem.datum = header.horizontalDatum;
    dem.vertical = vertical;
    dem.elevation = dep3 ? "bare earth" : "surface";
    dem.verticalOffset = offset;
    dem.verticalNote = options.verticalNote;
    dem.zone = options.zone;
    dem.north = options.north;
    dem.easting0 = options.easting0;
    dem.northing0 = options.northing0;
    dem.easting1 = options.easting1;
    dem.northing1 = options.northing1;
    dem.spacing = spacing;
    dem.nominalSpacing = nominal;
    dem.columns = columns;
    dem.rows = rows;
    validateTerrainDemSource(dem);
    set.dem = dem;

    // Notices: what the user should see, each named (ADR 0181 §2.3, §2.7, §2.8, §3.1).
    if (header.horizontalDatum == "NAD83")
        set.problems.add(TerrainReport::Severity::Notice, "datum-approximation", 1,
                         "The file is NAD83; it is taken as WGS84 with the label kept (ADR 0181 §2.3).");
    if (decimated && roomLimited)
        set.problems.add(TerrainReport::Severity::Notice, "spacing-decimated", 1,
                         "The model's other terrains leave room for " +
                             terrainCountText(std::uint64_t(room)) + " triangles, so the spacing is " +
                             std::to_string(int(spacing)) + " m, not " +
                             std::to_string(int(requested)) + " m (ADR 0181 §2.9).");
    else if (decimated)
        set.problems.add(TerrainReport::Severity::Notice, "spacing-decimated", 1,
                         "The grid would exceed the terrain limits, so the spacing is " +
                             std::to_string(int(spacing)) + " m, not " +
                             std::to_string(int(requested)) + " m (ADR 0181 §2.9).");
    if (!dep3)
        set.problems.add(TerrainReport::Severity::Notice, "surface-elevation", 1,
                         "GLO-30 heights are a surface model: buildings and trees are included "
                         "(ADR 0181 §10, risk 3).");
    if (options.vertical.size() && header.verticalReference.empty())
        set.problems.add(TerrainReport::Severity::Notice, "vertical-from-option", 1,
                         "The file names no vertical reference; the option's " + vertical +
                             " label is recorded.");
    if (offset != 0)
        set.problems.add(TerrainReport::Severity::Notice, "vertical-offset", 1,
                         "Heights are offset by " + std::to_string(offset) +
                             " m to the site's reference: " + options.verticalNote);

    QJsonObject window{{"zone", options.zone},
                       {"north", options.north},
                       {"easting0", options.easting0},
                       {"northing0", options.northing0},
                       {"easting1", options.easting1},
                       {"northing1", options.northing1}};
    set.report = QJsonObject{
        {"apiVersion", 1},
        {"format", "geotiff"},
        {"metresPerUnit", 1},
        {"sha256", set.sha256},
        {"bytes", double(bytes.size())},
        {"dem", QJsonObject{{"product", QString::fromStdString(dem.product)},
                            {"sourceName", QString::fromStdString(dem.sourceName)},
                            {"licence", QString::fromStdString(dem.licence)},
                            {"credit", QString::fromStdString(dem.credit)},
                            {"disclaimer", QString::fromStdString(dem.disclaimer)},
                            {"datum", QString::fromStdString(dem.datum)},
                            {"vertical", QString::fromStdString(dem.vertical)},
                            {"elevation", QString::fromStdString(dem.elevation)},
                            {"verticalOffset", dem.verticalOffset},
                            {"verticalNote", QString::fromStdString(dem.verticalNote)},
                            {"window", window},
                            {"spacing", spacing},
                            {"nominalSpacing", nominal},
                            {"decimated", decimated},
                            {"grid", QJsonObject{{"columns", double(columns)}, {"rows", double(rows)}}},
                            {"posts", QJsonObject{{"grid", double(columns * rows)},
                                                  {"read", double(set.points.size())},
                                                  {"voids", 0.0},
                                                  {"outside", 0.0}}},
                            {"blocksDecoded", double(raster.blocksDecoded())},
                            {"peakRasterBytes", double(raster.peakHeldBytes())}}}};
    terrainCheckpoint(progress, TerrainStage::Parse, 1);
    return set;
}

GeoTiffFacts geoTiffFacts(const QString &path) {
    FileBytes bytes(path);
    if (!bytes.open())
        unreadable();
    const GeoTiffHeader header = readHeader(bytes);
    GeoTiffFacts facts;
    facts.width = header.width;
    facts.height = header.height;
    facts.postArcSecondsEastWest = header.scaleX * 3600;
    facts.postArcSecondsNorthSouth = header.scaleY * 3600;
    // The posts sit at the pixel centres, so the extent is the centres of the first and last posts.
    const double columnsOf[]{0, header.width - 1.0, 0, header.width - 1.0};
    const double rowsOf[]{0, 0, header.height - 1.0, header.height - 1.0};
    facts.minLongitude = facts.minLatitude = std::numeric_limits<double>::infinity();
    facts.maxLongitude = facts.maxLatitude = -facts.minLongitude;
    for (int corner = 0; corner < 4; ++corner) {
        double longitude = 0, latitude = 0;
        header.fromPixel(columnsOf[corner], rowsOf[corner], longitude, latitude);
        facts.minLongitude = std::min(facts.minLongitude, longitude);
        facts.maxLongitude = std::max(facts.maxLongitude, longitude);
        facts.minLatitude = std::min(facts.minLatitude, latitude);
        facts.maxLatitude = std::max(facts.maxLatitude, latitude);
    }
    double northSouth = 0, eastWest = 0;
    postMetres(header, (facts.minLatitude + facts.maxLatitude) / 2, northSouth, eastWest);
    facts.postMetresNorthSouth = std::round(northSouth);
    facts.postMetresEastWest = std::round(eastWest);
    for (const auto product : {DemProduct::Dep3, DemProduct::Glo30})
        if (postAccepted(product, header))
            facts.products.push_back(product);
    facts.horizontalDatum = header.horizontalDatum;
    facts.geographicKey = header.geographicKey;
    facts.verticalKey = header.verticalKey;
    facts.verticalReference = header.verticalReference;
    facts.compression = header.compression == 1 ? "none" : header.compression == 5 ? "LZW" : "Deflate";
    facts.pixelIsArea = header.pixelIsArea;
    // The zones from the zone of the westernmost post to that of the easternmost (ADR 0181 §2.2).
    const auto zoneOf = [](double longitude) {
        return utmZoneOfLongitude(std::clamp(longitude, -180.0, 180.0));
    };
    for (int zone = zoneOf(facts.minLongitude); zone <= zoneOf(facts.maxLongitude); ++zone)
        facts.zones.push_back(zone);
    return facts;
}

} // namespace sketchy
