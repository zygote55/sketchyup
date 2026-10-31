#include "io/orthophoto_import.hpp"

#include "geo/geographic.hpp"
#include "io/geotiff.hpp"
#include "io/image_refusal.hpp"
#include "io/texture_image.hpp"
#include "io/world_file.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace sketchy {
namespace {

// The file's bytes as the GeoTIFF reader reads them, without a copy of the file.
class FileBytes final : public GeoTiffBytes {
  public:
    explicit FileBytes(const QByteArray &bytes) : bytes_(bytes) {}
    std::uint64_t size() const override { return std::uint64_t(bytes_.size()); }
    bool read(std::uint64_t offset, std::size_t count, std::uint8_t *out) const override {
        if (offset > size() || count > size() - offset)
            return false;
        std::memcpy(out, bytes_.constData() + offset, count);
        return true;
    }

  private:
    const QByteArray &bytes_;
};

// The stored bytes of one asset (AssetPayload::limit) and the placed size (ReferenceImage's limit). A PNG
// or JPEG is stored as its own bytes, so its file is limited to the asset limit. A GeoTIFF is decoded and
// stored as a normalized PNG, so only that PNG is limited; the file is read whole, up to the bound below
// (a 4096 by 4096 RGBA raster is 64 MiB and the decoded image is at most 64 MiB).
constexpr qint64 storedLimit = 16 * 1024 * 1024;
constexpr qint64 geoTiffSourceLimit = 128 * 1024 * 1024;
constexpr double placedLimit = 1e6;

enum class Kind { Png, Jpeg, GeoTiff };

// The format, from the file's first bytes, never from its name (R107.b2 design, "Refusals": image-type).
Kind kindOf(const QByteArray &bytes) {
    const auto starts = [&bytes](std::string_view magic) {
        return bytes.size() >= qsizetype(magic.size()) &&
               std::equal(magic.begin(), magic.end(), bytes.begin());
    };
    if (starts("\x89PNG\r\n\x1a\n"))
        return Kind::Png;
    if (starts("\xff\xd8\xff"))
        return Kind::Jpeg;
    if (starts(std::string_view("II*\0", 4)) || starts(std::string_view("MM\0*", 4)))
        return Kind::GeoTiff;
    throw ImageRefusal("image-type");
}

std::string megabytes(qint64 bytes) {
    return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 1).toStdString() + " MiB";
}

// The file's bytes, read once and checked: unchanged while it was read, and within the limit for its kind
// (the kind comes from the bytes, so the limit is applied after the first read).
QByteArray readImage(const QString &path) {
    const QFileInfo before(path);
    if (!before.isFile())
        throw std::runtime_error("The image is not a regular file");
    if (before.size() > geoTiffSourceLimit)
        throw std::runtime_error("The image is over 128 MiB, which this version does not read");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("The image cannot be read");
    const auto bytes = file.read(geoTiffSourceLimit + 1);
    const QFileInfo after(path);
    if (file.error() != QFileDevice::NoError)
        throw std::runtime_error("The image cannot be read");
    if (bytes.size() > geoTiffSourceLimit)
        throw std::runtime_error("The image is over 128 MiB, which this version does not read");
    if (after.size() != before.size() || after.lastModified() != before.lastModified() ||
        bytes.size() != before.size())
        throw ImageRefusal("file-changed");
    return bytes;
}

// A PNG or JPEG is stored as its own bytes, so its file must fit the asset limit (R107.b2 design, "Limits").
void checkStoredSize(Kind kind, const QByteArray &bytes) {
    if (kind != Kind::GeoTiff && bytes.size() > storedLimit)
        throw ImageRefusal("stored-size", {megabytes(bytes.size())});
}

// The world file that holds an image's placement (R107.b2 design, "Inputs"): the first match, in
// the order of worldFileSuffixes, and the others that were found but are ignored.
struct WorldMatch {
    QString path;
    std::vector<QString> ignored;
};

WorldMatch findWorldFile(const QFileInfo &image) {
    const auto suffixes = worldFileSuffixes(image.fileName().toStdString());
    const QDir folder = image.absoluteDir();
    const auto entries = folder.entryInfoList(QDir::Files);
    WorldMatch match;
    for (const auto &suffix : suffixes) {
        const auto wanted = image.completeBaseName() + "." + QString::fromStdString(suffix);
        for (const auto &entry : entries) {
            if (entry.fileName().compare(wanted, Qt::CaseInsensitive) != 0)
                continue;
            if (match.path.isEmpty())
                match.path = entry.absoluteFilePath();
            else
                match.ignored.push_back(entry.fileName());
        }
    }
    return match;
}

