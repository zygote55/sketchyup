#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "io/terrain_import.hpp"
#include "io/terrain_io.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace sketchy {
namespace {
[[noreturn]] void reject(const char *code, const std::string &message) {
    throw TerrainError(code, message);
}
double coordinate(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
        std::abs(value.toDouble()) > siteCoordinateLimit)
        reject("INVALID_REQUEST", "Expected a finite coordinate within 100,000 km");
    return value.toDouble();
}
std::string text(const QJsonObject &command, const char *key) {
    if (!command.contains(key))
        return {};
    if (!command[key].isString())
        reject("INVALID_REQUEST", std::string("Expected a string for ") + key);
    return command[key].toString().toStdString();
}
Id identity(const QJsonValue &value) {
    bool ok = false;
    const auto id = value.toString().toULongLong(&ok);
    if (!value.isString() || !ok || !id || QString::number(id) != value.toString())
        reject("INVALID_REQUEST", "Expected canonical string ID");
    return id;
}
void onlyFields(const QJsonObject &command, std::initializer_list<const char *> allowed) {
    for (auto it = command.begin(); it != command.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(),
                         [&](const char *name) { return it.key() == name; }))
            reject("INVALID_REQUEST", "Unknown command parameter");
}
QJsonObject origin(const SiteOrigin &site) {
    return {{"easting", site.easting},
            {"northing", site.northing},
            {"elevation", site.elevation}};
}
const char *roleName(TerrainSourceRole role) {
    switch (role) {
    case TerrainSourceRole::Contour:
        return "contour";
    case TerrainSourceRole::Breakline:
        return "breakline";
    case TerrainSourceRole::Boundary:
        return "boundary";
    case TerrainSourceRole::Hole:
        return "hole";
    case TerrainSourceRole::Points:
        return "points";
    }
    return "contour";
}

