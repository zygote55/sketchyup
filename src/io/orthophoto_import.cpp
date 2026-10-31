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

// The format, from the file's first bytes, never from its name (R107.b2 design, "Refusals": image-type).
std::optional<OrthophotoFormat> formatOf(const QByteArray &bytes) {
    const auto starts = [&bytes](std::string_view magic) {
        return bytes.size() >= qsizetype(magic.size()) &&
               std::equal(magic.begin(), magic.end(), bytes.begin());
    };
    if (starts("\x89PNG\r\n\x1a\n"))
        return OrthophotoFormat::Png;
    if (starts("\xff\xd8\xff"))
        return OrthophotoFormat::Jpeg;
    if (starts(std::string_view("II*\0", 4)) || starts(std::string_view("MM\0*", 4)))
        return OrthophotoFormat::GeoTiff;
    return std::nullopt;
}

OrthophotoFormat kindOf(const QByteArray &bytes) {
    const auto format = formatOf(bytes);
    if (!format)
        throw ImageRefusal("image-type");
    return *format;
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
void checkStoredSize(OrthophotoFormat kind, const QByteArray &bytes) {
    if (kind != OrthophotoFormat::GeoTiff && bytes.size() > storedLimit)
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

// A PNG's pixels, from its header: the bit depth and colour type (the header is at 16 bytes, and its
// IHDR chunk holds both at offsets 24 and 25). Empty when the header is too short to say.
std::string pngPixels(const QByteArray &bytes) {
    if (bytes.size() < 26)
        return {};
    const auto depth = std::to_string(std::uint8_t(bytes[24]));
    switch (std::uint8_t(bytes[25])) {
    case 0:
        return depth + "-bit grey";
    case 2:
        return depth + "-bit RGB";
    case 3:
        return depth + "-bit palette";
    case 4:
        return depth + "-bit grey and alpha";
    case 6:
        return depth + "-bit RGBA";
    default:
        return {};
    }
}

// A PNG or JPEG: its size from the reader (refused above 4096 pixels a side), and its placement from the
// world file beside it.
void readWorldFileImage(const QString &path, const QByteArray &bytes, OrthophotoFormat kind,
                        OrthophotoFile &file) {
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    const auto size = QImageReader(&buffer).size();
    if (!size.isValid())
        throw ImageRefusal("damaged", {"the image cannot be decoded"});
    if (size.width() > imageGeoPixelLimit || size.height() > imageGeoPixelLimit)
        throw ImageRefusal("image-size", {std::to_string(size.width()) + " × " + std::to_string(size.height())});
    file.columns = size.width();
    file.rows = size.height();
    if (kind == OrthophotoFormat::Png)
        file.pixels = pngPixels(bytes);

    const QFileInfo info(path);
    const auto match = findWorldFile(info);
    if (match.path.isEmpty())
        throw std::runtime_error("The image needs a world file beside it, named like the image with a .pngw, "
                                 ".pgw or .wld extension (for a JPEG, .jpgw, .jgw or .wld)");
    for (const auto &ignored : match.ignored)
        file.notices.push_back("The world file " + ignored.toStdString() + " is ignored: " +
                               QFileInfo(match.path).fileName().toStdString() + " is used.");
    QFile worldFile(match.path);
    if (!worldFile.open(QIODevice::ReadOnly))
        throw std::runtime_error("The world file cannot be read");
    const auto text = worldFile.read(1025); // one byte over the limit, so the reader refuses it
    const auto world = readWorldFile(text.toStdString());
    const auto corner = northUpPlacement(world);
    file.easting = corner.easting;
    file.northing = corner.northing;
    file.pixelWidth = corner.pixelWidth;
    file.pixelHeight = corner.pixelHeight;
    file.worldFile = QFileInfo(match.path).fileName().toStdString();
    file.worldSha256 = QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex().toStdString();
}

// A GeoTIFF: its zone, datum and placement from its tags, its pixels decoded to RGBA and stored as a
// normalized PNG, which the texture reader takes (R107.b2 design, "Limits": stored bytes).
void readGeoTiffImage(const QByteArray &bytes, OrthophotoFile &file) {
    const FileBytes source(bytes);
    const auto header = readGeoTiffOrthophoto(source);
    auto rgba = decodeGeoTiffOrthophoto(source, header);
    // The pixels move into the texture: a copy of a 64 MiB image would double the peak.
    const auto texture = TextureImage(int(header.width), int(header.height), std::move(rgba));
    file.stored = encodeTexturePng(texture);
    if (file.stored.size() > storedLimit)
        throw ImageRefusal("stored-size", {megabytes(file.stored.size())});
    file.columns = int(header.width);
    file.rows = int(header.height);
    file.pixels = header.samples == 4 ? "8-bit RGBA" : "8-bit RGB";
    file.zone = header.zone;
    file.north = header.north;
    file.datum = header.datum;
    file.easting = header.easting;
    file.northing = header.northing;
    file.pixelWidth = header.pixelWidth;
    file.pixelHeight = header.pixelHeight;
}

// A short sentence of the placed size, in kilometres, for the image-extent refusal.
std::string kilometres(double metres) {
    return std::to_string(int(std::round(metres / 1000))) + " km";
}

std::string formatName(OrthophotoFormat format) {
    switch (format) {
    case OrthophotoFormat::Png:
        return "png";
    case OrthophotoFormat::Jpeg:
        return "jpeg";
    case OrthophotoFormat::GeoTiff:
        return "geotiff";
    }
    return {};
}

} // namespace

std::optional<OrthophotoFormat> orthophotoFormatOf(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return formatOf(file.read(8));
}

QString worldFileBeside(const QString &imagePath) {
    return findWorldFile(QFileInfo(imagePath)).path;
}

OrthophotoFile readOrthophotoFile(const QString &path, const OrthophotoProgress &progress) {
    const auto report = [&progress](int percent) {
        if (progress)
            progress(percent);
    };
    report(0);
    const QFileInfo info(path);
    const auto bytes = readImage(path);
    const auto kind = kindOf(bytes);
    checkStoredSize(kind, bytes);
    OrthophotoFile file;
    file.format = kind;
    file.fileName = info.fileName().toStdString();
    file.sha256 = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
    file.sourceBytes = std::uint64_t(bytes.size());
    report(25);
    if (kind == OrthophotoFormat::GeoTiff) {
        readGeoTiffImage(bytes, file);
        report(75);
        return file;
    }
    readWorldFileImage(path, bytes, kind, file);
    report(50);
    // The stored image's texture must decode, as a PNG or JPEG of at most 8 bits a channel.
    file.stored = bytes;
    const AssetRecord asset{
        1, file.fileName, kind == OrthophotoFormat::Jpeg ? "image/jpeg" : "image/png",
        std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()))};
    const auto decoded = decodeTextureImage(asset);
    if (decoded.status == TextureImageStatus::Unsupported)
        throw ImageRefusal("damaged", {"the pixels are not 8 bits a channel"});
    if (decoded.status != TextureImageStatus::Ready)
        throw ImageRefusal("damaged", {"the image does not decode"});
    report(90);
    return file;
}

