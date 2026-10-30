// R107.b1 (ADR 0181 §9 budgets): times a GeoTIFF window import of a GLO-30-sized file and reports the
// raster held, the blocks decoded and the process's peak resident memory. Opt-in, built in a
// Release-like configuration:
//   cmake --build <dir> --target geotiff_budget_benchmark
//   geotiff_budget_benchmark FILE WINDOW_METRES
// FILE is written by tests/fixtures/geotiff/make_geotiff_fixtures.py --budget (never committed).
#include "core/terrain_records.hpp"
#include "geo/geographic.hpp"
#include "io/geotiff.hpp"
#include "io/geotiff_terrain.hpp"
#include "io/terrain_import.hpp"

#include <QFile>
#include <QJsonObject>

#include <sys/resource.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace sketchy;

namespace {

double seconds(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
}

long peakResidentKilobytes() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss; // kilobytes on Linux
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Usage: geotiff_budget_benchmark FILE WINDOW_METRES");
        const QString path = QString::fromLocal8Bit(argv[1]);
        const double window = std::atof(argv[2]);
        if (!(window > 0))
            throw std::runtime_error("WINDOW_METRES is positive");

        // The window is centred on a post of the file, in the UTM zone of that longitude.
        GeoTiffTerrainOptions options;
        options.product = DemProduct::Glo30;
        options.north = true;
        options.vertical = "EGM2008";
        GeoTiffHeader header;
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                throw std::runtime_error("The budget file cannot be read");
            const auto bytes = file.readAll();
            GeoTiffMemory memory(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
            header = readGeoTiffHeader(memory);
        }
        double lon = 0, lat = 0;
        header.fromPixel(header.width / 2.0, header.height / 2.0, lon, lat);
        options.zone = utmZoneOfLongitude(lon);
        const auto centre = geoToUtm({lat, lon}, options.zone);
        options.easting0 = centre.easting - window / 2;
        options.easting1 = centre.easting + window / 2;
        options.northing0 = centre.northing - window / 2;
        options.northing1 = centre.northing + window / 2;

        const auto parseStart = std::chrono::steady_clock::now();
        const auto set = readGeoTiffTerrain(path, options);
        const double parse = seconds(parseStart);

        const auto importStart = std::chrono::steady_clock::now();
        TerrainImportOptions importOptions;
        const TerrainImportProvenance provenance{"budget.tif", "EPSG:32630"};
        const auto result = runTerrainImport(set, importOptions, std::nullopt, provenance, nullptr);
        const double import = seconds(importStart);
        if (!result.ok)
            throw std::runtime_error("The window did not import: " + result.message);

        const auto &dem = *set.dem;
        std::cout << "file=" << argv[1] << " window_m=" << window << " posts=" << set.points.size()
                  << " spacing_m=" << dem.spacing << " nominal_m=" << dem.nominalSpacing
                  << " decimated=" << (dem.spacing > dem.nominalSpacing ? "yes" : "no")
                  << " triangles=" << result.mesh.triangles.size()
                  << " parse_s=" << parse << " import_s=" << import
                  << " total_s=" << parse + import
                  << " raster_peak_mib=" << set.report["dem"].toObject()["peakRasterBytes"].toDouble() / (1024 * 1024)
                  << " blocks=" << set.report["dem"].toObject()["blocksDecoded"].toDouble()
                  << " maxrss_mib=" << peakResidentKilobytes() / 1024.0 << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
