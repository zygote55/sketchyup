#pragma once
// R107.b1 (ADR 0181 §2.7 to §2.9, §3.2, §8 V3 and V5): a GeoTIFF elevation file that the user
// downloaded (USGS 3DEP or Copernicus GLO-30), read in a window the user names in UTM, resampled
// bilinearly onto a regular UTM grid at an explicit spacing, and returned as a terrain source set
// in metres. The import then runs as the DXF and point-file imports do (runTerrainImport): the same
// origin rules, record, report and limits. No network code: the file is read from disk, in windows.
//
// R107.b3 (UX §20.7): the reader is path-based for the desktop too (the 64 MiB rule of the text
// formats does not apply); geoTiffFacts reads a file's facts from its header; the spacing takes the
// room the model leaves; a refusal carries a stable code and a plain sentence (GeoTiffRefusalCode
// and TerrainError::refusal); a window refused for voids lists them; and the digest and the posts
// describe one file (the size and modification time are checked after the read).
//
// Void policy (ADR 0181 §2.8, decided with R107.b1): a grid post whose bilinear support uses a
// void (no-data or NaN) post is not filled. The window is refused and the voids are counted.
// Sizing: a window whose grid would exceed the terrain limits (250,000 triangles and 250,000
// vertices, TerrainLimits) is decimated to the smallest whole-metre spacing that fits, and the
// spacing is reported. R107.b3: the triangle limit is the room the model leaves
// (GeoTiffTerrainOptions::otherTerrainTriangles), and a window is refused only when no spacing fits.
#include "core/terrain_records.hpp"
#include "io/geotiff.hpp"
#include "io/terrain_source.hpp"
#include <QString>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sketchy {

// The products the subset is for (ADR 0181 §7.2). 3DEP is USGS public domain; GLO-30 is under the
// Copernicus WorldDEM-30 licence, and its heights are a surface model.
enum class DemProduct : std::uint8_t { Dep3, Glo30 };
std::optional<DemProduct> demProduct(const QString &name); // "3dep" or "glo30"
const char *demProductName(DemProduct product);

struct GeoTiffTerrainOptions {
    DemProduct product{DemProduct::Dep3};
    int zone{};                // UTM zone of the window and the grid, 1 to 60
    bool north{true};          // its hemisphere
    double easting0{}, northing0{}, easting1{}, northing1{}; // the window, metres
    std::optional<double> spacing; // metres; default: the file's nominal post size, rounded
    std::string vertical;      // NAVD88, EGM2008 or EGM96, when the file names none (or to confirm it)
    std::optional<double> verticalOffset; // metres added to the file's heights (V5); never zero
    std::string verticalNote;  // required with the offset
    // R107.b3 (UX §20.7 item 4): the triangles the model's other terrains hold. The grid is decimated
    // to fit the room left, TerrainLimits::triangles less these, and is refused only when no spacing fits.
    std::size_t otherTerrainTriangles{};
};

// The void posts a window was refused for (UX §20.7 item 11): the count, and the UTM positions (E
// and N, in metres) of the first geoTiffVoidsListed of them in grid order.
inline constexpr std::size_t geoTiffVoidsListed = 4096;
class GeoTiffVoidRefusal : public TerrainError {
  public:
    GeoTiffVoidRefusal(const std::string &message, std::string sentence, std::uint64_t count,
                       std::vector<std::array<double, 2>> positions)
        : TerrainError("INVALID_REQUEST", message, "window-void", std::move(sentence)),
          count_(count), positions_(std::move(positions)) {}
    std::uint64_t count() const { return count_; }
    const std::vector<std::array<double, 2>> &positions() const { return positions_; }

  private:
    std::uint64_t count_{};
    std::vector<std::array<double, 2>> positions_;
};

// Reads the window from the file at `path` and returns the source set (format "geotiff", unit m,
// one points layer named "posts", the SHA-256 of the whole file and the source record). Refusals
// throw TerrainError: INPUT_ERROR for a file or feature outside the subset, INVALID_REQUEST for an
// option or a window the file does not cover, LIMIT_EXCEEDED when the model has no room. Each carries
// a refusal code and a plain sentence (R107.b3); a void refusal is a GeoTiffVoidRefusal. With
// `progress`, reports TerrainStage::Parse.
TerrainSourceSet readGeoTiffTerrain(const QString &path, const GeoTiffTerrainOptions &options,
                                    TerrainProgress *progress = nullptr);
// The same read, with `afterDigest` run once the header and the digest are read and before the
// posts are (the tests change the file there; readGeoTiffTerrain passes nothing).
TerrainSourceSet readGeoTiffTerrainChecked(const QString &path, const GeoTiffTerrainOptions &options,
                                           TerrainProgress *progress,
                                           const std::function<void()> &afterDigest);

// R107.b3 (UX §20.7 item 3): the facts of a file, from its header alone; no post is read.
struct GeoTiffFacts {
    std::uint32_t width{}, height{};                  // the full-resolution image, in posts
    double postArcSecondsEastWest{}, postArcSecondsNorthSouth{}; // the post steps, in arc-seconds
    double postMetresEastWest{}, postMetresNorthSouth{};         // nominal, at the extent's centre latitude, whole metres
    std::vector<DemProduct> products;                 // the products whose post size the file has (none, one or both)
    std::string horizontalDatum;                      // "WGS84" or "NAD83" (a labelled approximation)
    int geographicKey{};                              // GeoKey 2048: 4326 or 4269
    std::uint16_t verticalKey{};                      // GeoKey 4096: 5703, 3855 or 5773; 0 when the file names none
    std::string verticalReference;                    // "NAVD88", "EGM2008", "EGM96", or empty
    std::string compression;                          // "none", "LZW" or "Deflate"
    bool pixelIsArea{true};                           // false for pixel-is-point (GLO-30)
    double minLongitude{}, minLatitude{}, maxLongitude{}, maxLatitude{}; // degrees: the posts' extent
    std::vector<int> zones;                           // the UTM zones the extent touches, ascending
};
// Throws TerrainError (INPUT_ERROR, with its refusal code) for a file the reader does not accept.
GeoTiffFacts geoTiffFacts(const QString &path);

} // namespace sketchy