bool orthophotoNeedsZone(const OrthophotoFile &file, const std::optional<SiteOrigin> &site) {
    return file.format != OrthophotoFormat::GeoTiff && !(site && site->geographic);
}

OrthophotoPlacement placeOrthophoto(const OrthophotoFile &file, const OrthophotoOptions &options,
                                    const std::optional<SiteOrigin> &site) {
    if (options.credit.size() > imageGeoTextBytes)
        throw ImageRefusal("credit");
    if (options.elevation && (!std::isfinite(*options.elevation) || std::abs(*options.elevation) > 1e6))
        throw ImageRefusal("elevation");
    if (options.datum != "WGS84" && options.datum != "NAD83")
        throw std::runtime_error("The datum must be WGS84, or NAD83 as a labelled approximation");

    OrthophotoPlacement result;
    result.notices = file.notices;

    // The zone and datum: a GeoTIFF's own, else the site record's, else the person's choice (R107.b2 design,
    // "Zone, CRS and datum"). A world file's datum is the person's.
    int zone{};
    bool north{true};
    std::string datum;
    if (file.format == OrthophotoFormat::GeoTiff) {
        zone = file.zone;
        north = file.north;
        datum = file.datum;
        if (options.zone != 0 && (options.zone != file.zone || options.north != file.north))
            result.notices.push_back("The GeoTIFF names its zone, " + zoneLabel(file.zone, file.north) +
                                     ", so the zone chosen for it is not used.");
    } else {
        if (site && site->geographic) {
            zone = site->geographic->utmZone;
            north = site->geographic->utmNorth;
        } else if (options.zone != 0) {
            zone = options.zone;
            north = options.north;
        } else {
            throw std::runtime_error("A world file needs its UTM zone: the site has no geographic record, so give "
                                     "the zone (1 to 60, with N or S)");
        }
        datum = options.datum;
    }
    if (datum == "NAD83")
        result.notices.push_back("NAD83 is a labelled approximation of WGS84: the image is placed on its "
                                 "grid without a transformation (ADR 0181 §2.3).");

    // The zone must match the site's record, when the site has one (R107.b2 design, "Zone, CRS and datum").
    if (site && site->geographic) {
        const auto &record = *site->geographic;
        if (zone != record.utmZone || north != record.utmNorth)
            throw ImageRefusal("zone-mismatch", {zoneLabel(zone, north), zoneLabel(record.utmZone, record.utmNorth)});
    } else if (site) {
        result.notices.push_back("The site has no geographic record, so the image's zone is not checked "
                                 "against it.");
    }

    // The placed size, and each corner converted with R107.a's series (R107.b2 design, "Limits").
    const double width = file.columns * file.pixelWidth, height = file.rows * file.pixelHeight;
    if (width > placedLimit || height > placedLimit)
        throw ImageRefusal("image-extent", {kilometres(std::max(width, height))});
    result.northWest = cornerPosition(zone, north, file.easting, file.northing);
    cornerPosition(zone, north, file.easting + width, file.northing);
    cornerPosition(zone, north, file.easting, file.northing - height);
    result.southEast = cornerPosition(zone, north, file.easting + width, file.northing - height);

    const auto source = ImageGeoSource{formatName(file.format), file.fileName, file.sha256, file.worldFile,
                                       file.worldSha256};
    ImageGeoreference georeference;
    georeference.zone = zone;
    georeference.north = north;
    georeference.datum = datum;
    georeference.easting = file.easting;
    georeference.northing = file.northing;
    georeference.pixelWidth = file.pixelWidth;
    georeference.pixelHeight = file.pixelHeight;
    georeference.columns = file.columns;
    georeference.rows = file.rows;
    georeference.source = source;
    georeference.credit = options.credit;
    const ReferenceImage checked{1, width, height, 1.0, georeference};
    checked.validate();
    result.image = ReferenceImage{0, width, height, 1.0, georeference};
    result.georeference = georeference;
    result.width = width;
    result.height = height;

    // The site origin: the existing one, or a new one at the image's centre rounded to 100 m, with the
    // geographic record (R107.b2 design, "Placement").
    result.establishesOrigin = !site;
    result.origin = site ? *site : SiteOrigin{};
    auto &origin = result.origin;
    if (result.establishesOrigin) {
        origin.easting = std::round((file.easting + width / 2) / 100) * 100;
        origin.northing = std::round((file.northing - height / 2) / 100) * 100;
        if (origin.easting == 0)
            origin.easting = 0; // no negative zero in reports
        if (origin.northing == 0)
            origin.northing = 0;
        origin.elevation = options.elevation.value_or(0);
        origin.crs = "EPSG:" + std::to_string((north ? 32600 : 32700) + zone);
        origin.established = {file.fileName, file.sha256};
        const auto position = cornerPosition(zone, north, origin.easting, origin.northing);
        SiteGeographic geographic;
        geographic.latitude = position.latitude;
        geographic.longitude = position.longitude;
        geographic.datum = datum;
        geographic.utmZone = zone;
        geographic.utmNorth = north;
        geographic.convergence = utmGridConvergence(position, zone);
        geographic.verticalReference = "unknown";
        geographic.provenance = {"imported", file.fileName, file.sha256};
        origin.geographic = geographic;
        validateSiteOrigin(origin);
        result.notices.push_back("The site origin was set at the image's centre, rounded to 100 m.");
    }

    // The image's model position: local = site - origin, for its lower-left corner.
    result.elevation = options.elevation.value_or(origin.elevation);
    result.placement = Transform::translation({file.easting - origin.easting,
                                               file.northing - height - origin.northing,
                                               result.elevation - origin.elevation});
    const auto stem = QFileInfo(QString::fromStdString(file.fileName)).completeBaseName().toStdString();
    const auto name = options.name.empty() ? stem : options.name;
    result.name = name.empty() ? "Orthophoto" : name;
    return result;
}

