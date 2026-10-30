#pragma once
// R107.b1 (ADR 0181 §2.7 to §2.9, §3.2, §8 V3 and V5): a GeoTIFF elevation file that the user
// downloaded (USGS 3DEP or Copernicus GLO-30), read in a window the user names in UTM, resampled
// bilinearly onto a regular UTM grid at an explicit spacing, and returned as a terrain source set
// in metres. The import then runs as the DXF and point-file imports do (runTerrainImport): the same
// origin rules, record, report and limits. No network code: the file is read from disk, in windows.
//
// Void policy (ADR 0181 §2.8, decided with R107.b1): a grid post whose bilinear support uses a
// void (no-data or NaN) post is not filled. The window is refused and the voids are counted.
// Sizing: a window whose grid would exceed the terrain limits (250,000 triangles and 250,000
// vertices, TerrainLimits) is decimated to the smallest whole-metre spacing that fits, and the
// spacing is reported.
#include "io/geotiff.hpp"
#include "io/terrain_source.hpp"
#include <QString>
#include <cstdint>
#include <optional>
#include <string>

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
    std::optional<double> verticalOffset; // metres added to the file's heights (V5)
    std::string verticalNote;  // required with the offset
};

// Reads the window from the file at `path` and returns the source set (format "geotiff", unit m,
// one points layer named "posts", the SHA-256 of the whole file and the source record). Refusals
// throw TerrainError: INPUT_ERROR for a file or feature outside the subset, INVALID_REQUEST for an
// option or a window the file does not cover. With `progress`, reports TerrainStage::Parse.
TerrainSourceSet readGeoTiffTerrain(const QString &path, const GeoTiffTerrainOptions &options,
                                    TerrainProgress *progress = nullptr);

} // namespace sketchy