// The zone label of a grid, for the sentences: "30N".
std::string zoneLabel(int zone, bool north) {
    return std::to_string(zone) + (north ? "N" : "S");
}

// The geographic position of a grid corner. A refusal of the UTM range checks is named by its code
// (R107.b2 design, "Refusals": zone-band, latitude, zone-exception).
GeoPosition cornerPosition(int zone, bool north, double easting, double northing) {
    try {
        return utmToGeo({zone, north, easting, northing});
    } catch (const UtmRangeRefusal &refusal) {
        switch (refusal.kind()) {
        case UtmRangeRefusal::Kind::Band:
            throw ImageRefusal("zone-band", {std::to_string(zone)});
        case UtmRangeRefusal::Kind::Latitude:
            throw ImageRefusal("latitude");
        case UtmRangeRefusal::Kind::Exception:
            throw ImageRefusal("zone-exception", {refusal.name()});
        }
    }
    throw std::runtime_error("The image's corners cannot be placed in the zone");
}

// The placement the file gives, before the site is considered.
struct Placed {
    Kind kind{};
    QByteArray stored;     // the asset's bytes: the file itself, or a normalized PNG for a GeoTIFF
    int columns{}, rows{};
    int zone{};            // 0 when the file gives none (a world file)
    bool north{true};
    std::string datum;
    double easting{}, northing{}; // the top-left corner, metres in the zone
    double pixelWidth{}, pixelHeight{};
    std::string worldFile, worldSha256;
    std::vector<std::string> notices;
};

// A PNG or JPEG: its size from the reader (refused above 4096 pixels a side), and its placement from the
// world file beside it.
Placed placedFromWorldFile(const QString &path, const QByteArray &bytes, Kind kind,
                           const OrthophotoOptions &options, const std::optional<SiteOrigin> &site) {
    Placed placed;
    placed.kind = kind;
    placed.stored = bytes;
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    const auto size = QImageReader(&buffer).size();
    if (!size.isValid())
        throw ImageRefusal("damaged", {"the image cannot be decoded"});
    if (size.width() > imageGeoPixelLimit || size.height() > imageGeoPixelLimit)
        throw ImageRefusal("image-size", {std::to_string(size.width()) + " × " + std::to_string(size.height())});
    placed.columns = size.width();
    placed.rows = size.height();

    const QFileInfo info(path);
    const auto match = findWorldFile(info);
    if (match.path.isEmpty())
        throw std::runtime_error("The image needs a world file beside it, named like the image with a .pngw, "
                                 ".pgw or .wld extension (for a JPEG, .jpgw, .jgw or .wld)");
    for (const auto &ignored : match.ignored)
        placed.notices.push_back("The world file " + ignored.toStdString() + " is ignored: " +
                                 QFileInfo(match.path).fileName().toStdString() + " is used.");
    QFile worldFile(match.path);
    if (!worldFile.open(QIODevice::ReadOnly))
        throw std::runtime_error("The world file cannot be read");
    const auto text = worldFile.read(1025); // one byte over the limit, so the reader refuses it
    const auto world = readWorldFile(text.toStdString());
    const auto corner = northUpPlacement(world);
    placed.easting = corner.easting;
    placed.northing = corner.northing;
    placed.pixelWidth = corner.pixelWidth;
    placed.pixelHeight = corner.pixelHeight;
    placed.worldFile = QFileInfo(match.path).fileName().toStdString();
    placed.worldSha256 = QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex().toStdString();

    // The zone: the site record's, or the person's choice. The datum is WGS84 unless NAD83 is chosen.
    if (site && site->geographic) {
        placed.zone = site->geographic->utmZone;
        placed.north = site->geographic->utmNorth;
    } else if (options.zone != 0) {
        placed.zone = options.zone;
        placed.north = options.north;
    } else {
        throw std::runtime_error("A world file needs its UTM zone: the site has no geographic record, so give "
                                 "the zone (1 to 60, with N or S)");
    }
    placed.datum = options.datum;
    return placed;
}