QJsonObject applyOrthophoto(Document &doc, const OrthophotoFile &file, const OrthophotoPlacement &placement) {
    const auto assetId = doc.nextAssetId();
    auto image = placement.image;
    image.asset = assetId;
    image.validate();

    auto body = std::make_shared<Body>();
    body->id = doc.nextId();
    body->kind = BodyKind::ReferenceImage;
    body->referenceImage = image;
    body->transform = placement.placement;
    body->name = placement.name;

    const auto mediaType = std::string(file.format == OrthophotoFormat::Jpeg ? "image/jpeg" : "image/png");
    auto asset = std::make_shared<AssetRecord>(AssetRecord{
        assetId, file.fileName, mediaType,
        std::make_shared<const AssetPayload>(std::vector<std::uint8_t>(file.stored.begin(), file.stored.end()))});

    Edit edit{"Import orthophoto", {}};
    edit.assets.push_back({assetId, nullptr, asset});
    edit.changes.push_back({body->id, nullptr, body});
    if (placement.establishesOrigin)
        edit.site = std::pair{std::optional<SiteOrigin>{}, std::optional<SiteOrigin>{placement.origin}};
    doc.apply(std::move(edit), doc.revision());

    QJsonArray notices;
    for (const auto &notice : placement.notices)
        notices.append(QString::fromStdString(notice));
    return {{"status", "imported"},
            {"body", QString::number(body->id)},
            {"asset", QString::number(assetId)},
            {"format", QString::fromStdString(formatName(file.format))},
            {"file", QString::fromStdString(file.fileName)},
            {"columns", file.columns},
            {"rows", file.rows},
            {"zone", placement.georeference.zone},
            {"hemisphere", placement.georeference.north ? "N" : "S"},
            {"datum", QString::fromStdString(placement.georeference.datum)},
            {"originEstablished", placement.establishesOrigin},
            {"notices", notices}};
}

QJsonObject importOrthophoto(Document &doc, const QString &path, const OrthophotoOptions &options) {
    const auto file = readOrthophotoFile(path);
    const auto placement = placeOrthophoto(file, options, doc.site());
    return applyOrthophoto(doc, file, placement);
}

} // namespace sketchy
