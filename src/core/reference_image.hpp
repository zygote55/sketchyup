#pragma once
#include "core/transform.hpp"

#include <optional>
#include <string>

namespace sketchy {

// Where a georeferenced image came from (ADR 0181, R107.b2 design "The record (schema 35)"): the
// format, the file's name (not its path) and SHA-256, and for a PNG or JPEG the world file's name and
// SHA-256. A GeoTIFF has no world file.
struct ImageGeoSource {
    std::string format;                 // "png", "jpeg" or "geotiff"
    std::string file;                   // the image's file name, at most 256 bytes
    std::string sha256;                 // of the image file's bytes, lower-case hex
    std::string worldFile, worldSha256; // the world file's name and digest, or empty
    bool operator==(const ImageGeoSource &) const = default;
};

// The position of a placed orthophoto on the ground (R107.b2 design, schema 35, required feature
// reference-geo-v1). The corner is the image's top-left corner in UTM metres of the zone, so one
// corner and two pixel sizes place the image exactly. The image is north-up.
struct ImageGeoreference {
    int zone{};                         // UTM zone, 1 to 60
    bool north{true};                   // the hemisphere of the zone grid
    std::string datum;                  // "WGS84", or "NAD83" as a labelled approximation
    double easting{}, northing{};       // the top-left corner, in metres in the zone
    double pixelWidth{}, pixelHeight{}; // metres per pixel, east and north, both positive
    int columns{}, rows{};              // the image's size in pixels, as read
    ImageGeoSource source;
    std::string credit;                 // optional, at most 256 bytes
    bool operator==(const ImageGeoreference &) const = default;
};

// Bounds of the georeference record (R107.b2 design, "Limits"). Each text field is at most 256 bytes.
inline constexpr std::size_t imageGeoTextBytes = 256;
inline constexpr int imageGeoPixelLimit = 4096;

// Checks a georeference against the placed size (throws std::runtime_error naming the bound).
void validateImageGeoreference(const ImageGeoreference &geo, double width, double height);

// A visual image plane, with no modeled vertices, edges or faces. Managed asset
// identity is distinct from placement; resolving pixels belongs to the IO layer.
struct ReferenceImage {
    Id asset{};
    double width{1}, height{1}, opacity{1};
    // Schema 35 (R107.b2): an orthophoto's position on the ground. Absent for every other image.
    std::optional<ImageGeoreference> georeference;
    void validate() const;
    bool operator==(const ReferenceImage &) const = default;
};

// Normalized image coordinates: (0,0) is top left, (1,1) is bottom right.
using ImagePoint = std::array<double, 2>;

Vec3 referenceImagePoint(const ReferenceImage &image, ImagePoint point);

// Lower left, lower right, upper right, upper left in world space.
std::array<Vec3, 4> referenceImageCorners(const ReferenceImage &image, const Transform &world);

struct CalibratedReferenceImage {
    ReferenceImage image;
    Transform local;
};

// Uniformly resize physical dimensions to match the measured world-space distance.
// The first anchor stays fixed; local axes and the full parent affine placement remain.
CalibratedReferenceImage calibrateReferenceImage(const ReferenceImage &image,
                                                 const Transform &local, const Transform &parent,
                                                 ImagePoint first, ImagePoint second,
                                                 double knownLength);

} // namespace sketchy