// A GeoTIFF: its zone, datum and placement from its tags, its pixels decoded to RGBA and stored as a
// normalized PNG, which the texture reader takes (R107.b2 design, "Limits": stored bytes).
Placed placedFromGeoTiff(const QByteArray &bytes, const OrthophotoOptions &options) {
    Placed placed;
    placed.kind = Kind::GeoTiff;
    const FileBytes source(bytes);
    const auto header = readGeoTiffOrthophoto(source);
    auto rgba = decodeGeoTiffOrthophoto(source, header);
    // The pixels move into the texture: a copy of a 64 MiB image would double the peak.
    const auto texture = TextureImage(int(header.width), int(header.height), std::move(rgba));
    placed.stored = encodeTexturePng(texture);
    if (placed.stored.size() > storedLimit)
        throw ImageRefusal("stored-size", {megabytes(placed.stored.size())});
    placed.columns = int(header.width);
    placed.rows = int(header.height);
    placed.zone = header.zone;
    placed.north = header.north;
    placed.datum = header.datum;
    placed.easting = header.easting;
    placed.northing = header.northing;
    placed.pixelWidth = header.pixelWidth;
    placed.pixelHeight = header.pixelHeight;
    if (options.zone != 0 && (options.zone != header.zone || options.north != header.north))
        placed.notices.push_back("The GeoTIFF names its zone, " + zoneLabel(header.zone, header.north) +
                                 ", so the zone chosen for it is not used.");
    return placed;
}

// The stored image's texture must decode, as a PNG or JPEG of at most 8 bits a channel.
void checkDecodes(const Placed &placed, const AssetRecord &asset) {
    if (placed.kind == Kind::GeoTiff)
        return; // encodeTexturePng wrote it
    const auto decoded = decodeTextureImage(asset);
    if (decoded.status == TextureImageStatus::Ready)
        return;
    if (decoded.status == TextureImageStatus::Unsupported)
        throw ImageRefusal("damaged", {"the pixels are not 8 bits a channel"});
    throw ImageRefusal("damaged", {"the image does not decode"});
}

// A short sentence of the placed size, in kilometres, for the image-extent refusal.
std::string kilometres(double metres) {
    return std::to_string(int(std::round(metres / 1000))) + " km";
}

} // namespace

