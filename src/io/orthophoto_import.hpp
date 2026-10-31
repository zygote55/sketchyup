#pragma once
// R107.b2.1 (ADR 0181, R107.b2 design "Inputs", "Zone, CRS and datum", "Placement" and "Credits"): imports an
// orthophoto, a PNG or JPEG with its world file, or an 8-bit RGB or RGBA GeoTIFF, as a georeferenced
// reference image. The import is one undoable edit: the asset, the body and any origin it sets.

#include "core/model.hpp"

#include <QJsonObject>
#include <QString>

#include <optional>
#include <string>

namespace sketchy {

// What the person chooses (R107.b2 design). A zero zone is not chosen.
struct OrthophotoOptions {
    int zone{};                      // UTM zone, 1 to 60; needed for a world file when the site has no record
    bool north{true};                // the hemisphere of the chosen zone
    std::string datum{"WGS84"};      // "WGS84", or "NAD83" as a labelled approximation (decision 3)
    std::string credit;              // optional, at most 256 bytes (decision 2)
    std::optional<double> elevation; // metres in site coordinates; default: the origin's elevation (0 for a new origin)
};

// Imports the file as one reference image and returns the report. Throws ImageRefusal for a refusal
// (its code and sentence) and std::runtime_error for a request the person must correct.
QJsonObject importOrthophoto(Document &doc, const QString &path, const OrthophotoOptions &options);

} // namespace sketchy