// ---- R095.c terrain sources ---------------------------------------------------------------
QByteArray sourceData(const QJsonObject &command) {
    const auto value = command["data"];
    if (!value.isString())
        reject("INVALID_REQUEST", "Terrain source data must be base64 text");
    const auto text = value.toString();
    if (text.size() > terrainDataCharacters)
        reject("LIMIT_EXCEEDED", "Encoded terrain source exceeds 16 MiB");
    const auto encoded = text.toLatin1();
    const auto decoded =
        QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (QString::fromLatin1(encoded) != text || !decoded || decoded.decoded.toBase64() != encoded)
        reject("INVALID_REQUEST", "Terrain source data must be canonical base64");
    return decoded.decoded;
}
std::string boundedText(const QJsonObject &command, const char *key, std::size_t bytes) {
    const auto value = text(command, key);
    if (value.size() > bytes || std::any_of(value.begin(), value.end(), [](char c) {
            return static_cast<unsigned char>(c) < 0x20;
        }))
        reject("INVALID_REQUEST", std::string(key) + " is at most " + std::to_string(bytes) +
                                      " bytes without control characters");
    return value;
}
using ParsedSource = TerrainSourceRequest;
ParsedSource parseSource(const QByteArray &bytes, const QJsonObject &command, bool importing) {
    const std::initializer_list<const char *> common{
        "command",   "data",      "format",     "units",       "origin",        "crs",
        "sourceName", "roles",    "columns",    "delimiter",   "header",        "flatMode",
        "steinerFill", "maxEdgeLength", "chordTolerance"};
    std::vector<const char *> allowed(common);
    if (importing)
        allowed.push_back("name");
    for (auto it = command.begin(); it != command.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(),
                         [&](const char *name) { return it.key() == name; }))
            reject("INVALID_REQUEST", "Unknown command parameter");
    ParsedSource parsed;
    const auto format = command["format"].toString();
    if (format != "dxf" && format != "points")
        reject("INVALID_REQUEST", "Terrain source format is \"dxf\" or \"points\"");
    std::optional<TerrainUnit> unit;
    if (command.contains("units")) {
        const auto name = command["units"].toString();
        if (!(format == "dxf" && name == "header")) {
            unit = terrainUnit(name);
            if (!unit)
                reject("INVALID_REQUEST",
                       "Terrain units are " + terrainUnitNames().toStdString() +
                           (format == "dxf" ? std::string(", or header") : std::string()));
        }
    }
    auto number = [&](const char *key, double low, double high) {
        const auto value = command[key];
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < low ||
            value.toDouble() > high)
            reject("INVALID_REQUEST", std::string(key) + " is out of range");
        return value.toDouble();
    };
    double chord = 0.05;
    if (command.contains("chordTolerance"))
        chord = number("chordTolerance", 1e-6, 100);
    try {
        if (format == "dxf") {
            for (const auto *key : {"columns", "delimiter", "header"})
                if (command.contains(key))
                    reject("INVALID_REQUEST", std::string(key) + " applies to point files only");
            parsed.set = parseDxfTerrain(bytes, {unit, chord});
        } else {
            if (!unit)
                reject("INVALID_REQUEST", "Point files have no units; choose " +
                                              terrainUnitNames().toStdString());
            if (command.contains("chordTolerance"))
                reject("INVALID_REQUEST", "chordTolerance applies to DXF sources only");
            TerrainPointOptions points;
            points.unit = *unit;
            if (command.contains("columns")) {
                const auto columns = command["columns"];
                if (columns.isString()) {
                    points.columns = terrainColumnPreset(columns.toString());
                    points.preset = columns.toString();
                    if (!points.columns)
                        reject("INVALID_REQUEST",
                               "Column presets are PNEZD, PENZD, ENZ and XYZ");
                } else if (columns.isArray() && !columns.toArray().isEmpty() &&
                           columns.toArray().size() <= 64) {
                    std::vector<TerrainColumn> list;
                    for (const auto &entry : columns.toArray()) {
                        const auto column = terrainColumn(entry.toString());
                        if (!column)
                            reject("INVALID_REQUEST",
                                   "Column meanings are point, easting, northing, elevation, "
                                   "description and ignore");
                        list.push_back(*column);
                    }
                    points.columns = std::move(list);
                } else
                    reject("INVALID_REQUEST",
                           "columns is a preset name or a list of 1 to 64 column meanings");
            }
            if (command.contains("delimiter")) {
                points.delimiter = terrainDelimiter(command["delimiter"].toString());
                if (!points.delimiter)
                    reject("INVALID_REQUEST",
                           "Delimiters are comma, tab, semicolon and whitespace");
            }
            if (command.contains("header")) {
                if (!command["header"].isBool())
                    reject("INVALID_REQUEST", "header is true or false");
                points.header = command["header"].toBool();
            }
            parsed.set = parseTerrainPoints(bytes, points);
        }
    } catch (const TerrainError &) {
        throw;
    } catch (const std::bad_alloc &) {
        throw;
    } catch (const std::exception &error) {
        const std::string message = error.what();
        reject(message.find("exceed") != std::string::npos ||
                       message.find("at most") != std::string::npos
                   ? "LIMIT_EXCEEDED"
                   : "INVALID_REQUEST",
               message);
    }
    if (command.contains("roles")) {
        if (!command["roles"].isObject() || command["roles"].toObject().size() > 1024)
            reject("INVALID_REQUEST", "roles maps at most 1,024 layers or codes to roles");
        const auto roles = command["roles"].toObject();
        for (auto it = roles.begin(); it != roles.end(); ++it) {
            const auto role = terrainImportRole(it.value().toString());
            if (!role || it.key().toUtf8().size() > 256)
                reject("INVALID_REQUEST", "Roles are auto, contour, breakline, boundary, hole, "
                                          "ground and ignore");
            parsed.options.roles[it.key()] = *role;
        }
    }
    if (command.contains("origin")) {
        if (!command["origin"].isObject())
            reject("INVALID_REQUEST", "origin is {easting, northing, elevation}");
        const auto origin = command["origin"].toObject();
        for (auto it = origin.begin(); it != origin.end(); ++it)
            if (it.key() != "easting" && it.key() != "northing" && it.key() != "elevation")
                reject("INVALID_REQUEST", "origin is {easting, northing, elevation}");
        parsed.options.origin = TerrainOriginMetres{
            coordinate(origin["easting"]), coordinate(origin["northing"]),
            origin.contains("elevation") ? coordinate(origin["elevation"]) : 0};
    }
    auto &triangulation = parsed.options.triangulation;
    if (command.contains("flatMode")) {
        const auto mode = command["flatMode"].toString();
        if (mode != "flip" && mode != "reportOnly")
            reject("INVALID_REQUEST", "flatMode is flip or reportOnly");
        triangulation.flatMode = mode == "flip" ? TerrainFlatMode::Flip : TerrainFlatMode::ReportOnly;
    }
    if (command.contains("steinerFill")) {
        if (!command["steinerFill"].isBool())
            reject("INVALID_REQUEST", "steinerFill is true or false");
        triangulation.steinerFill = command["steinerFill"].toBool();
    }
    if (command.contains("maxEdgeLength"))
        triangulation.maxEdgeLength = number("maxEdgeLength", 1e-3, 2e6);
    parsed.provenance.crs = boundedText(command, "crs", siteCrsBytes);
    parsed.provenance.sourceName = boundedText(command, "sourceName", siteSourceBytes);
    if (importing && command.contains("name")) {
        parsed.name = boundedText(command, "name", 1024);
        if (parsed.name.empty())
            reject("INVALID_REQUEST", "A terrain name is 1 to 1,024 bytes");
    }
    return parsed;
}