QJsonObject importOrthophoto(Document &doc, const QString &path, const OrthophotoOptions &options) {
    const QFileInfo info(path);
    const auto bytes = readImage(path);
    const auto kind = kindOf(bytes);
    checkStoredSize(kind, bytes);
    const auto fileName = info.fileName().toStdString();
    const auto sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
    const auto &site = doc.site();

    if (options.credit.size() > imageGeoTextBytes)
        throw ImageRefusal("credit");
    if (options.elevation && (!std::isfinite(*options.elevation) || std::abs(*options.elevation) > 1e6))
        throw ImageRefusal("elevation");
    if (options.datum != "WGS84" && options.datum != "NAD83")
        throw std::runtime_error("The datum must be WGS84, or NAD83 as a labelled approximation");

    Placed placed = kind == Kind::GeoTiff ? placedFromGeoTiff(bytes, options)
                                          : placedFromWorldFile(path, bytes, kind, options, site);
    if (placed.datum == "NAD83")
        placed.notices.push_back("NAD83 is a labelled approximation of WGS84: the image is placed on its "
                                 "grid without a transformation (ADR 0181 §2.3).");

    // The zone must match the site's record, when the site has one (R107.b2 design, "Zone, CRS and datum").
    if (site && site->geographic) {
        const auto &record = *site->geographic;
        if (placed.zone != record.utmZone || placed.north != record.utmNorth)
            throw ImageRefusal("zone-mismatch", {zoneLabel(placed.zone, placed.north),
                                                 zoneLabel(record.utmZone, record.utmNorth)});
    } else if (site) {
        placed.notices.push_back("The site has no geographic record, so the image's zone is not checked "
                                 "against it.");
    }

    // The placed size, and each corner converted with R107.a's series (R107.b2 design, "Limits").
    const double width = placed.columns * placed.pixelWidth, height = placed.rows * placed.pixelHeight;
    if (width > placedLimit || height > placedLimit)
        throw ImageRefusal("image-extent", {kilometres(std::max(width, height))});
    cornerPosition(placed.zone, placed.north, placed.easting, placed.northing);
    cornerPosition(placed.zone, placed.north, placed.easting + width, placed.northing);
    cornerPosition(placed.zone, placed.north, placed.easting, placed.northing - height);
    cornerPosition(placed.zone, placed.north, placed.easting + width, placed.northing - height);

    const auto assetId = doc.nextAssetId();
    const auto mediaType = std::string(placed.kind == Kind::Jpeg ? "image/jpeg" : "image/png");
    const auto stem = info.completeBaseName().toStdString();
    const auto source = ImageGeoSource{
        placed.kind == Kind::Png ? "png" : placed.kind == Kind::Jpeg ? "jpeg" : "geotiff",
        fileName, sha256, placed.worldFile, placed.worldSha256};
    ImageGeoreference georeference;
    georeference.zone = placed.zone;
    georeference.north = placed.north;
    georeference.datum = placed.datum;
    georeference.easting = placed.easting;
    georeference.northing = placed.northing;
    georeference.pixelWidth = placed.pixelWidth;
    georeference.pixelHeight = placed.pixelHeight;
    georeference.columns = placed.columns;
    georeference.rows = placed.rows;
    georeference.source = source;
    georeference.credit = options.credit;
    ReferenceImage image{assetId, width, height, 1.0, georeference};
    image.validate();

    // The site origin: the existing one, or a new one at the image's centre rounded to 100 m, with the
    // geographic record (R107.b2 design, "Placement").
    const bool establishes = !site;
    SiteOrigin origin = site ? *site : SiteOrigin{};
    if (establishes) {
        origin.easting = std::round((placed.easting + width / 2) / 100) * 100;
        origin.northing = std::round((placed.northing - height / 2) / 100) * 100;
        if (origin.easting == 0)
            origin.easting = 0; // no negative zero in reports
        if (origin.northing == 0)
            origin.northing = 0;
        origin.elevation = options.elevation.value_or(0);
        origin.crs = "EPSG:" + std::to_string((placed.north ? 32600 : 32700) + placed.zone);
        origin.established = {fileName, sha256};
        const auto position = cornerPosition(placed.zone, placed.north, origin.easting, origin.northing);
        SiteGeographic geographic;
        geographic.latitude = position.latitude;
        geographic.longitude = position.longitude;
        geographic.datum = placed.datum;
        geographic.utmZone = placed.zone;
        geographic.utmNorth = placed.north;
        geographic.convergence = utmGridConvergence(position, placed.zone);
        geographic.verticalReference = "unknown";
        geographic.provenance = {"imported", fileName, sha256};
        origin.geographic = geographic;
        validateSiteOrigin(origin);
        placed.notices.push_back("The site origin was set at the image's centre, rounded to 100 m.");
    }

    // The image's model position: local = site - origin, for its lower-left corner.
    const double elevation = options.elevation.value_or(origin.elevation);
    const Transform placement = Transform::translation(
        {placed.easting - origin.easting, placed.northing - height - origin.northing, elevation - origin.elevation});

    auto body = std::make_shared<Body>();
    body->id = doc.nextId();
    body->kind = BodyKind::ReferenceImage;
    body->referenceImage = image;
    body->transform = placement;
    body->name = stem.empty() ? "Orthophoto" : stem;

    auto asset = std::make_shared<AssetRecord>(AssetRecord{
        assetId, fileName, mediaType,
        std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(placed.stored.begin(), placed.stored.end()))});
    checkDecodes(placed, *asset);

    Edit edit{"Import orthophoto", {}};
    edit.assets.push_back({assetId, nullptr, asset});
    edit.changes.push_back({body->id, nullptr, body});
    if (establishes)
        edit.site = std::pair{std::optional<SiteOrigin>{}, std::optional<SiteOrigin>{origin}};
    doc.apply(std::move(edit), doc.revision());

    QJsonArray notices;
    for (const auto &notice : placed.notices)
        notices.append(QString::fromStdString(notice));
    return {{"status", "imported"},
            {"body", QString::number(body->id)},
            {"asset", QString::number(assetId)},
            {"format", placed.kind == Kind::Png ? "png" : placed.kind == Kind::Jpeg ? "jpeg" : "geotiff"},
            {"file", QString::fromStdString(fileName)},
            {"columns", placed.columns},
            {"rows", placed.rows},
            {"zone", placed.zone},
            {"hemisphere", placed.north ? "N" : "S"},
            {"datum", QString::fromStdString(placed.datum)},
            {"originEstablished", establishes},
            {"notices", notices}};
}

} // namespace sketchy
