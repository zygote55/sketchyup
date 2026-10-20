#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sources.hpp"
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
ParsedSource parseSource(const QByteArray &bytes, const QJsonObject &command, bool importing,
                         TerrainProgress *progress = nullptr) {
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
            parsed.set = parseDxfTerrain(bytes, {unit, chord}, progress);
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
            parsed.set = parseTerrainPoints(bytes, points, progress);
        }
    } catch (const TerrainError &) {
        throw;
    } catch (const TerrainCancelled &) {
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
    auto request = parseTerrainEdgeRequest(doc, command);
    auto result = runTerrainEdgeRequest(request);
    if (!result.ok)
        throw TerrainImportError(result.code, result.message, result.report, "terrainReport");
    auto created =
        createTerrain(doc, result.mesh, std::move(result.record), 0, std::move(request.name));
    changes = std::move(created.changes);
    return {{"command", "terrain.create_from_edges"},
            {"terrain", QString::number(created.terrain)},
            {"tiles", idList(created.tiles)},
            {"changed", true},
            {"terrainReport", result.report}};
}
// terrain.regenerate, terrain.set_parameters, terrain.reset_edits and terrain.remove_edit:
// one edit with the request's label, replaying the (changed) edit list.
QJsonObject replayTerrainCommand(Document &doc, const QJsonObject &command,
                                 ChangeReport &changes) {
    const auto request = parseTerrainReplayRequest(doc, command);
    return applyTerrainReplay(doc, request, runTerrainReplay(request), &changes);
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

// ---- R096.c: terrain.edit_source, terrain.add_source and terrain.remove_source ------------
// Inputs are model coordinates in metres, converted once to the terrain frame (ADR 0169 §4);
// the report's locations are model coordinates again.
struct SourceEditCommand {
    Id terrain{};
    std::string kind, label;
    std::shared_ptr<TerrainSourceEditData> data;
    const TerrainSource *before{}; // in `state`
    std::shared_ptr<const TerrainSourceState> state;
    Transform world;
    TerrainPolylineCleaning cleaning;
    bool promoted{};
};
std::uint32_t sourceIdOf(const QJsonValue &value) {
    if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble()) ||
        value.toDouble() < 1 || value.toDouble() > 4294967295.0)
        reject("INVALID_REQUEST", "source is the integer ID of a terrain source (terrain.describe "
                                  "lists them)");
    return std::uint32_t(value.toDouble());
}
TerrainSourceRole roleOf(const QJsonValue &value, bool points) {
    const auto name = value.toString();
    if (name == "contour")
        return TerrainSourceRole::Contour;
    if (name == "breakline")
        return TerrainSourceRole::Breakline;
    if (points && name == "points")
        return TerrainSourceRole::Points;
    reject("INVALID_REQUEST", points ? "role is contour, breakline or points"
                                     : "role is contour or breakline");
}
// Model points [x, y] or [x, y, z] to the terrain frame; a missing z stays NaN for the caller.
std::vector<Vec3> localPoints(const QJsonValue &value, const Transform &toLocal) {
    if (!value.isArray() || value.toArray().isEmpty() ||
        std::size_t(value.toArray().size()) > terrainSourceEditPoints)
        reject("INVALID_REQUEST", "points is a list of 1 to 20,000 model points [x, y] or [x, y, z]");
    std::vector<Vec3> points;
    for (const auto &entry : value.toArray()) {
        const auto xyz = entry.toArray();
        if (!entry.isArray() || xyz.size() < 2 || xyz.size() > 3)
            reject("INVALID_REQUEST", "A point is [x, y] or [x, y, z] in model metres");
        const double x = coordinate(xyz[0]), y = coordinate(xyz[1]);
        if (xyz.size() == 3) {
            points.push_back(toLocal.point({x, y, coordinate(xyz[2])}));
        } else {
            auto p = toLocal.point({x, y, 0});
            p.z = std::numeric_limits<double>::quiet_NaN();
            points.push_back(p);
        }
    }
    return points;
}
bool closedPoints(const std::vector<Vec3> &points) {
    return points.size() >= 4 && points.front() == points.back();
}
// Cleaning and canonical order, as the importer does; a closed polyline keeps its repeated start.
std::vector<Vec3> cleanPoints(std::vector<Vec3> points, TerrainSourceRole role,
                              TerrainPolylineCleaning &counts) {
    if (role == TerrainSourceRole::Points)
        return points;
    const bool closed = closedPoints(points);
    std::vector<TerrainVec3> line;
    for (std::size_t i = 0; i < points.size() - (closed ? 1 : 0); ++i)
        line.push_back({points[i].x, points[i].y, points[i].z});
    cleanTerrainPolyline(line, closed, counts);
    const bool stillClosed = closed && line.size() >= 3;
    canonicalTerrainPolyline(line, stillClosed);
    std::vector<Vec3> result;
    for (const auto &p : line)
        result.push_back({p.x, p.y, p.z});
    if (stillClosed)
        result.push_back(result.front());
    if (result.size() < 2)
        reject("INVALID_REQUEST", "The polyline has no length after removing repeated points");
    return result;
}
// Each point's Z: the contour's one elevation, or the points' own (required).
void elevations(std::vector<Vec3> &points, TerrainSourceRole role, std::optional<double> contourZ) {
    if (role == TerrainSourceRole::Contour) {
        double z = contourZ.value_or(std::numeric_limits<double>::quiet_NaN());
        if (!contourZ)
            for (const auto &p : points)
                if (!std::isnan(p.z)) {
                    if (!std::isnan(z) && p.z != z)
                        reject("INVALID_REQUEST", "A contour has one elevation; give `elevation` "
                                                  "or points at one Z");
                    z = p.z;
                }
        if (std::isnan(z))
            reject("INVALID_REQUEST", "A contour needs its elevation: give `elevation` or [x, y, "
                                      "z] points");
        for (auto &p : points)
            p.z = z;
        return;
    }
    for (const auto &p : points)
        if (std::isnan(p.z))
            reject("INVALID_REQUEST", "Breakline and spot points need [x, y, z]");
}
std::array<double, 4> planBox(const std::vector<Vec3> &points) {
    std::array<double, 4> box{points.front().x, points.front().y, points.front().x,
                              points.front().y};
    for (const auto &p : points) {
        box[0] = std::min(box[0], p.x);
        box[1] = std::min(box[1], p.y);
        box[2] = std::max(box[2], p.x);
        box[3] = std::max(box[3], p.y);
    }
    return box;
}
SourceEditCommand parseSourceEdit(const Document &doc, const QJsonObject &command) {
    const auto name = command["command"].toString();
    SourceEditCommand parsed;
    if (name == "terrain.edit_source") {
        onlyFields(command, {"command", "terrain", "source", "points", "elevation", "role"});
        parsed.kind = terrainEditSourceKind;
        parsed.label = "Edit contour";
    } else if (name == "terrain.add_source") {
        onlyFields(command, {"command", "terrain", "role", "points", "edges", "elevation", "level",
                             "at"});
        parsed.kind = terrainAddSourceKind;
        parsed.label = "Add contour";
    } else {
        onlyFields(command, {"command", "terrain", "source"});
        parsed.kind = terrainRemoveSourceKind;
        parsed.label = "Remove contour";
    }
    parsed.terrain = identity(command["terrain"]);
    if (!isTerrain(doc, parsed.terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, parsed.terrain);
    requireLevelTerrain(doc, parsed.terrain);
    const auto &record = *doc.bodies().at(parsed.terrain)->terrain;
    parsed.state = terrainSourceState(record);
    parsed.world = doc.worldTransform(parsed.terrain);
    const auto toLocal = parsed.world.inverse();
    const auto zLocal = [&](double elevation) { return toLocal.point({0, 0, elevation}).z; };
    auto data = std::make_shared<TerrainSourceEditData>();
    if (parsed.kind != terrainAddSourceKind) {
        data->source = sourceIdOf(command["source"]);
        parsed.before = parsed.state->find(data->source);
        if (!parsed.before)
            reject("NOT_FOUND", "The terrain has no source " + std::to_string(data->source));
        if (parsed.before->role == TerrainSourceRole::Boundary ||
            parsed.before->role == TerrainSourceRole::Hole)
            reject("INVALID_REQUEST", "Boundary and hole sources define the outline; source edits "
                                      "change contours, breaklines and spot points");
        data->before = planBox(parsed.before->points);
    }
    if (parsed.kind == terrainEditSourceKind) {
        const auto &old = *parsed.before;
        if (!command.contains("points") && !command.contains("elevation") &&
            !command.contains("role"))
            reject("INVALID_REQUEST", "Give points, elevation or role to change");
        data->role = old.role;
        if (command.contains("role")) {
            if (old.role == TerrainSourceRole::Points)
                reject("INVALID_REQUEST", "Spot points keep their role");
            data->role = roleOf(command["role"], false);
        }
        std::optional<double> contourZ;
        if (command.contains("elevation")) {
            if (data->role == TerrainSourceRole::Points)
                reject("INVALID_REQUEST", "elevation applies to contours and breaklines");
            contourZ = zLocal(coordinate(command["elevation"]));
        } else if (old.role == TerrainSourceRole::Contour && !old.points.empty() &&
                   data->role == TerrainSourceRole::Contour)
            contourZ = old.points.front().z;
        if (command.contains("points")) {
            auto points = localPoints(command["points"], toLocal);
            elevations(points, data->role, contourZ);
            data->points = cleanPoints(std::move(points), data->role, parsed.cleaning);
        } else {
            data->points = old.points;
            if (contourZ && data->role == TerrainSourceRole::Contour)
                for (auto &p : data->points)
                    p.z = *contourZ;
            else if (command.contains("elevation"))
                for (auto &p : data->points)
                    p.z = *contourZ;
            else if (data->role == TerrainSourceRole::Contour)
                elevations(data->points, data->role, std::nullopt);
        }
        if (data->role == old.role && data->points == old.points)
            reject("INVALID_REQUEST", "The source would not change");
    } else if (parsed.kind == terrainAddSourceKind) {
        const bool hasPoints = command.contains("points"), hasEdges = command.contains("edges"),
                   hasLevel = command.contains("level");
        if (int(hasPoints) + int(hasEdges) + int(hasLevel) != 1)
            reject("INVALID_REQUEST", "Give exactly one of points, edges or level");
        data->role = command.contains("role") ? roleOf(command["role"], true)
                     : hasLevel               ? TerrainSourceRole::Contour
                                              : TerrainSourceRole::Points;
        if (!command.contains("role") && !hasLevel)
            reject("INVALID_REQUEST", "role is contour, breakline or points");
        if (command.contains("at") && !hasLevel)
            reject("INVALID_REQUEST", "at chooses a contour line with level");
        std::optional<double> contourZ;
        if (command.contains("elevation")) {
            if (data->role == TerrainSourceRole::Points || hasLevel)
                reject("INVALID_REQUEST", "elevation applies to contours and breaklines");
            contourZ = zLocal(coordinate(command["elevation"]));
        }
        std::vector<Vec3> points;
        if (hasLevel) {
            if (data->role != TerrainSourceRole::Contour)
                reject("INVALID_REQUEST", "A display contour is promoted as a contour");
            std::optional<std::array<double, 2>> near;
            if (command.contains("at")) {
                const auto at = command["at"].toArray();
                if (!command["at"].isArray() || at.size() != 2)
                    reject("INVALID_REQUEST", "at is a model point [x, y]");
                const auto local = toLocal.point({coordinate(at[0]), coordinate(at[1]), 0});
                near = std::array<double, 2>{local.x, local.y};
            }
            const auto mesh = terrainEditMeshCache().get(doc, parsed.terrain, true);
            points = terrainContourAtLevel(*mesh, zLocal(coordinate(command["level"])), near);
            parsed.promoted = true;
        } else if (hasPoints) {
            points = localPoints(command["points"], toLocal);
        } else {
            TerrainEdgeSelection selection;
            selection.edges = edgeList(command["edges"], "edges");
            if (selection.edges.empty())
                reject("INVALID_REQUEST", "Choose at least one edge");
            const auto set = terrainEdgeSources(doc, selection);
            if (data->role != TerrainSourceRole::Points && set.polylines.size() != 1)
                reject("INVALID_REQUEST",
                       "The edges form " + std::to_string(set.polylines.size()) +
                           " polylines; add one polyline at a time");
            for (const auto &line : set.polylines) {
                for (const auto &p : line.points) {
                    auto local = toLocal.point({p.x, p.y, p.z});
                    if (data->role == TerrainSourceRole::Contour && contourZ)
                        local.z = std::numeric_limits<double>::quiet_NaN();
                    points.push_back(local);
                }
                if (line.closed && data->role != TerrainSourceRole::Points && !points.empty())
                    points.push_back(points.front());
            }
            if (points.size() > terrainSourceEditPoints)
                reject("LIMIT_EXCEEDED", "A source edit has at most 20,000 points");
        }
        elevations(points, data->role, contourZ);
        data->points = cleanPoints(std::move(points), data->role, parsed.cleaning);
        data->source = terrainAddedSourceId(parsed.state->maxBaseId(), record.nextEditId);
    }
    try {
        validateTerrainSourceEditData(parsed.kind, *data);
    } catch (const std::invalid_argument &error) {
        reject("INVALID_REQUEST", error.what());
    }
    parsed.data = std::move(data);
    return parsed;
}
// The bounded report (the import's shape) with locations in model coordinates.
QJsonObject sourceEditReport(const TerrainReport &report, QJsonObject envelope) {
    return {{"apiVersion", 1},
            {"format", "terrain-edit"},
            {"units", "m"},
            {"space", "model"},
            {"terrain", report.json(std::move(envelope), TerrainReportBounds::reportBytes - 512)}};
}
void addProblems(TerrainReport &report, const std::vector<TerrainEditProblem> &problems,
                 const Transform &world) {
    for (const auto &problem : problems) {
        report.add(TerrainReport::Severity::Refusal, problem.kind, problem.count, problem.message);
        for (const auto &location : problem.locations) {
            const auto model = world.point({location.xy.x, location.xy.y, 0});
            report.locate(TerrainReport::Severity::Refusal, problem.kind, problem.message,
                          {location.sourceA ? "source " + std::to_string(location.sourceA)
                                            : std::string("edit"),
                           location.sourceB ? "change " + std::to_string(location.sourceB)
                                            : std::string(),
                           std::array<double, 2>{model.x, model.y}},
                          false);
        }
    }
}
QJsonObject editTerrainSource(Document &doc, const QJsonObject &command, ChangeReport &changes) {
    auto parsed = parseSourceEdit(doc, command);
    const auto &record = *doc.bodies().at(parsed.terrain)->terrain;
    TerrainReport report;
    const auto refused = [&](const TerrainEditRefused &error) {
        addProblems(report, error.problems(), parsed.world);
        throw TerrainImportError(error.code(), error.what(),
                                 sourceEditReport(report, {}), "terrainReport");
    };
    // The dry run: validation with a located report, and the resolved geometry (new vertices
    // that merge into existing ones are stored where the mesh holds them, so C1 holds exactly).
    TerrainSourceEditPlan plan;
    {
        const auto mesh = terrainEditMeshCache().get(doc, parsed.terrain, true);
        TerrainSource next;
        next.id = parsed.data->source;
        next.role = parsed.data->role;
        next.points = parsed.data->points;
        try {
            plan = planTerrainSourceEdit({*mesh, *parsed.state, record.parameters}, parsed.before,
                                         parsed.kind == terrainRemoveSourceKind ? nullptr : &next);
        } catch (const TerrainEditRefused &error) {
            refused(error);
        }
        if (parsed.kind != terrainRemoveSourceKind && plan.resolved != parsed.data->points) {
            std::uint64_t moved = 0;
            for (std::size_t i = 0; i < plan.resolved.size(); ++i)
                moved += plan.resolved[i] != parsed.data->points[i];
            parsed.data->points = plan.resolved;
            report.add(TerrainReport::Severity::Notice, "merged-duplicates", moved,
                       "Vertices within 1e-7 m of existing ones were merged into them.");
        }
    }
    TerrainEditApplied applied;
    try {
        applied = applyTerrainEdit(doc, parsed.terrain, parsed.kind, parsed.data, parsed.label);
    } catch (const TerrainEditRefused &error) {
        refused(error);
    }
    changes = std::move(applied.tiles.changes);
    if (parsed.cleaning.zeroLength)
        report.add(TerrainReport::Severity::Notice, "zero-length-segments",
                   parsed.cleaning.zeroLength,
                   "Zero-length segments (repeated vertices) were dropped.");
    if (parsed.cleaning.spikes)
        report.add(TerrainReport::Severity::Notice, "collinear-spikes", parsed.cleaning.spikes,
                   "Collinear spikes (vertices where a polyline doubles back on itself) were "
                   "removed.");
    if (applied.tiles.paintDropped.faces)
        report.add(TerrainReport::Severity::Notice, "face-paint-dropped",
                   applied.tiles.paintDropped.faces,
                   "Per-face paint on the rebuilt tiles cannot follow the new mesh and was "
                   "removed; the tiles' own material is kept.");
    if (plan.flatRemaining)
        report.add(TerrainReport::Severity::Notice, "flat-triangles", plan.flatRemaining,
                   "Flat triangles remain in the edited region (three vertices of one contour "
                   "elevation); add spot heights or a breakline to resolve them.");
    const QJsonObject envelope{
        {"source", double(parsed.data->source)},
        {"elevationOnly", plan.elevationOnly},
        {"cavity", QJsonObject{{"removed", double(plan.change.removed.size())},
                               {"added", double(plan.change.added.size())},
                               {"lifted", double(plan.change.lifted.size())},
                               {"verticesRemoved", double(plan.removedVertices)},
                               {"verticesInserted", double(plan.insertedVertices)},
                               {"flips", double(plan.flips)}}},
        {"edit", double(applied.edit)},
        {"kind", QString::fromStdString(parsed.kind)},
        {"promoted", parsed.promoted},
        {"points", double(parsed.data->points.size())},
        {"tilesReplaced", double(applied.tiles.removed.size())},
        {"tilesAdded", double(applied.tiles.added.size())}};
    return {{"command", command["command"]},
            {"terrain", QString::number(parsed.terrain)},
            {"source", double(parsed.data->source)},
            {"edit", double(applied.edit)},
            {"changed", true},
            {"tiles", QJsonObject{{"removed", idList(applied.tiles.removed)},
                                  {"added", idList(applied.tiles.added)}}},
            {"terrainReport", sourceEditReport(report, envelope)}};
}
} // namespace
TerrainReplayRequest parseTerrainReplayRequest(const Document &doc, const QJsonObject &command) {
    const auto name = command["command"].toString();
    TerrainReplayRequest request;
    request.command = name;
    if (name == "terrain.regenerate") {
        onlyFields(command, {"command", "terrain"});
        request.label = "Regenerate terrain";
    } else if (name == "terrain.set_parameters") {
        onlyFields(command, {"command", "terrain", "boundary", "maxEdgeLength",
                             "flipFlatTriangles", "steinerFill"});
        request.label = "Terrain settings";
    } else if (name == "terrain.reset_edits") {
        onlyFields(command, {"command", "terrain"});
        request.label = "Reset terrain edits";
    } else if (name == "terrain.remove_edit") {
        onlyFields(command, {"command", "terrain", "edit"});
        request.label = "Remove terrain change";
    } else
        reject("INVALID_REQUEST", "Not a terrain replay command");
    const auto terrain = identity(command["terrain"]);
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    request.terrain = terrain;
    request.current = *doc.bodies().at(terrain)->terrain;
    request.record = request.current;
    request.parameters = name == "terrain.set_parameters"
                             ? chosenParameters(doc, terrain, command)
                             : request.record.parameters;
    if (name == "terrain.reset_edits") {
        if (request.record.edits.empty())
            reject("INVALID_REQUEST", "The terrain has no edits to reset");
        request.record.edits.clear();
    } else if (name == "terrain.remove_edit") {
        const auto value = command["edit"];
        if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble()) ||
            value.toDouble() < 1 || value.toDouble() > 4294967295.0)
            reject("INVALID_REQUEST", "edit is the integer ID of a recorded terrain change");
        const auto id = std::uint32_t(value.toDouble());
        auto &edits = request.record.edits;
        const auto found = std::find_if(edits.begin(), edits.end(),
                                        [&](const TerrainEditPtr &edit) { return edit->id == id; });
        if (found == edits.end())
            reject("NOT_FOUND", "The terrain has no change " + std::to_string(id));
        edits.erase(found);
    }
    if (!request.record.edits.empty())
        requireLevelTerrain(doc, terrain);
    // Tiles are rebuilt, so paint on the old tiles cannot follow the new mesh.
    const auto paint = terrainTilePaint(doc, terrain);
    if (paint.faces)
        request.notices.add(TerrainReport::Severity::Notice, "face-paint-dropped", paint.faces,
                            "Per-face paint on the old tiles cannot follow a new mesh and was "
                            "removed; the terrain's own material is kept.");
    if (paint.tiles)
        request.notices.add(TerrainReport::Severity::Notice, "tile-paint-dropped", paint.tiles,
                            "Materials and colours of individual tiles were removed with the old "
                            "tiles; the terrain's own material is kept.");
    const auto total = doc.materializedTotals().terrainTriangles;
    const auto own = terrainTriangleCount(doc, terrain);
    request.otherTriangles = total > own ? total - own : 0;
    return request;
}
TerrainImportResult runTerrainReplay(const TerrainReplayRequest &request,
                                     TerrainProgress *progress) {
    return runTerrainRegenerate(request.record, request.parameters, request.otherTriangles,
                                request.notices, progress);
}
QJsonObject applyTerrainReplay(Document &doc, const TerrainReplayRequest &request,
                               TerrainImportResult result, ChangeReport *changes) {
    if (result.cancelled)
        throw TerrainImportError("CANCELLED", result.message, result.report, "terrainReport");
    if (!result.ok || !result.tileSet)
        throw TerrainImportError(result.code.empty() ? "INVALID_REQUEST" : result.code,
                                 result.message, result.report, "terrainReport");
    if (!isTerrain(doc, request.terrain) ||
        !(*doc.bodies().at(request.terrain)->terrain == request.current))
        reject("INVALID_OPERATION",
               "The terrain changed while it was being rebuilt; nothing was changed");
    auto regenerated = regenerateTerrain(doc, request.terrain, *result.tileSet,
                                         std::move(result.record), request.label);
    if (changes)
        *changes = std::move(regenerated.changes);
    return {{"command", request.command},
            {"terrain", QString::number(request.terrain)},
            {"tiles", idList(regenerated.tiles)},
            {"changed", regenerated.changed},
            {"terrainReport", result.report}};
}
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
           name == "terrain.set_display" || name == "terrain.reset_edits" ||
           name == "terrain.remove_edit" || name == "terrain.edit_source" ||
           name == "terrain.add_source" || name == "terrain.remove_source";
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
                                               const QJsonObject &command, bool importing,
                                               TerrainProgress *progress) {
    return parseSource(bytes, command, importing, progress);
}
QJsonObject importTerrainSource(Document &doc, const QByteArray &bytes, const QJsonObject &command,
                                TerrainProgress *progress) {
    const auto parsed = parseSource(bytes, command, true, progress);
    return applyTerrainImport(
        doc,
        runTerrainImport(parsed.set, parsed.options, doc.site(), parsed.provenance, progress),
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
TerrainEdgeRequest parseTerrainEdgeRequest(const Document &doc, const QJsonObject &command) {
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
    TerrainEdgeRequest request;
    if (command.contains("name")) {
        request.name = boundedText(command, "name", 1024);
        if (request.name.empty())
            reject("INVALID_REQUEST", "A terrain name is 1 to 1,024 bytes");
    }
    request.options.roles = terrainEdgeRoles();
    request.options.origin = TerrainOriginMetres{0, 0, 0};
    request.options.triangulation = edgeTriangulation(command);
    request.options.otherTerrainTriangles = doc.materializedTotals().terrainTriangles;
    request.set = terrainEdgeSources(doc, selection);
    return request;
}
TerrainImportResult runTerrainEdgeRequest(const TerrainEdgeRequest &request,
                                          TerrainProgress *progress) {
    // The terrain frame is the model frame: no site origin is used, set or required.
    auto result = runTerrainImport(request.set, request.options, std::nullopt, {}, progress);
    if (result.cancelled) {
        result.message = "Terrain creation cancelled; nothing was changed";
        return result;
    }
    auto terrain = result.report["terrain"].toObject();
    terrain.remove("proposedOrigin");
    terrain.remove("origin");
    terrain["originSource"] = "model";
    terrain["conversion"] = "none: the terrain uses the model frame, in metres";
    terrain["outcome"] = terrain["outcome"] == "imported"              ? "created"
                         : terrain["outcome"] == "importedWithNotices" ? "createdWithNotices"
                                                                       : "refused";
    result.report["terrain"] = terrain;
    if (!result.ok && result.message.starts_with("Terrain import"))
        result.message = "Terrain creation" + result.message.substr(14);
    return result;
}
QJsonObject terrainCreateFromEdgesCommand(const TerrainEdgeSelection &selection) {
    auto list = [](const std::vector<TerrainEdgeRef> &edges) {
        QJsonArray out;
        for (const auto &ref : edges)
            out.append(QJsonObject{{"body", QString::number(ref.body)},
                                   {"edge", QString::number(ref.edge)}});
        return out;
    };
    QJsonObject command{{"command", "terrain.create_from_edges"}, {"edges", list(selection.edges)}};
    QJsonObject roles;
    if (!selection.contours.empty())
        roles["contour"] = list(selection.contours);
    if (!selection.breaklines.empty())
        roles["breakline"] = list(selection.breaklines);
    if (!roles.isEmpty())
        command["roles"] = roles;
    if (!selection.boundary.empty())
        command["boundary"] = list(selection.boundary);
    if (!selection.holes.empty()) {
        QJsonArray holes;
        for (const auto &hole : selection.holes)
            holes.append(list(hole));
        command["holes"] = holes;
    }
    return command;
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
    if (name == "terrain.edit_source" || name == "terrain.add_source" ||
        name == "terrain.remove_source") {
        ChangeReport changes;
        auto record = editTerrainSource(doc, command, changes);
        if (operation)
            *operation = std::move(record);
        return changes;
    }
    if (name == "terrain.create_from_edges" || name == "terrain.set_parameters" ||
        name == "terrain.regenerate" || name == "terrain.reset_edits" ||
        name == "terrain.remove_edit") {
        ChangeReport changes;
        QJsonObject record;
        if (name == "terrain.create_from_edges")
            record = createTerrainFromEdges(doc, command, changes);
        else
            record = replayTerrainCommand(doc, command, changes);
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
    const QJsonObject sourceId{{"type", "integer"},
                               {"minimum", 1},
                               {"maximum", 4294967295.0},
                               {"description", "ID of a current terrain source (terrain.describe "
                                               "lists them under currentSources)"}};
    const QJsonObject modelPoints{
        {"type", "array"},
        {"minItems", 1},
        {"maxItems", int(terrainSourceEditPoints)},
        {"items", QJsonObject{{"type", "array"},
                              {"items", coordinateSchema},
                              {"minItems", 2},
                              {"maxItems", 3}}},
        {"description", "Model points [x, y, z] (or [x, y] for a contour with an elevation); a "
                        "closed polyline repeats its first point"}};
    const QJsonObject idSchema{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    const QJsonObject edgeSchema{{"type", "object"},
                                 {"properties", QJsonObject{{"body", idSchema}, {"edge", idSchema}}},
                                 {"required", QJsonArray{"body", "edge"}},
                                 {"additionalProperties", false}};
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
        spec("terrain.reset_edits", "Reset terrain edits", {{"terrain", terrainId}}, {"terrain"}),
        spec("terrain.remove_edit", "Remove terrain change",
             {{"terrain", terrainId},
              {"edit", QJsonObject{{"type", "integer"},
                                   {"minimum", 1},
                                   {"maximum", 4294967295.0},
                                   {"description", "ID of a recorded change (terrain.describe "
                                                   "lists them); the rest are replayed"}}}},
             {"terrain", "edit"}),
        spec("terrain.edit_source", "Edit contour",
             {{"terrain", terrainId},
              {"source", sourceId},
              {"points", modelPoints},
              {"elevation", metres("A contour's new elevation (model Z, metres)")},
              {"role", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"contour", "breakline"}},
                                   {"description", "Change a contour to a breakline or back"}}}},
             {"terrain", "source"}),
        spec("terrain.add_source", "Add contour",
             {{"terrain", terrainId},
              {"role", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"contour", "breakline", "points"}},
                                   {"description", "Required with points or edges; level adds a "
                                                   "contour"}}},
              {"points", modelPoints},
              {"edges", QJsonObject{{"type", "array"},
                                    {"maxItems", int(terrainEdgeLimit)},
                                    {"items", edgeSchema},
                                    {"description", "Model edges chained into one polyline (or "
                                                    "spot points), as terrain.create_from_edges"}}},
              {"elevation", metres("A contour's elevation when points have no Z (model Z)")},
              {"level", metres("Promote the display contour at this model Z to a source contour")},
              {"at", QJsonObject{{"type", "array"},
                                 {"items", coordinateSchema},
                                 {"minItems", 2},
                                 {"maxItems", 2},
                                 {"description", "Model [x, y] choosing the nearest contour line "
                                                 "when several lie at the level"}}}},
             {"terrain"}),
        spec("terrain.remove_source", "Remove contour", {{"terrain", terrainId}, {"source", sourceId}},
             {"terrain", "source"}),
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
QJsonObject terrainEditsJson(const TerrainRecord &record) {
    QJsonArray entries;
    for (const auto &edit : record.edits) {
        if (entries.size() >= 200)
            break;
        QJsonObject entry{{"id", double(edit->id)}, {"kind", QString::fromStdString(edit->kind)}};
        if (const auto *kind = findTerrainEditKind(edit->kind); kind && kind->bounds) {
            const auto box = kind->bounds(*edit);
            entry["bounds"] = QJsonArray{box[0], box[1], box[2], box[3]};
        }
        entries.append(entry);
    }
    return {{"count", double(record.edits.size())},
            {"bytes", double(terrainEditBytes(record.edits))},
            {"space", "terrain"},
            {"entries", entries},
            {"entriesOmitted", double(record.edits.size() - std::size_t(entries.size()))}};
}
namespace {
// R096.c: the current sources (base sources with the source edits applied) by role, with ids
// for the first 200 (terrain.edit_source and terrain.remove_source name them).
QJsonObject currentSourcesJson(const TerrainRecord &record) {
    const auto state = terrainSourceState(record);
    QJsonObject roles;
    QJsonArray listed;
    std::size_t vertices = 0;
    const auto sources = state->sources();
    for (const auto *source : sources) {
        const auto key = QString::fromLatin1(roleName(source->role));
        roles[key] = roles[key].toInt() + 1;
        vertices += source->points.size();
        if (listed.size() < 200)
            listed.append(QJsonObject{{"id", double(source->id)},
                                      {"role", key},
                                      {"reference", QString::fromStdString(source->reference)},
                                      {"points", double(source->points.size())}});
    }
    return {{"count", double(sources.size())},
            {"vertices", double(vertices)},
            {"roles", roles},
            {"space", "terrain"},
            {"entries", listed},
            {"entriesOmitted", double(sources.size() - std::size_t(listed.size()))}};
}
} // namespace
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
        {"edits", terrainEditsJson(record)},
        {"currentSources", currentSourcesJson(record)},
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