// ---- R095.c2: terrain.create_from_edges, terrain.set_parameters, terrain.regenerate -------
constexpr qsizetype terrainEdgeLimit = 100000; // edges per list and in all
TerrainEdgeRef edgeRef(const QJsonValue &value) {
    const auto object = value.toObject();
    if (!value.isObject() || object.size() != 2 || !object.contains("body") ||
        !object.contains("edge"))
        reject("INVALID_REQUEST", "An edge is {\"body\": ID, \"edge\": ID}");
    return {identity(object["body"]), identity(object["edge"])};
}
std::vector<TerrainEdgeRef> edgeList(const QJsonValue &value, const char *key) {
    if (!value.isArray() || value.toArray().size() > terrainEdgeLimit)
        reject("INVALID_REQUEST",
               std::string(key) + " is a list of at most 100,000 {body, edge} references");
    std::vector<TerrainEdgeRef> edges;
    for (const auto &entry : value.toArray())
        edges.push_back(edgeRef(entry));
    return edges;
}
QJsonArray idList(const std::vector<Id> &ids) {
    QJsonArray list;
    for (const auto id : ids)
        list.append(QString::number(id));
    return list;
}
TerrainTriangulationOptions edgeTriangulation(const QJsonObject &command) {
    TerrainTriangulationOptions triangulation;
    if (command.contains("flatMode")) {
        const auto mode = command["flatMode"].toString();
        if (mode != "flip" && mode != "reportOnly")
            reject("INVALID_REQUEST", "flatMode is flip or reportOnly");
        triangulation.flatMode =
            mode == "flip" ? TerrainFlatMode::Flip : TerrainFlatMode::ReportOnly;
    }
    if (command.contains("steinerFill")) {
        if (!command["steinerFill"].isBool())
            reject("INVALID_REQUEST", "steinerFill is true or false");
        triangulation.steinerFill = command["steinerFill"].toBool();
    }
    if (command.contains("maxEdgeLength")) {
        const auto value = command["maxEdgeLength"];
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 1e-3 ||
            value.toDouble() > 2e6)
            reject("INVALID_REQUEST", "maxEdgeLength is out of range");
        triangulation.maxEdgeLength = value.toDouble();
    }
    return triangulation;
}
QJsonObject createTerrainFromEdges(Document &doc, const QJsonObject &command,
                                   ChangeReport &changes) {
    onlyFields(command, {"command", "edges", "roles", "boundary", "holes", "name", "flatMode",
                         "steinerFill", "maxEdgeLength"});
    TerrainEdgeSelection selection;
    selection.edges = edgeList(command["edges"], "edges");
    if (command.contains("roles")) {
        const auto roles = command["roles"].toObject();
        if (!command["roles"].isObject())
            reject("INVALID_REQUEST", "roles is {contour?: edges, breakline?: edges}");
        for (auto it = roles.begin(); it != roles.end(); ++it)
            if (it.key() != "contour" && it.key() != "breakline")
                reject("INVALID_REQUEST", "roles is {contour?: edges, breakline?: edges}");
        if (roles.contains("contour"))
            selection.contours = edgeList(roles["contour"], "roles.contour");
        if (roles.contains("breakline"))
            selection.breaklines = edgeList(roles["breakline"], "roles.breakline");
    }
    if (command.contains("boundary"))
        selection.boundary = edgeList(command["boundary"], "boundary");
    if (command.contains("holes")) {
        if (!command["holes"].isArray() || command["holes"].toArray().size() > 10000)
            reject("INVALID_REQUEST", "holes is a list of at most 10,000 edge lists");
        for (const auto &hole : command["holes"].toArray())
            selection.holes.push_back(edgeList(hole, "holes"));
    }
    qsizetype total = qsizetype(selection.edges.size() + selection.contours.size() +
                                selection.breaklines.size() + selection.boundary.size());
    for (const auto &hole : selection.holes)
        total += qsizetype(hole.size());
    if (total > terrainEdgeLimit)
        reject("LIMIT_EXCEEDED", "A terrain is built from at most 100,000 edges");
    std::string name = "Terrain";
    if (command.contains("name")) {
        name = boundedText(command, "name", 1024);
        if (name.empty())
            reject("INVALID_REQUEST", "A terrain name is 1 to 1,024 bytes");
    }
    TerrainImportOptions options;
    options.roles = terrainEdgeRoles();
    options.origin = TerrainOriginMetres{0, 0, 0};
    options.triangulation = edgeTriangulation(command);
    options.otherTerrainTriangles = doc.materializedTotals().terrainTriangles;
    const auto set = terrainEdgeSources(doc, selection);
    // The terrain frame is the model frame: no site origin is used, set or required.
    auto result = runTerrainImport(set, options, std::nullopt);
    auto terrain = result.report["terrain"].toObject();
    terrain.remove("proposedOrigin");
    terrain.remove("origin");
    terrain["originSource"] = "model";
    terrain["conversion"] = "none: the terrain uses the model frame, in metres";
    terrain["outcome"] = terrain["outcome"] == "imported"              ? "created"
                         : terrain["outcome"] == "importedWithNotices" ? "createdWithNotices"
                                                                       : "refused";
    result.report["terrain"] = terrain;
    if (!result.ok) {
        auto message = result.message;
        if (message.starts_with("Terrain import"))
            message = "Terrain creation" + message.substr(14);
        throw TerrainImportError(result.code, message, result.report, "terrainReport");
    }
    auto created = createTerrain(doc, result.mesh, std::move(result.record), 0, std::move(name));
    changes = std::move(created.changes);
    return {{"command", "terrain.create_from_edges"},
            {"terrain", QString::number(created.terrain)},
            {"tiles", idList(created.tiles)},
            {"changed", true},
            {"terrainReport", result.report}};
}
// terrain.regenerate and terrain.set_parameters: one edit with `label`.
QJsonObject regenerateTerrainCommand(Document &doc, Id terrain,
                                     std::optional<TerrainParameters> parameters,
                                     const QString &command, std::string label,
                                     ChangeReport &changes) {
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    const auto record = *doc.bodies().at(terrain)->terrain;
    const auto chosen = parameters.value_or(record.parameters);
    // Tiles are rebuilt, so paint on the old tiles cannot follow the new mesh.
    TerrainReport notices;
    const auto paint = terrainTilePaint(doc, terrain);
    if (paint.faces)
        notices.add(TerrainReport::Severity::Notice, "face-paint-dropped", paint.faces,
                    "Per-face paint on the old tiles cannot follow a new mesh and was removed; "
                    "the terrain's own material is kept.");
    if (paint.tiles)
        notices.add(TerrainReport::Severity::Notice, "tile-paint-dropped", paint.tiles,
                    "Materials and colours of individual tiles were removed with the old tiles; "
                    "the terrain's own material is kept.");
    const auto total = doc.materializedTotals().terrainTriangles;
    const auto own = terrainTriangleCount(doc, terrain);
    auto result = runTerrainRegenerate(record, chosen, total > own ? total - own : 0, notices);
    if (!result.ok)
        throw TerrainImportError(result.code, result.message, result.report, "terrainReport");
    auto regenerated =
        regenerateTerrain(doc, terrain, result.mesh, std::move(result.record), std::move(label));
    changes = std::move(regenerated.changes);
    return {{"command", command},
            {"terrain", QString::number(terrain)},
            {"tiles", idList(regenerated.tiles)},
            {"changed", regenerated.changed},
            {"terrainReport", result.report}};
}
TerrainParameters chosenParameters(const Document &doc, Id terrain, const QJsonObject &command) {
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    auto parameters = doc.bodies().at(terrain)->terrain->parameters;
    if (command.contains("boundary")) {
        const auto mode = command["boundary"].toString();
        if (mode == "convexHull")
            parameters.boundary = TerrainBoundaryMode::ConvexHull;
        else if (mode == "maxEdgeLength")
            parameters.boundary = TerrainBoundaryMode::MaxEdgeLength;
        else if (mode == "boundary")
            parameters.boundary = TerrainBoundaryMode::Boundary;
        else
            reject("INVALID_REQUEST", "boundary is convexHull, maxEdgeLength or boundary");
    }
    if (command.contains("maxEdgeLength")) {
        const auto value = command["maxEdgeLength"];
        if (!value.isDouble() || !std::isfinite(value.toDouble()))
            reject("INVALID_REQUEST", "maxEdgeLength is 0 (not set) or 0.001 to 2,000,000 m");
        parameters.maxEdgeLength = value.toDouble();
    }
    for (const auto *key : {"flipFlatTriangles", "steinerFill"})
        if (command.contains(key)) {
            if (!command[key].isBool())
                reject("INVALID_REQUEST", std::string(key) + " is true or false");
            (std::string(key) == "steinerFill" ? parameters.steinerFill
                                               : parameters.flipFlatTriangles) =
                command[key].toBool();
        }
    validateTerrainParameters(parameters, doc.bodies().at(terrain)->terrain->sources);
    return parameters;
}
} // namespace
QJsonObject terrainDisplayJson(const TerrainDisplay &display) {
    return {{"contours", display.contours},
            {"contourInterval", display.contourInterval},
            {"shading", display.shading == TerrainShading::ElevationBands ? "elevationBands"
                                                                          : "material"}};
}
bool isTerrainCommand(const QString &name) {
    return name == "site.set" || name == "site.reanchor" || name == "terrain.explode" ||
           name == "terrain.import" || name == "terrain.create_from_edges" ||
           name == "terrain.set_parameters" || name == "terrain.regenerate" ||
           name == "terrain.set_display";
}
QJsonObject inspectTerrainSource(const Document &doc, const QByteArray &bytes,
                                 const QJsonObject &command) {
    const auto parsed = parseSource(bytes, command, false);
    const auto dryRun =
        runTerrainImport(parsed.set, parsed.options, doc.site(), parsed.provenance);
    return terrainSourceInspection(parsed.set, dryRun);
}
QJsonObject inspectTerrainCommand(const Document &doc, const QJsonObject &command) {
    return inspectTerrainSource(doc, sourceData(command), command);
}
TerrainSourceRequest parseTerrainSourceRequest(const QByteArray &bytes,
                                               const QJsonObject &command, bool importing) {
    return parseSource(bytes, command, importing);
}
QJsonObject importTerrainSource(Document &doc, const QByteArray &bytes,
                                const QJsonObject &command) {
    const auto parsed = parseSource(bytes, command, true);
    return applyTerrainImport(
        doc, runTerrainImport(parsed.set, parsed.options, doc.site(), parsed.provenance),
        parsed.name);
}
QJsonObject applyTerrainImport(Document &doc, TerrainImportResult result,
                               const std::string &name) {
    if (!result.ok)
        throw TerrainImportError(result.code, result.message, result.report);
    const bool established = result.establish.has_value();
    const auto created = importTerrain(doc, result.mesh, std::move(result.record),
                                       std::move(result.establish), name);
    QJsonArray tiles;
    for (const auto id : created.tiles)
        tiles.append(QString::number(id));
    return {{"command", "terrain.import"},
            {"terrain", QString::number(created.terrain)},
            {"tiles", tiles},
            {"siteEstablished", established},
            {"importReport", result.report}};
}
ChangeReport executeTerrainCommand(Document &doc, const QJsonObject &command,
                                   QJsonObject *operation) {
    const auto name = command["command"].toString();
    if (name == "terrain.import") {
        auto record = importTerrainSource(doc, sourceData(command), command);
        if (operation)
            *operation = std::move(record);
        return {};
    }
    if (name == "terrain.create_from_edges" || name == "terrain.set_parameters" ||
        name == "terrain.regenerate") {
        ChangeReport changes;
        QJsonObject record;
        if (name == "terrain.create_from_edges")
            record = createTerrainFromEdges(doc, command, changes);
        else if (name == "terrain.regenerate") {
            onlyFields(command, {"command", "terrain"});
            record = regenerateTerrainCommand(doc, identity(command["terrain"]), std::nullopt, name,
                                              "Regenerate terrain", changes);
        } else {
            onlyFields(command, {"command", "terrain", "boundary", "maxEdgeLength",
                                 "flipFlatTriangles", "steinerFill"});
            const auto terrain = identity(command["terrain"]);
            record = regenerateTerrainCommand(doc, terrain, chosenParameters(doc, terrain, command),
                                              name, "Terrain settings", changes);
        }
        if (operation)
            *operation = std::move(record);
        return changes;
    }
    if (name == "site.set") {
        onlyFields(command, {"command", "easting", "northing", "elevation", "crs", "source",
                             "sha256"});
        SiteOrigin site;
        site.easting = coordinate(command["easting"]);
        site.northing = coordinate(command["northing"]);
        site.elevation = command.contains("elevation") ? coordinate(command["elevation"]) : 0;
        site.crs = text(command, "crs");
        site.established = {text(command, "source"), text(command, "sha256")};
        setSiteOrigin(doc, std::move(site));
        return {};
    }
    if (name == "site.reanchor") {
        onlyFields(command, {"command", "easting", "northing", "elevation"});
        if (!doc.site())
            reject("NOT_FOUND", "The document has no site origin; set it first");
        return reanchorSite(doc, coordinate(command["easting"]), coordinate(command["northing"]),
                            command.contains("elevation") ? coordinate(command["elevation"])
                                                          : doc.site()->elevation);
    }
    if (name == "terrain.explode") {
        onlyFields(command, {"command", "body"});
        return explodeTerrain(doc, identity(command["body"]));
    }
    if (name == "terrain.set_display") {
        // R095.e2: display settings only; the mesh, its digest and "Changed since generated"
        // are untouched. Omitted fields keep their values.
        onlyFields(command, {"command", "terrain", "contours", "contourInterval", "shading"});
        const auto terrain = identity(command["terrain"]);
        if (!isTerrain(doc, terrain))
            reject("NOT_FOUND", "Terrain body does not exist");
        auto display = doc.bodies().at(terrain)->terrain->display;
        if (command.contains("contours")) {
            if (!command["contours"].isBool())
                reject("INVALID_REQUEST", "contours is true or false");
            display.contours = command["contours"].toBool();
        }
        if (command.contains("contourInterval")) {
            const auto value = command["contourInterval"];
            if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
                value.toDouble() < terrainContourIntervalMin ||
                value.toDouble() > terrainContourIntervalMax)
                reject("INVALID_REQUEST", "contourInterval is 0.01 to 1,000 m");
            display.contourInterval = value.toDouble();
        }
        if (command.contains("shading")) {
            const auto mode = command["shading"].toString();
            if (mode == "material")
                display.shading = TerrainShading::Material;
            else if (mode == "elevationBands")
                display.shading = TerrainShading::ElevationBands;
            else
                reject("INVALID_REQUEST", "shading is material or elevationBands");
        }
        const auto revision = doc.revision();
        auto changes = setTerrainDisplay(doc, terrain, display);
        if (operation)
            *operation = {{"command", name},
                          {"terrain", QString::number(terrain)},
                          {"changed", doc.revision() != revision},
                          {"display", terrainDisplayJson(display)}};
        return changes;
    }
    reject("UNSUPPORTED_CAPABILITY", "Unavailable terrain command");
}
namespace {
// R095.c terrain.import and terrain.inspect_source parameters.
QJsonObject sourceProperties(bool importing) {
    auto choice = [](QJsonArray values, const char *description) {
        return QJsonObject{{"type", "string"}, {"enum", values}, {"description", description}};
    };
    const QJsonObject coordinate{{"type", "number"},
                                 {"minimum", -siteCoordinateLimit},
                                 {"maximum", siteCoordinateLimit}};
    const QJsonObject role = choice({"auto", "contour", "breakline", "boundary", "hole", "ground",
                                     "ignore"},
                                    "Role of a DXF layer or point description code");
    const QJsonObject column =
        choice({"point", "easting", "northing", "elevation", "description", "ignore"},
               "Meaning of one point-file column");
    QJsonObject properties{
        {"data",
         QJsonObject{{"type", "string"},
                     {"maxLength", terrainDataCharacters},
                     {"description", "Canonical base64 of the source file (at most 16 MiB; "
                                     "a 64 KiB request carries about 47 KiB)"}}},
        {"format", choice({"dxf", "points"}, "DXF terrain mode or a CSV/XYZ/PNEZD point file")},
        {"units",
         choice({"mm", "cm", "m", "in", "ft", "usft", "header"},
                "Source unit; required for point files and unitless DXF; usft is the US survey "
                "foot (1200/3937 m); header follows $INSUNITS")},
        {"origin",
         QJsonObject{{"type", "object"},
                     {"description", "Explicit site origin in metres; default: the proposed "
                                     "origin, or the document's"},
                     {"properties", QJsonObject{{"easting", coordinate},
                                                {"northing", coordinate},
                                                {"elevation", coordinate}}},
                     {"required", QJsonArray{"easting", "northing"}},
                     {"additionalProperties", false}}},
        {"crs", QJsonObject{{"type", "string"},
                            {"maxLength", 64},
                            {"description", "Informational CRS label for a new site origin"}}},
        {"sourceName", QJsonObject{{"type", "string"},
                                   {"maxLength", 256},
                                   {"description", "Source file name for the provenance"}}},
        {"roles", QJsonObject{{"type", "object"},
                              {"maxProperties", 1024},
                              {"propertyNames", QJsonObject{{"maxLength", 256}}},
                              {"additionalProperties", role}}},
        {"columns",
         QJsonObject{{"anyOf",
                      QJsonArray{choice({"PNEZD", "PENZD", "ENZ", "XYZ"}, "Column preset"),
                                 QJsonObject{{"type", "array"},
                                             {"minItems", 1},
                                             {"maxItems", 64},
                                             {"items", column}}}}}},
        {"delimiter", choice({"comma", "tab", "semicolon", "whitespace"},
                             "Point-file delimiter; detected when omitted")},
        {"header", QJsonObject{{"type", "boolean"},
                               {"description", "Whether the first data line is a header; "
                                               "detected when omitted"}}},
        {"flatMode", choice({"flip", "reportOnly"}, "Flat-triangle handling; default flip")},
        {"steinerFill", QJsonObject{{"type", "boolean"}}},
        {"maxEdgeLength", QJsonObject{{"type", "number"}, {"minimum", 1e-3}, {"maximum", 2e6}}},
        {"chordTolerance",
         QJsonObject{{"type", "number"},
                     {"minimum", 1e-6},
                     {"maximum", 100},
                     {"description", "DXF arc chord tolerance in metres; default 0.05"}}}};
    if (importing)
        properties["name"] = QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}};
    return properties;
}
} // namespace
namespace {
// R095.c2 terrain.create_from_edges parameters.
QJsonObject edgeProperties() {
    const QJsonObject id{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    const QJsonObject edge{{"type", "object"},
                           {"properties", QJsonObject{{"body", id}, {"edge", id}}},
                           {"required", QJsonArray{"body", "edge"}},
                           {"additionalProperties", false}};
    auto edges = [&](const char *description) {
        return QJsonObject{{"type", "array"},
                           {"maxItems", int(terrainEdgeLimit)},
                           {"items", edge},
                           {"description", description}};
    };
    return {
        {"edges", edges("Model edges; constant Z makes a contour, varying Z a breakline")},
        {"roles", QJsonObject{{"type", "object"},
                              {"properties",
                               QJsonObject{{"contour", edges("Edges forced to be contours")},
                                           {"breakline", edges("Edges forced to be breaklines")}}},
                              {"additionalProperties", false}}},
        {"boundary", edges("Edges of one closed outer boundary")},
        {"holes", QJsonObject{{"type", "array"},
                              {"maxItems", 10000},
                              {"items", edges("Edges of one closed hole")}}},
        {"name", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
        {"flatMode", QJsonObject{{"type", "string"},
                                 {"enum", QJsonArray{"flip", "reportOnly"}},
                                 {"description", "Flat-triangle handling; default flip"}}},
        {"steinerFill", QJsonObject{{"type", "boolean"}}},
        {"maxEdgeLength", QJsonObject{{"type", "number"}, {"minimum", 1e-3}, {"maximum", 2e6}}}};
}
} // namespace
QJsonArray terrainCommandSpecs() {
    const QJsonObject terrainId{{"type", "string"},
                                {"pattern", "^[1-9][0-9]*$"},
                                {"maxLength", 20},
                                {"description", "Terrain body"}};
    const QJsonObject coordinateSchema{{"type", "number"},
                                       {"minimum", -siteCoordinateLimit},
                                       {"maximum", siteCoordinateLimit}};
    auto metres = [&](const char *description) {
        auto schema = coordinateSchema;
        schema["description"] = description;
        return schema;
    };
    auto spec = [](QString name, QString label, QJsonObject properties, QJsonArray required) {
        properties["command"] = QJsonObject{{"const", name}};
        required.append("command");
        return QJsonObject{
            {"name", name},
            {"label", label},
            {"category", "Site and terrain"},
            {"parameters", QJsonObject{{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                                       {"type", "object"},
                                       {"description", label},
                                       {"properties", properties},
                                       {"required", required},
                                       {"additionalProperties", false}}},
            {"validation", "Schema plus authoritative site, terrain, document and limit validation"},
            {"undo", "one batch history item"}};
    };
    return {
        spec("site.set", "Set site origin",
             {{"easting", metres("Projected easting of the model origin, metres")},
              {"northing", metres("Projected northing of the model origin, metres")},
              {"elevation", metres("Site elevation of model Z = 0, metres; default 0")},
              {"crs", QJsonObject{{"type", "string"},
                                  {"maxLength", 64},
                                  {"description", "Informational CRS label; never used to "
                                                  "transform"}}},
              {"source", QJsonObject{{"type", "string"}, {"maxLength", 256}}},
              {"sha256",
               QJsonObject{{"type", "string"}, {"pattern", "^[0-9a-f]{64}$"}, {"maxLength", 64}}}},
             {"easting", "northing"}),
        spec("site.reanchor", "Re-anchor site origin",
             {{"easting", metres("New projected easting of the model origin, metres")},
              {"northing", metres("New projected northing of the model origin, metres")},
              {"elevation", metres("New site elevation of model Z = 0; default unchanged")}},
             {"easting", "northing"}),
        spec("terrain.explode", "Explode terrain",
             {{"body", QJsonObject{{"type", "string"},
                                   {"pattern", "^[1-9][0-9]*$"},
                                   {"maxLength", 20},
                                   {"description", "Terrain body"}}}},
             {"body"}),
        spec("terrain.inspect_source", "Inspect terrain source", sourceProperties(false),
             {"data", "format"}),
        spec("terrain.import", "Import terrain", sourceProperties(true), {"data", "format"}),
        spec("terrain.create_from_edges", "Create terrain", edgeProperties(), {"edges"}),
        spec("terrain.set_parameters", "Terrain settings",
             {{"terrain", terrainId},
              {"boundary",
               QJsonObject{{"type", "string"},
                           {"enum", QJsonArray{"convexHull", "maxEdgeLength", "boundary"}},
                           {"description", "Boundary mode; boundary needs a stored boundary "
                                           "source, maxEdgeLength a positive length"}}},
              {"maxEdgeLength",
               QJsonObject{{"type", "number"},
                           {"minimum", 0},
                           {"maximum", 2e6},
                           {"description", "Metres; 0 clears it (at least 0.001 when set)"}}},
              {"flipFlatTriangles", QJsonObject{{"type", "boolean"}}},
              {"steinerFill", QJsonObject{{"type", "boolean"}}}},
             {"terrain"}),
        spec("terrain.regenerate", "Regenerate terrain", {{"terrain", terrainId}}, {"terrain"}),
        spec("terrain.set_display", "Terrain display",
             {{"terrain", terrainId},
              {"contours", QJsonObject{{"type", "boolean"},
                                       {"description", "Draw contour lines (derived display, "
                                                       "not geometry)"}}},
              {"contourInterval",
               QJsonObject{{"type", "number"},
                           {"minimum", terrainContourIntervalMin},
                           {"maximum", terrainContourIntervalMax},
                           {"description", "Metres between contour lines; every fifth is an "
                                           "index line"}}},
              {"shading", QJsonObject{{"type", "string"},
                                      {"enum", QJsonArray{"material", "elevationBands"}},
                                      {"description", "Shade by the tiles' material or by ten "
                                                      "elevation bands"}}}},
             {"terrain"})};
}
QJsonObject siteDescription(const Document &doc) {
    if (!doc.site())
        return {{"established", false},
                {"units", "m"},
                {"conversion", "Set a site origin to convert between model and site coordinates"}};
    const auto &site = *doc.site();
    return {{"established", true},
            {"units", "m"},
            {"origin", origin(site)},
            {"crs", QString::fromStdString(site.crs)},
            {"crsUse", "label only; coordinates are never reprojected"},
            {"provenance", QJsonObject{{"source", QString::fromStdString(site.established.source)},
                                       {"sha256", QString::fromStdString(site.established.sha256)}}},
            {"conversion", "site = model + origin; model = site - origin"}};
}
QJsonObject siteConversion(const Document &doc, const QString &to, const QJsonArray &points) {
    if (!doc.site())
        reject("NOT_FOUND", "The document has no site origin");
    if (to != "site" && to != "model")
        reject("INVALID_REQUEST", "Convert to \"site\" or \"model\"");
    if (points.isEmpty() || size_t(points.size()) > terrainPointBatch)
        reject("LIMIT_EXCEEDED", "Convert 1 to 200 points at a time");
    QJsonArray result;
    for (const auto &value : points) {
        const auto xyz = value.toArray();
        if (xyz.size() != 3)
            reject("INVALID_REQUEST", "Points require three coordinates");
        const Vec3 point{coordinate(xyz[0]), coordinate(xyz[1]), coordinate(xyz[2])};
        const auto converted =
            to == "site" ? modelToSite(*doc.site(), point) : siteToModel(*doc.site(), point);
        result.append(QJsonArray{converted.x, converted.y, converted.z});
    }
    return {{"to", to}, {"units", "m"}, {"points", result}};
}
QJsonObject terrainDescription(const Document &doc, Id terrain) {
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    const auto &body = *doc.bodies().at(terrain);
    const auto &record = *body.terrain;
    QJsonObject roles;
    size_t sourceVertices = 0;
    for (const auto &source : record.sources) {
        const auto key = QString::fromLatin1(roleName(source.role));
        roles[key] = roles[key].toInt() + 1;
        sourceVertices += source.points.size();
    }
    QJsonArray tiles;
    size_t triangles = 0;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    const auto world = doc.worldTransform(terrain);
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = *doc.bodies().at(id);
        tiles.append(QString::number(id));
        triangles += tile.surface.faces.size();
        for (const auto &[vertex, point] : tile.surface.vertices) {
            const auto z = world.point(point).z;
            low = std::min(low, z);
            high = std::max(high, z);
        }
    }
    const auto digest = terrainMeshDigest(doc, terrain);
    QJsonObject result{
        {"body", QString::number(terrain)},
        {"name", QString::fromStdString(body.name)},
        {"triangles", double(triangles)},
        {"tileCount", tiles.size()},
        {"tiles", tiles},
        {"generator", QString::fromStdString(record.generator)},
        {"parameters", encodeTerrainRecord(record)["parameters"]},
        {"provenance", encodeTerrainRecord(record)["provenance"]},
        {"display", terrainDisplayJson(record.display)},
        {"sources", QJsonObject{{"count", double(record.sources.size())},
                                {"vertices", double(sourceVertices)},
                                {"roles", roles}}},
        {"meshDigest", QString::fromStdString(record.meshDigest)},
        {"currentDigest", QString::fromStdString(digest)},
        {"changedSinceGenerated", digest != record.meshDigest},
        {"limits", QJsonObject{{"documentTriangles", double(TerrainLimits::triangles)},
                               {"tileTriangles", double(TerrainLimits::tileTriangles)}}},
        {"units", "m"}};
    if (triangles)
        result["elevationRange"] = QJsonArray{low, high};
    return result;
}
QJsonObject terrainElevationDescription(const Document &doc, Id terrain,
                                        const QJsonArray &points) {
    if (points.isEmpty() || size_t(points.size()) > terrainPointBatch)
        reject("LIMIT_EXCEEDED", "Ask for 1 to 200 points at a time");
    std::vector<std::array<double, 2>> xy;
    for (const auto &value : points) {
        const auto pair = value.toArray();
        if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble())
            reject("INVALID_REQUEST", "Points require two model coordinates");
        xy.push_back({pair[0].toDouble(), pair[1].toDouble()});
    }
    const auto found = terrainElevationAt(doc, terrain, xy);
    QJsonArray result;
    for (size_t i = 0; i < found.size(); ++i) {
        QJsonObject entry{{"point", QJsonArray{xy[i][0], xy[i][1]}}, {"found", found[i].found}};
        entry["elevation"] = found[i].found ? QJsonValue(found[i].elevation) : QJsonValue();
        if (found[i].found) {
            entry["tile"] = QString::number(found[i].tile);
            entry["face"] = QString::number(found[i].face);
        }
        result.append(entry);
    }
    return {{"body", QString::number(terrain)},
            {"space", "model"},
            {"units", "m"},
            {"points", result}};
}
} // namespace sketchy
