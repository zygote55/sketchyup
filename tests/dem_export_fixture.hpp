#pragma once
// R107.c1 (ADR 0181 §3.4, V6): a document whose terrain came from a committed GeoTIFF fixture, for the
// export credit tests. The fixtures tie at 0.2 W, 51.6 N (tests/fixtures/geotiff/make_geotiff_fixtures.py),
// and each window is centred on a post near the tie, in UTM zone 30.
#include "core/model.hpp"
#include "core/terrain.hpp"
#include "geo/geographic.hpp"
#include "io/geotiff_terrain.hpp"
#include "io/terrain_import.hpp"

#include <stdexcept>
#include <string>

namespace sketchy {

// The notice texts the fixtures carry (ADR 0181 §3.1; the record's own wording, as R107.b1 wrote it).
inline const char *demCreditUsgs = "Credit: U.S. Geological Survey";
inline const char *demCreditGlo30 =
    "produced using Copernicus WorldDEM-30 © DLR e.V. 2010-2014 and © Airbus Defence and Space "
    "GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA; all rights reserved. The "
    "organisations in charge of the Copernicus programme by law or by delegation do not incur any "
    "liability for any use of the Copernicus WorldDEM-30.";

// One fixture's window, read into a source set (the R107.b1 reader, with the window the tests use).
inline TerrainSourceSet demFixtureSet(const std::string &fixtures, const std::string &file,
                                      DemProduct product, const std::string &vertical) {
    const bool dep3 = product == DemProduct::Dep3;
    // 3DEP: post (24, 20) of the 1 arc-second plane. GLO-30: post (20, 15) of the pixel-is-point file.
    const GeoPosition at = dep3 ? GeoPosition{51.6 - 20.0 / 3600.0, -0.2 + 24.0 / 3600.0}
                                : GeoPosition{51.6 - 15.0 / 3600.0, -0.2 + 20.0 / 2400.0};
    const auto centre = geoToUtm(at, 30);
    const double half = dep3 ? 150 : 60;
    GeoTiffTerrainOptions options;
    options.product = product;
    options.zone = 30;
    options.north = true;
    options.easting0 = centre.easting - half;
    options.northing0 = centre.northing - half;
    options.easting1 = centre.easting + half;
    options.northing1 = centre.northing + half;
    options.vertical = vertical;
    return readGeoTiffTerrain(QString::fromStdString(fixtures + file), options);
}

// Imports a source set as a terrain into `document`. The first import makes the site origin from the
// window's centre; later imports use that origin, as the desktop does.
inline void importDemSet(Document &document, const TerrainSourceSet &set, const std::string &file) {
    const auto result = runTerrainImport(set, TerrainImportOptions{}, document.site(),
                                         TerrainImportProvenance{file, "EPSG:32630"});
    if (!result.ok)
        throw std::runtime_error("DEM fixture import refused: " + result.message);
    importTerrain(document, result.mesh, result.record, result.establish, "Terrain");
}

inline void importDemFixture(Document &document, const std::string &fixtures, const std::string &file,
                             DemProduct product, const std::string &vertical) {
    importDemSet(document, demFixtureSet(fixtures, file, product, vertical), file);
}

} // namespace sketchy
