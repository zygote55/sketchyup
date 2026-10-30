#pragma once
// R107.b4: one dry run of the terrain import dialog, shared by its settings page
// (terrain_import_dialog.cpp) and its GeoTIFF page (terrain_import_dialog_dem.cpp): the core's
// terrain.import without the edit, for one set of settings, on the worker.
#include "app/terrain_preview.hpp"
#include "automation/terrain_commands.hpp"
#include "io/geotiff_terrain.hpp"
#include <QJsonObject>
#include <QString>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sketchy {

// One dry run: the core's terrain.import without the edit, for one set of settings.
struct TerrainImportDryRun {
    QJsonObject command;
    bool unitsChosen{};
    bool cancelled{}; // D-044: stopped by its TerrainRunControl; never shown
    std::uint64_t revision{};
    QString error;              // the source could not be read or the request was refused
    bool unitsMissing{};        // DXF: no usable $INSUNITS, so units must be chosen
    QString headerUnit;         // DXF: the unit $INSUNITS declares, when it does
    std::vector<TerrainSourceLayer> layers;
    QJsonObject sourceReport;   // the parser's report (delimiter, header, columns, ...)
    double metresPerUnit{1};
    std::optional<TerrainImportResult> result; // when units are chosen and the source parsed
    std::string name{"Terrain"};
    std::shared_ptr<const TerrainPreviewData> preview;
    // R107.b4: a GeoTIFF's refusal: the reader's stable code and plain sentence, and for a window
    // refused for voids, the count and the first void posts' UTM positions.
    bool geotiff{};
    std::string refusal;
    QString sentence;
    std::uint64_t voidCount{};
    std::vector<std::array<double, 2>> voids;
};

} // namespace sketchy
