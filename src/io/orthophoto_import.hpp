#pragma once
// R107.b2.1 and R107.b2.2 (ADR 0181, R107.b2 design "Inputs", "Zone, CRS and datum", "Placement" and
// "Credits"): imports an orthophoto, a PNG or JPEG with its world file, or an 8-bit RGB or RGBA GeoTIFF,
// as a georeferenced reference image. The import has three steps, so the desktop can read the file on a
// worker: the file is read (it touches no document), the read is placed (the zone, datum, corners and
// origin, checked against the site), and the placement is applied as one undoable edit.

#include "core/model.hpp"
#include "core/reference_image.hpp"
#include "core/site_origin.hpp"
#include "geo/geographic.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sketchy {

// What the person chooses (R107.b2 design). A zero zone is not chosen.
struct OrthophotoOptions {
    int zone{};                      // UTM zone, 1 to 60; needed for a world file when the site has no record
    bool north{true};                // the hemisphere of the chosen zone
    std::string datum{"WGS84"};      // "WGS84", or "NAD83" as a labelled approximation (decision 3)
    std::string credit;              // optional, at most 256 bytes (decision 2)
    std::optional<double> elevation; // metres in site coordinates; default: the origin's elevation (0 for a new origin)
    std::string name;                // the body's name; empty: the file's name without its extension
};

enum class OrthophotoFormat { Png, Jpeg, GeoTiff };

// Reports the read's progress, as a percentage from 0 to 100 at each stage. The read holds no lock and
// may run on a worker thread.
using OrthophotoProgress = std::function<void(int percent)>;

// The image file, read once and checked (R107.b2.2): its pixels as stored, its size, and the placement
// its own file gives. A world file gives the corner and pixel sizes; a GeoTIFF also gives its zone,
// datum and hemisphere. Reads no document.
struct OrthophotoFile {
    OrthophotoFormat format{};
    std::string fileName, sha256;     // the file's name (not its path) and the digest of its bytes
    std::uint64_t sourceBytes{};      // the file's size as read
    QByteArray stored;                // the asset's bytes: the file itself, or a normalized PNG for a GeoTIFF
    int columns{}, rows{};            // the size in pixels, as read
    std::string pixels;               // "8-bit RGB", "8-bit RGBA", or empty when the format does not say
    int zone{};                       // GeoTIFF only: its zone, 1 to 60; 0 for a world file
    bool north{true};                 // GeoTIFF only
    std::string datum;                // GeoTIFF only: "WGS84" or "NAD83"
    double easting{}, northing{};     // the top-left corner, in metres of the zone (or of the file's zone)
    double pixelWidth{}, pixelHeight{};
    std::string worldFile, worldSha256; // a world file's name and digest, or empty
    std::vector<std::string> notices; // the file's own notices (a world file that is ignored)
};

// Reads and checks the file. Throws ImageRefusal for a file-level refusal (its code and sentence) and
// std::runtime_error for a request the person must correct.
OrthophotoFile readOrthophotoFile(const QString &path, const OrthophotoProgress &progress = {});

// The format from the file's first bytes, or nothing when the file is not a PNG, JPEG or TIFF or cannot be
// read. The desktop uses it to choose the dialog before it reads.
std::optional<OrthophotoFormat> orthophotoFormatOf(const QString &path);

// The world file that would place an image beside it (the first match, as the import takes it), or empty.
QString worldFileBeside(const QString &imagePath);

// True when the file needs a zone the site does not give: a world file, and a site with no geographic
// record. Such a file needs the person's zone (R107.b2 design, "Zone, CRS and datum").
bool orthophotoNeedsZone(const OrthophotoFile &file, const std::optional<SiteOrigin> &site);

// The placement against a site: the zone, datum, corners, origin and record, and the body's transform.
// Pure: it reads no document, so the desktop may place again after each change of an option.
struct OrthophotoPlacement {
    ReferenceImage image;              // the asset is set by applyOrthophoto
    ImageGeoreference georeference;
    SiteOrigin origin;                 // the site's origin, or the new origin this import sets
    bool establishesOrigin{};
    Transform placement;               // the body's local transform (the lower-left corner)
    double elevation{};                // metres in site coordinates
    double width{}, height{};          // the placed size, in metres
    GeoPosition northWest, southEast;  // the corners, for the person to check
    std::string name;
    std::vector<std::string> notices;  // each notice once, in the order the import states them
};

// Throws ImageRefusal for a refusal (its code and sentence) and std::runtime_error for a request the
// person must correct (for example a world file with no zone to take).
OrthophotoPlacement placeOrthophoto(const OrthophotoFile &file, const OrthophotoOptions &options,
                                    const std::optional<SiteOrigin> &site);

// The one undoable edit: the asset, the body and any origin the placement sets. The placement must have
// been made against the document's current site. Returns the import's report.
QJsonObject applyOrthophoto(Document &doc, const OrthophotoFile &file, const OrthophotoPlacement &placement);

// Imports the file as one reference image and returns the report. Throws ImageRefusal for a refusal
// (its code and sentence) and std::runtime_error for a request the person must correct.
QJsonObject importOrthophoto(Document &doc, const QString &path, const OrthophotoOptions &options);

} // namespace sketchy
