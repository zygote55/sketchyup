#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
#include "core/document_limits.hpp"
#include "core/terrain_display.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/component_validation.hpp"
#include "core/groups.hpp"
#include "core/material_records.hpp"
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
    if (value.isArray() && std::size_t(value.toArray().size()) > terrainSourceEditPoints)
        reject("LIMIT_EXCEEDED", "A source edit has at most 20,000 points; split the change");
    if (!value.isArray() || value.toArray().isEmpty())
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
        // An entry stores the whole new geometry (about 24 bytes a point), at most 20,000 points.
        if (old.points.size() > terrainSourceEditPoints)
            reject("LIMIT_EXCEEDED",
                   "This point source has " + std::to_string(old.points.size()) +
                       " points; changes are limited to 20,000 points per source. Split it with "
                       "Explode terrain or re-import it in parts.");
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
// R096.d: an edit that would pass the document's terrain triangle limit is refused before the
// commit, located at the change, with the counts and the remedies.
void refuseOverTerrainLimit(const Document &doc, std::ptrdiff_t added, std::array<double, 2> at,
                            const std::string &what) {
    const auto total = doc.materializedTotals().terrainTriangles;
    if (added <= 0 || total + std::size_t(added) <= TerrainLimits::triangles)
        return;
    const std::string message =
        what + " refused; nothing was changed. The document's terrains have " +
        groupedCount(total) + " of " + groupedCount(TerrainLimits::triangles) +
        " triangles and this change needs " + groupedCount(std::size_t(added)) +
        " more. Make the change smaller, raise the maximum edge length in Terrain settings, or "
        "Explode terrain to keep the surface as ordinary geometry.";
    TerrainReport report;
    report.locate(TerrainReport::Severity::Refusal, "triangle-budget", message, {"edit", {}, at});
    throw TerrainImportError("LIMIT_EXCEEDED", message.substr(0, 256),
                             sourceEditReport(report, {}), "terrainReport");
}
QJsonObject editTerrainSource(Document &doc, const QJsonObject &command, ChangeReport &changes) {
    auto parsed = parseSourceEdit(doc, command);
    const auto &record = *doc.bodies().at(parsed.terrain)->terrain;
    TerrainReport report;
    const auto refused = [&](const TerrainEditRefused &error) {
        addProblems(report, error.problems(), parsed.world);
        std::string message = error.what();
        // A display contour crossing a breakline meets it at the crossing point, which the
        // crossing rule refuses (ADR 0169, R096.c notes).
        if (parsed.promoted &&
            std::any_of(error.problems().begin(), error.problems().end(),
                        [](const auto &problem) { return problem.kind == "crossing-constraints"; }))
            message = ("The contour line at this level crosses a breakline or another contour, "
                       "which a source contour may not touch; " +
                       message)
                          .substr(0, 256);
        throw TerrainImportError(error.code(), message, sourceEditReport(report, {}),
                                 "terrainReport");
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
            // The commit plans the resolved points, which can give another cavity: report that.
            next.points = parsed.data->points;
            try {
                plan = planTerrainSourceEdit({*mesh, *parsed.state, record.parameters},
                                             parsed.before, &next);
            } catch (const TerrainEditRefused &error) {
                refused(error);
            }
        }
    }
    {
        const auto &first = !plan.change.added.empty() ? plan.change.added.front()[0]
                            : !parsed.data->points.empty() ? parsed.data->points.front()
                                                           : Vec3{};
        const auto at = parsed.world.point(first);
        refuseOverTerrainLimit(doc,
                               std::ptrdiff_t(plan.change.added.size()) -
                                   std::ptrdiff_t(plan.change.removed.size()),
                               {at.x, at.y}, "Contour edit");
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

// ---- R096.b: terrain.sculpt -------------------------------------------------------------------
// A brush path or a region polygon in model [x, y], converted once to the terrain frame (ADR
// 0169 §4); radius and width in plan metres, the amount in model Z metres. The plan of a level
// terrain is scaled evenly (a similarity), so plan distances convert by one factor.
double finiteNumber(const QJsonObject &command, const char *key, const char *message) {
    const auto value = command[key];
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        reject("INVALID_REQUEST", message);
    return value.toDouble();
}
std::vector<std::array<double, 2>> localPlan(const QJsonValue &value, const Transform &toLocal,
                                             std::size_t limit, const char *message) {
    if (value.isArray() && std::size_t(value.toArray().size()) > limit)
        reject("LIMIT_EXCEEDED", message);
    if (!value.isArray() || value.toArray().isEmpty())
        reject("INVALID_REQUEST", message);
    std::vector<std::array<double, 2>> points;
    for (const auto &entry : value.toArray()) {
        const auto xy = entry.toArray();
        if (!entry.isArray() || xy.size() != 2)
            reject("INVALID_REQUEST", "A plan point is [x, y] in model metres");
        const auto local = toLocal.point({coordinate(xy[0]), coordinate(xy[1]), 0});
        points.push_back({local.x + 0.0, local.y + 0.0}); // -0.0 is stored as 0.0 (JSON)
    }
    return points;
}
QJsonObject sculptTerrain(Document &doc, const QJsonObject &command, ChangeReport &changes) {
    onlyFields(command, {"command", "terrain", "mode", "path", "radius", "falloff", "region",
                         "falloffWidth", "amount", "strength", "iterations"});
    const auto terrain = identity(command["terrain"]);
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    requireLevelTerrain(doc, terrain);
    const auto mode = command["mode"].toString();
    if (!command["mode"].isString() || (mode != "raise" && mode != "smooth"))
        reject("INVALID_REQUEST", "mode is raise or smooth");
    const bool raise = mode == "raise";
    const std::string kind = raise ? terrainSculptKind : terrainSmoothKind;
    const auto world = doc.worldTransform(terrain);
    const auto toLocal = world.inverse();
    // Radius, width and amount are model metres and are stored as they are, so the terrain's
    // frame must keep metres (a level terrain may be moved or turned, not scaled).
    const auto &m = world.m;
    const double sx = std::hypot(m[0], m[1]), sy = std::hypot(m[4], m[5]);
    if (std::abs(sx - 1) > 1e-12 || std::abs(sy - 1) > 1e-12 ||
        std::abs(m[0] * m[4] + m[1] * m[5]) > 1e-12 || std::abs(m[10] - 1) > 1e-12)
        reject("INVALID_OPERATION", "Sculpting needs a terrain at its original scale");
    const bool hasPath = command.contains("path"), hasRegion = command.contains("region");
    if (hasPath == hasRegion)
        reject("INVALID_REQUEST", "Give a brush path or a region, not both");
    auto data = std::make_shared<TerrainSculptData>();
    if (command.contains("falloff")) {
        const auto falloff = terrainFalloffOf(command["falloff"].toString().toStdString());
        if (!command["falloff"].isString() || !falloff)
            reject("INVALID_REQUEST", "falloff is smooth, linear or constant");
        data->falloff = *falloff;
    }
    if (hasPath) {
        if (command.contains("falloffWidth"))
            reject("INVALID_REQUEST", "falloffWidth applies to a region; a brush has a radius");
        if (!command.contains("radius"))
            reject("INVALID_REQUEST", "A brush needs its radius (0.01 to 1,000 m)");
        data->path = localPlan(command["path"], toLocal, terrainSculptPathPoints,
                               "path is a list of 1 to 256 model points [x, y]");
        data->radius = finiteNumber(command, "radius", "The brush radius is 0.01 to 1,000 m");
    } else {
        if (command.contains("radius"))
            reject("INVALID_REQUEST", "radius applies to a brush; a region has a falloffWidth");
        auto region = localPlan(command["region"], toLocal, terrainSculptRegionPoints + 1,
                                "region is a closed polygon of 3 to 20,000 model points [x, y]");
        // Repeated consecutive points (and a repeated first point closing the polygon) are
        // dropped; the closing edge is implied.
        std::vector<std::array<double, 2>> corners;
        for (const auto &p : region)
            if (corners.empty() || corners.back() != p)
                corners.push_back(p);
        while (corners.size() > 1 && corners.back() == corners.front())
            corners.pop_back();
        if (corners.size() > terrainSculptRegionPoints)
            reject("LIMIT_EXCEEDED", "region is a closed polygon of 3 to 20,000 model points [x, y]");
        data->region = std::move(corners);
        data->width = command.contains("falloffWidth")
                          ? finiteNumber(command, "falloffWidth",
                                         "The falloff width is 0 to 1,000 m")
                          : 0;
    }
    if (raise) {
        if (command.contains("strength") || command.contains("iterations"))
            reject("INVALID_REQUEST", "strength and iterations apply to mode smooth");
        if (!command.contains("amount"))
            reject("INVALID_REQUEST", "mode raise needs an amount in metres (negative lowers)");
        data->amount = finiteNumber(command, "amount", "The amount is a height in metres");
    } else {
        if (command.contains("amount"))
            reject("INVALID_REQUEST", "amount applies to mode raise");
        data->strength = command.contains("strength")
                             ? finiteNumber(command, "strength",
                                            "The smoothing strength is above 0 and at most 1")
                             : 0.5;
        data->iterations = 1;
        if (command.contains("iterations")) {
            const double iterations = finiteNumber(command, "iterations",
                                                   "Smoothing runs 1 to 8 iterations");
            if (iterations != std::floor(iterations) || iterations < 1 ||
                iterations > terrainSmoothIterationsMax)
                reject("INVALID_REQUEST", "Smoothing runs 1 to 8 iterations");
            data->iterations = int(iterations);
        }
    }
    // A region whose outline crosses one horizontal band too often is refused with its band
    // located (ADR 0169, R096.b notes): it would make every vertex's inside test long.
    if (!data->brush() && data->region.size() >= 3) {
        const auto bands = terrainSculptRegionBands(data->region);
        if (bands.edges > terrainSculptBandEdges) {
            double minX = data->region[0][0], maxX = minX;
            for (const auto &p : data->region) {
                minX = std::min(minX, p[0]);
                maxX = std::max(maxX, p[0]);
            }
            const auto at = world.point(
                {(minX + maxX) / 2, bands.low + (double(bands.busiest) + 0.5) * bands.height, 0});
            const std::string message =
                "The region's outline has " + std::to_string(bands.edges) +
                " edges in one horizontal band (at most 512); split it into simpler regions";
            TerrainReport report;
            report.locate(TerrainReport::Severity::Refusal, "limit-exceeded", message,
                          {"region", {}, std::array<double, 2>{at.x, at.y}});
            throw TerrainImportError("LIMIT_EXCEEDED", message, sourceEditReport(report, {}),
                                     "terrainReport");
        }
    }
    try {
        validateTerrainSculptData(kind, *data);
    } catch (const std::invalid_argument &error) {
        reject("INVALID_REQUEST", error.what());
    }
    // The dry run: refusals with their code and a located report, and the counts reported.
    const auto &record = *doc.bodies().at(terrain)->terrain;
    TerrainSculptPlan plan;
    {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto state = terrainSourceState(record);
        const TerrainEdit entry{record.nextEditId, kind, data};
        plan = planTerrainSculpt({*mesh, *state, record.parameters}, entry);
    }
    // The pieces of split lines and their ids are resolved geometry: recorded in the entry, so a
    // piece keeps its id on replay while its line splits the same way.
    data->pieces = plan.clip.resolved;
    TerrainReport report;
    if (!plan.refusal.empty()) {
        const auto &first = data->brush() ? data->path.front() : data->region.front();
        const auto at = world.point({first[0], first[1], 0});
        const auto problem = plan.refusalCode == "LIMIT_EXCEEDED" ? "limit-exceeded" : "nothing-moved";
        report.locate(TerrainReport::Severity::Refusal, problem, plan.refusal,
                      {data->brush() ? "brush" : "region", {}, std::array<double, 2>{at.x, at.y}});
        throw TerrainImportError(plan.refusalCode, plan.refusal, sourceEditReport(report, {}),
                                 "terrainReport");
    }
    const auto applied =
        applyTerrainEdit(doc, terrain, kind, data, raise ? "Sculpt terrain" : "Smooth terrain");
    changes = applied.tiles.changes;
    if (plan.clip.vertices)
        report.add(TerrainReport::Severity::Notice, "sources-clipped", plan.clip.vertices,
                   "Source contour, breakline and spot-point vertices under the change are edited "
                   "ground now and were clipped from the current sources.");
    if (plan.protectedVertices)
        report.add(TerrainReport::Severity::Notice, "pad-protected", plan.protectedVertices,
                   "Vertices on building pads were not moved.");
    if (applied.tiles.paintDropped.faces)
        report.add(TerrainReport::Severity::Notice, "face-paint-dropped",
                   applied.tiles.paintDropped.faces,
                   "Per-face paint on the rebuilt tiles cannot follow the new mesh and was "
                   "removed; the tiles' own material is kept.");
    const QJsonObject envelope{
        {"edit", double(applied.edit)},
        {"kind", QString::fromStdString(kind)},
        {"mode", mode},
        {"scope", data->brush() ? "brush" : "region"},
        {"verticesMoved", double(plan.change.lifted.size())},
        {"verticesInfluenced", double(plan.influenced)},
        {"verticesProtected", double(plan.protectedVertices)},
        {"tilesTouched", double(applied.tiles.removed.size())},
        {"tilesAdded", double(applied.tiles.added.size())},
        {"sourcesClipped", QJsonObject{{"sources", double(plan.clip.sources.size())},
                                       {"vertices", double(plan.clip.vertices)},
                                       {"pieces", double(plan.clip.pieces)},
                                       {"removed", double(plan.clip.removed)}}}};
    return {{"command", "terrain.sculpt"},
            {"terrain", QString::number(terrain)},
            {"edit", double(applied.edit)},
            {"mode", mode},
            {"changed", true},
            {"verticesMoved", double(plan.change.lifted.size())},
            {"tiles", QJsonObject{{"removed", idList(applied.tiles.removed)},
                                  {"added", idList(applied.tiles.added)}}},
            {"terrainReport", sourceEditReport(report, envelope)}};
}

// ---- R096.d: terrain.stamp_pad ----------------------------------------------------------------
// The footprint in model coordinates: a horizontal face, the lowest horizontal face of a group or
// component (or of a geometry body), a closed edge loop, or an explicit polygon. Its plan (the
// terrain frame's x and y) is cleaned and made counter-clockwise from its smallest point; its
// model Z (when every point has one, within 1e-6 m) is the default pad elevation.
struct Footprint {
    std::vector<std::array<double, 2>> plan; // terrain frame
    std::optional<double> z;                 // model Z
    Id body{}, face{};                       // the source, for Restamp
};
constexpr double horizontalTolerance = 1e-6;
// A face's outer loop in model coordinates, or empty when it has holes or is not horizontal.
std::vector<Vec3> horizontalFace(const Document &doc, const Body &body, const Face &face) {
    if (face.loops.size() != 1 || face.loops.front().size() < 3)
        return {};
    const auto world = doc.worldTransform(body.id);
    std::vector<Vec3> points;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (const auto vertex : face.loops.front()) {
        const auto found = body.surface.vertices.find(vertex);
        if (found == body.surface.vertices.end())
            return {};
        const auto p = world.point(found->second);
        low = std::min(low, p.z);
        high = std::max(high, p.z);
        points.push_back(p);
    }
    if (high - low > horizontalTolerance)
        return {};
    return points;
}
bool descendsFrom(const Document &doc, Id body, Id ancestor) {
    for (Id at = body; at;) {
        if (at == ancestor)
            return true;
        const auto found = doc.bodies().find(at);
        if (found == doc.bodies().end())
            return false;
        at = found->second->parent;
    }
    return false;
}
Footprint footprintOf(const Document &doc, const QJsonValue &value, const Transform &toLocal) {
    if (!value.isObject())
        reject("INVALID_REQUEST", "footprint is {body, face}, {body}, {edges} or {polygon}");
    const auto object = value.toObject();
    Footprint result;
    std::vector<Vec3> model;
    bool hasZ = true;
    auto bodyOf = [&](Id id) -> const Body & {
        const auto found = doc.bodies().find(id);
        if (found == doc.bodies().end())
            reject("NOT_FOUND", "The footprint body does not exist");
        if (found->second->terrainTile || found->second->kind == BodyKind::Terrain)
            reject("UNSUPPORTED_TARGET", "A terrain cannot be a pad footprint");
        return *found->second;
    };
    if (object.contains("polygon")) {
        if (object.size() != 1)
            reject("INVALID_REQUEST", "footprint {polygon} has no other fields");
        const auto list = object["polygon"].toArray();
        if (!object["polygon"].isArray() || list.size() < 3)
            reject("INVALID_REQUEST", "polygon is a list of at least 3 model points [x, y] or [x, y, z]");
        if (std::size_t(list.size()) > terrainStampFootprintPoints + 1)
            reject("LIMIT_EXCEEDED", "A pad footprint has at most 20,000 points");
        for (const auto &entry : list) {
            const auto xyz = entry.toArray();
            if (!entry.isArray() || xyz.size() < 2 || xyz.size() > 3)
                reject("INVALID_REQUEST", "A footprint point is [x, y] or [x, y, z] in model metres");
            hasZ = hasZ && xyz.size() == 3;
            model.push_back({coordinate(xyz[0]), coordinate(xyz[1]),
                             xyz.size() == 3 ? coordinate(xyz[2]) : 0.0});
        }
    } else if (object.contains("edges")) {
        if (object.size() != 1)
            reject("INVALID_REQUEST", "footprint {edges} has no other fields");
        TerrainEdgeSelection selection;
        selection.edges = edgeList(object["edges"], "edges");
        if (selection.edges.empty())
            reject("INVALID_REQUEST", "Choose the edges of one closed loop");
        const auto set = terrainEdgeSources(doc, selection);
        if (set.polylines.size() != 1 || !set.polylines.front().closed)
            reject("INVALID_REQUEST", "The footprint edges must form one closed loop");
        for (const auto &p : set.polylines.front().points)
            model.push_back({p.x, p.y, p.z});
        if (model.size() > terrainStampFootprintPoints + 1)
            reject("LIMIT_EXCEEDED", "A pad footprint has at most 20,000 points");
    } else if (object.contains("body")) {
        if (object.size() > 2 || (object.size() == 2 && !object.contains("face")))
            reject("INVALID_REQUEST", "footprint is {body, face} or {body}");
        const auto &body = bodyOf(identity(object["body"]));
        result.body = body.id;
        if (object.contains("face")) {
            result.face = identity(object["face"]);
            const auto face = body.surface.faces.find(result.face);
            if (face == body.surface.faces.end())
                reject("NOT_FOUND", "The footprint face does not exist");
            model = horizontalFace(doc, body, face->second);
            if (model.empty())
                reject("INVALID_REQUEST",
                       "The footprint face must be horizontal and have no holes");
        } else {
            // The lowest horizontal face (without holes) of the body or of the geometry inside the
            // group or component; ties go to the smaller body and face ID.
            double best = std::numeric_limits<double>::infinity();
            for (const auto &[id, candidate] : doc.bodies()) {
                if (candidate->kind != BodyKind::Geometry || candidate->terrainTile ||
                    !descendsFrom(doc, id, body.id))
                    continue;
                for (const auto &[faceId, face] : candidate->surface.faces) {
                    auto points = horizontalFace(doc, *candidate, face);
                    if (points.empty() || !(points.front().z < best))
                        continue;
                    best = points.front().z;
                    model = std::move(points);
                }
            }
            if (model.empty())
                reject("INVALID_REQUEST",
                       "The footprint body has no horizontal face without holes to stamp");
        }
    } else
        reject("INVALID_REQUEST", "footprint is {body, face}, {body}, {edges} or {polygon}");
    // Plan points in the terrain frame, cleaned and canonical (counter-clockwise from the
    // smallest), with the closing repeat dropped.
    std::vector<TerrainVec3> line;
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (const auto &p : model) {
        const auto local = toLocal.point(p);
        line.push_back({local.x + 0.0, local.y + 0.0, 0});
        low = std::min(low, p.z);
        high = std::max(high, p.z);
    }
    while (line.size() > 1 && std::abs(line.back().x - line.front().x) <= 1e-7 &&
           std::abs(line.back().y - line.front().y) <= 1e-7)
        line.pop_back();
    TerrainPolylineCleaning cleaning;
    cleanTerrainPolyline(line, true, cleaning);
    if (line.size() < 3)
        reject("INVALID_REQUEST", "The footprint encloses no area after removing repeated points");
    canonicalTerrainPolyline(line, true);
    if (line.size() > terrainStampFootprintPoints)
        reject("LIMIT_EXCEEDED", "A pad footprint has at most 20,000 points");
    for (const auto &p : line)
        result.plan.push_back({p.x, p.y});
    if (hasZ && high - low <= horizontalTolerance)
        result.z = model.front().z;
    return result;
}
// A footprint whose outline crosses one horizontal band too often is refused with the band
// located, as sculpt regions are (ADR 0169, R096.b notes).
void checkFootprintBands(const std::vector<std::array<double, 2>> &plan, const Transform &world) {
    const auto bands = terrainSculptRegionBands(plan);
    if (bands.edges <= terrainSculptBandEdges)
        return;
    double minX = plan[0][0], maxX = minX;
    for (const auto &p : plan) {
        minX = std::min(minX, p[0]);
        maxX = std::max(maxX, p[0]);
    }
    const auto at =
        world.point({(minX + maxX) / 2, bands.low + (double(bands.busiest) + 0.5) * bands.height, 0});
    const std::string message = "The footprint's outline has " + std::to_string(bands.edges) +
                                " edges in one horizontal band (at most 512); simplify it";
    TerrainReport report;
    report.locate(TerrainReport::Severity::Refusal, "limit-exceeded", message,
                  {"footprint", {}, std::array<double, 2>{at.x, at.y}});
    throw TerrainImportError("LIMIT_EXCEEDED", message, sourceEditReport(report, {}),
                             "terrainReport");
}
// Stamps and sculpts store plan metres and slopes as given, so the terrain's frame must keep
// metres (a level terrain may be moved or turned, not scaled).
void requireUnscaledTerrain(const Transform &world, const char *what) {
    const auto &m = world.m;
    const double sx = std::hypot(m[0], m[1]), sy = std::hypot(m[4], m[5]);
    if (std::abs(sx - 1) > 1e-12 || std::abs(sy - 1) > 1e-12 ||
        std::abs(m[0] * m[4] + m[1] * m[5]) > 1e-12 || std::abs(m[10] - 1) > 1e-12)
        reject("INVALID_OPERATION", what);
}
// The grading fields of stamp_pad and update_edit into `data` (mode switches reset the other
// mode's fields; omitted fields keep `data`'s values, or the defaults when the mode changes).
void gradingFields(const QJsonObject &command, TerrainStampData &data, bool update) {
    if (command.contains("mode")) {
        const auto mode = command["mode"].toString();
        if (!command["mode"].isString() || (mode != "daylight" && mode != "width"))
            reject("INVALID_REQUEST", "mode is daylight or width");
        const auto next = mode == "width" ? TerrainStampMode::Width : TerrainStampMode::Daylight;
        if (next != data.mode || !update) {
            data.mode = next;
            data.cutSlope = data.fillSlope = data.maxDistance = data.width = 0;
        }
    }
    if (data.mode == TerrainStampMode::Daylight) {
        if (command.contains("width"))
            reject("INVALID_REQUEST", "width applies to mode width; daylight has slopes");
        if (data.cutSlope == 0)
            data.cutSlope = 0.5;
        if (data.fillSlope == 0)
            data.fillSlope = 0.5;
        if (data.maxDistance == 0)
            data.maxDistance = 100;
        if (command.contains("cutSlope"))
            data.cutSlope = finiteNumber(command, "cutSlope", "cutSlope is 0.01 to 100 (rise over run)");
        if (command.contains("fillSlope"))
            data.fillSlope =
                finiteNumber(command, "fillSlope", "fillSlope is 0.01 to 100 (rise over run)");
        if (command.contains("maxDistance"))
            data.maxDistance =
                finiteNumber(command, "maxDistance", "maxDistance is above 0 and at most 1,000 m");
    } else {
        for (const char *key : {"cutSlope", "fillSlope", "maxDistance"})
            if (command.contains(key))
                reject("INVALID_REQUEST", "Slopes and maxDistance apply to mode daylight");
        if (command.contains("width"))
            data.width = finiteNumber(command, "width", "width is 0.01 to 1,000 m");
        if (data.width == 0)
            reject("INVALID_REQUEST", "mode width needs a width in metres (0.01 to 1,000)");
    }
}
void validateStamp(const TerrainStampData &data) {
    try {
        validateTerrainStampData(data);
    } catch (const std::invalid_argument &error) {
        reject("INVALID_REQUEST", error.what());
    }
}
QJsonArray modelBox(const Transform &world, const std::array<double, 4> &box) {
    double minX = std::numeric_limits<double>::infinity(), minY = minX, maxX = -minX, maxY = -minX;
    for (const double x : {box[0], box[2]})
        for (const double y : {box[1], box[3]}) {
            const auto p = world.point({x, y, 0});
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }
    return {minX, minY, maxX, maxY};
}
// A pad as terrain.describe and terrain.stamp_pad report it, in model coordinates: its
// elevation, the plan boxes of its outline and of its whole extent (to the daylight line), and its
// rays.
QJsonObject padJson(const Transform &world, std::uint32_t edit, const TerrainStampData &data) {
    std::array<double, 4> outline{std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity()};
    for (const auto &p : data.resolved() ? data.outline : data.footprint) {
        outline[0] = std::min(outline[0], p[0]);
        outline[1] = std::min(outline[1], p[1]);
        outline[2] = std::max(outline[2], p[0]);
        outline[3] = std::max(outline[3], p[1]);
    }
    QJsonObject pad{{"edit", double(edit)},
                    {"elevation", world.point({0, 0, data.elevation}).z},
                    {"mode", terrainStampModeName(data.mode)},
                    {"outline", modelBox(world, outline)},
                    {"applied", data.resolved()}};
    if (data.mode == TerrainStampMode::Daylight) {
        pad["cutSlope"] = data.cutSlope;
        pad["fillSlope"] = data.fillSlope;
        pad["maxDistance"] = data.maxDistance;
    } else
        pad["width"] = data.width;
    if (data.resolved()) {
        pad["extent"] = modelBox(world, terrainStampBounds(data));
        pad["rays"] = double(data.rays.size());
        pad["raysDropped"] = double(data.dropped);
    }
    if (data.sourceBody)
        pad["sourceBody"] = QString::number(data.sourceBody);
    return pad;
}
// The stamp's dry run (refusals with a located report in model coordinates), then ONE labelled
// "Stamp pad" edit, and the bounded report.
QJsonObject applyStamp(Document &doc, Id terrain, std::shared_ptr<TerrainStampData> data,
                       const QString &name, ChangeReport &changes) {
    const auto world = doc.worldTransform(terrain);
    TerrainReport report;
    const auto refused = [&](const TerrainEditRefused &error) {
        addProblems(report, error.problems(), world);
        throw TerrainImportError(error.code(), error.what(), sourceEditReport(report, {}),
                                 "terrainReport");
    };
    const auto &record = *doc.bodies().at(terrain)->terrain;
    TerrainStampPlan plan;
    try {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto state = terrainSourceState(record);
        const TerrainEdit entry{record.nextEditId, terrainStampKind, data};
        plan = planTerrainStamp({*mesh, *state, record.parameters}, entry);
    } catch (const TerrainEditRefused &error) {
        refused(error);
    }
    {
        const auto &p = data->footprint.front();
        const auto at = world.point({p[0], p[1], 0});
        refuseOverTerrainLimit(doc,
                               std::ptrdiff_t(plan.addedTriangles) -
                                   std::ptrdiff_t(plan.cavityTriangles),
                               {at.x, at.y}, "Stamp pad");
    }
    // The dry run is the kind's own outcome for this entry on the same mesh and sources, so the
    // commit uses it rather than planning again.
    TerrainEditApplied applied;
    try {
        const TerrainEditOutcome outcome{std::move(plan.change), {}, plan.resolved};
        applied = applyTerrainEdit(doc, terrain, terrainStampKind, plan.resolved, "Stamp pad",
                                   &outcome);
    } catch (const TerrainEditRefused &error) {
        refused(error);
    }
    changes = std::move(applied.tiles.changes);
    const auto &stored =
        dynamic_cast<const TerrainStampData &>(*doc.bodies().at(terrain)->terrain->edits.back()->data);
    if (plan.clip.vertices || plan.clip.pieces || plan.clip.removed)
        report.add(TerrainReport::Severity::Notice, "sources-clipped", plan.clip.vertices,
                   "Source contour, breakline and spot-point vertices under the pad and its "
                   "slopes are graded ground now and were clipped from the current sources.");
    if (plan.dropped) {
        const auto at = world.point({plan.droppedAt.x, plan.droppedAt.y, 0});
        report.locate(TerrainReport::Severity::Notice, "daylight-repaired",
                      "Grading rays that crossed others (tight or re-entrant corners) were "
                      "dropped; the daylight line keeps its outer boundary.",
                      {"pad", {}, std::array<double, 2>{at.x, at.y},
                       std::to_string(plan.dropped) + " rays dropped"},
                      false);
        report.add(TerrainReport::Severity::Notice, "daylight-repaired", plan.dropped,
                   "Grading rays that crossed others (tight or re-entrant corners) were "
                   "dropped; the daylight line keeps its outer boundary.");
    }
    if (applied.tiles.paintDropped.faces)
        report.add(TerrainReport::Severity::Notice, "face-paint-dropped",
                   applied.tiles.paintDropped.faces,
                   "Per-face paint on the rebuilt tiles cannot follow the new mesh and was "
                   "removed; the tiles' own material is kept.");
    if (plan.flatRemaining)
        report.add(TerrainReport::Severity::Notice, "flat-triangles", plan.flatRemaining,
                   "Flat triangles remain around the pad (three vertices of one contour "
                   "elevation); add spot heights or a breakline to resolve them.");
    const double elevation = world.point({0, 0, stored.elevation}).z;
    const QJsonObject envelope{
        {"edit", double(applied.edit)},
        {"mode", terrainStampModeName(stored.mode)},
        {"elevation", elevation},
        {"rays", QJsonObject{{"count", double(stored.rays.size())},
                             {"cut", double(plan.cut)},
                             {"fill", double(plan.fill)},
                             {"touching", double(plan.touching)},
                             {"dropped", double(plan.dropped)}}},
        {"outlinePoints", double(stored.outline.size())},
        {"daylightPoints", double(terrainStampRing(stored).size())},
        {"cavity", QJsonObject{{"removed", double(plan.cavityTriangles)},
                               {"added", double(plan.addedTriangles)},
                               {"verticesRemoved", double(plan.removedVertices)}}},
        {"tilesReplaced", double(applied.tiles.removed.size())},
        {"tilesAdded", double(applied.tiles.added.size())},
        {"sourcesClipped", QJsonObject{{"sources", double(plan.clip.sources.size())},
                                       {"vertices", double(plan.clip.vertices)},
                                       {"pieces", double(plan.clip.pieces)},
                                       {"removed", double(plan.clip.removed)}}}};
    return {{"command", name},
            {"terrain", QString::number(terrain)},
            {"edit", double(applied.edit)},
            {"changed", true},
            {"pad", padJson(world, applied.edit, stored)},
            {"tiles", QJsonObject{{"removed", idList(applied.tiles.removed)},
                                  {"added", idList(applied.tiles.added)}}},
            {"terrainReport", sourceEditReport(report, envelope)}};
}
std::uint32_t editIdOf(const QJsonValue &value) {
    if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble()) ||
        value.toDouble() < 1 || value.toDouble() > 4294967295.0)
        reject("INVALID_REQUEST", "edit is the integer ID of a recorded terrain change");
    return std::uint32_t(value.toDouble());
}
// terrain.update_edit: the pad's entry with its new parameters (or its footprint re-read from its
// source body, Restamp) and no resolved geometry, in place in `record` with the same ID. Returns
// the ID.
std::uint32_t updatedStamp(const Document &doc, Id terrain, const QJsonObject &command,
                           TerrainRecord &record) {
    const auto id = editIdOf(command["edit"]);
    auto &edits = record.edits;
    const auto found = std::find_if(edits.begin(), edits.end(),
                                    [&](const TerrainEditPtr &edit) { return edit->id == id; });
    if (found == edits.end())
        reject("NOT_FOUND", "The terrain has no change " + std::to_string(id));
    if ((*found)->kind != terrainStampKind)
        reject("INVALID_REQUEST", "Only pads (stamp changes) can be updated; this change is a " +
                                      (*found)->kind);
    const auto &old = dynamic_cast<const TerrainStampData &>(*(*found)->data);
    requireLevelTerrain(doc, terrain);
    const auto world = doc.worldTransform(terrain);
    requireUnscaledTerrain(world, "Updating a pad needs a terrain at its original scale");
    const auto toLocal = world.inverse();
    bool restamp = false;
    if (command.contains("restamp")) {
        if (!command["restamp"].isBool())
            reject("INVALID_REQUEST", "restamp is true or false");
        restamp = command["restamp"].toBool();
    }
    if (!restamp && !command.contains("elevation") && !command.contains("mode") &&
        !command.contains("cutSlope") && !command.contains("fillSlope") &&
        !command.contains("maxDistance") && !command.contains("width"))
        reject("INVALID_REQUEST", "Give restamp, elevation, mode, slopes, maxDistance or width");
    auto data = std::make_shared<TerrainStampData>(old);
    // The new parameters stand alone: the resolved geometry is recomputed by the replay (the
    // recorded pieces stay, so split source lines keep their ids).
    data->outline.clear();
    data->rays.clear();
    data->dropped = 0;
    if (restamp) {
        if (!old.sourceBody)
            reject("INVALID_REQUEST", "This pad's footprint was edges or a polygon; it has no "
                                      "source body to restamp");
        if (!doc.bodies().contains(old.sourceBody))
            reject("NOT_FOUND", "The pad's source body no longer exists");
        QJsonObject source{{"body", QString::number(old.sourceBody)}};
        const auto &body = *doc.bodies().at(old.sourceBody);
        if (old.sourceFace) {
            const auto face = body.surface.faces.find(old.sourceFace);
            if (face != body.surface.faces.end() &&
                !horizontalFace(doc, body, face->second).empty())
                source["face"] = QString::number(old.sourceFace);
        }
        const auto footprint = footprintOf(doc, source, toLocal);
        checkFootprintBands(footprint.plan, world);
        data->footprint = footprint.plan;
        data->sourceFace = footprint.face;
    }
    if (command.contains("elevation"))
        data->elevation = toLocal.point({0, 0, coordinate(command["elevation"])}).z + 0.0;
    gradingFields(command, *data, true);
    validateStamp(*data);
    *found = std::make_shared<const TerrainEdit>(TerrainEdit{id, terrainStampKind, data});
    return id;
}
QJsonObject stampTerrain(Document &doc, const QJsonObject &command, ChangeReport &changes) {
    onlyFields(command, {"command", "terrain", "footprint", "elevation", "mode", "cutSlope",
                         "fillSlope", "maxDistance", "width"});
    const auto terrain = identity(command["terrain"]);
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    requireLevelTerrain(doc, terrain);
    const auto world = doc.worldTransform(terrain);
    requireUnscaledTerrain(world, "Stamping a pad needs a terrain at its original scale");
    const auto toLocal = world.inverse();
    if (!command.contains("footprint"))
        reject("INVALID_REQUEST", "Give the footprint: {body, face}, {body}, {edges} or {polygon}");
    const auto footprint = footprintOf(doc, command["footprint"], toLocal);
    checkFootprintBands(footprint.plan, world);
    auto data = std::make_shared<TerrainStampData>();
    data->footprint = footprint.plan;
    data->sourceBody = footprint.body;
    data->sourceFace = footprint.face;
    double elevation = 0;
    if (command.contains("elevation"))
        elevation = coordinate(command["elevation"]);
    else if (footprint.z)
        elevation = *footprint.z;
    else
        reject("INVALID_REQUEST", "Give the pad elevation (model Z): the footprint has no single "
                                  "height");
    data->elevation = toLocal.point({0, 0, elevation}).z + 0.0;
    gradingFields(command, *data, false);
    validateStamp(*data);
    return applyStamp(doc, terrain, std::move(data), command["command"].toString(), changes);
}

// ---- R096.e: terrain.drape ---------------------------------------------------------------------
// Edges ({body, edge}, chained as terrain.create_from_edges chains them) and faces ({body, face},
// every loop) in any context, read in world coordinates and projected to the terrain frame's
// plan. Imprint (the default of decision 5) records a `drape` entry; overlay makes ordinary
// geometry in a new group and changes no terrain record.
struct DrapeInput {
    std::shared_ptr<TerrainDrapeData> data;
    std::vector<bool> edgeLines;                                   // per line: from edges
    std::vector<std::vector<std::vector<std::array<double, 2>>>> faces; // per face: its loops
    std::vector<MaterialSides> faceMaterials;                      // per face: its sides
    std::uint64_t verticalFaces{}, unpainted{}, edges{}, faceCount{};
    TerrainPolylineCleaning cleaning;
};
constexpr qsizetype terrainDrapeFaceLimit = 1024;
// A plan polyline in the terrain frame, cleaned (zero-length segments and spikes, as the importer
// cleans) with a closed polyline's closing repeat dropped; empty when nothing with length is left.
std::vector<std::array<double, 2>> drapePlan(const std::vector<Vec3> &model, bool closed,
                                             const Transform &toLocal,
                                             TerrainPolylineCleaning &cleaning) {
    std::vector<TerrainVec3> line;
    for (const auto &p : model) {
        const auto local = toLocal.point(p);
        line.push_back({local.x + 0.0, local.y + 0.0, 0});
    }
    while (closed && line.size() > 1 && std::abs(line.back().x - line.front().x) <= 1e-7 &&
           std::abs(line.back().y - line.front().y) <= 1e-7)
        line.pop_back();
    cleanTerrainPolyline(line, closed, cleaning);
    std::vector<std::array<double, 2>> plan;
    for (const auto &p : line)
        plan.push_back({p.x, p.y});
    if (plan.size() < (closed ? 3u : 2u))
        plan.clear();
    return plan;
}
DrapeInput drapeInput(const Document &doc, const QJsonObject &command, const Transform &toLocal,
                      bool paintRegions) {
    DrapeInput input;
    input.data = std::make_shared<TerrainDrapeData>();
    auto &data = *input.data;
    const bool hasEdges = command.contains("edges"), hasFaces = command.contains("faces");
    if (!hasEdges && !hasFaces)
        reject("INVALID_REQUEST", "Give edges, faces or both to drape");
    if (hasEdges) {
        TerrainEdgeSelection selection;
        selection.edges = edgeList(command["edges"], "edges");
        input.edges = selection.edges.size();
        if (!selection.edges.empty()) {
            const auto set = terrainEdgeSources(doc, selection);
            for (const auto &polyline : set.polylines) {
                std::vector<Vec3> model;
                for (const auto &p : polyline.points)
                    model.push_back({p.x, p.y, p.z});
                auto plan = drapePlan(model, polyline.closed, toLocal, input.cleaning);
                if (plan.empty())
                    continue; // a vertical edge has no plan length
                data.lines.push_back({std::move(plan), polyline.closed});
                input.edgeLines.push_back(true);
            }
        }
    }
    if (hasFaces) {
        if (!command["faces"].isArray())
            reject("INVALID_REQUEST", "faces is a list of {body, face}");
        const auto list = command["faces"].toArray();
        if (list.size() > terrainDrapeFaceLimit)
            reject("LIMIT_EXCEEDED", "Drape at most 1,024 faces at once");
        std::set<std::pair<Id, Id>> seen;
        for (const auto &value : list) {
            const auto object = value.toObject();
            if (!value.isObject() || object.size() != 2 || !object.contains("body") ||
                !object.contains("face"))
                reject("INVALID_REQUEST", "A face is {body, face}");
            const auto bodyId = identity(object["body"]), faceId = identity(object["face"]);
            const auto found = doc.bodies().find(bodyId);
            if (found == doc.bodies().end())
                reject("NOT_FOUND", "Body " + std::to_string(bodyId) + " does not exist");
            const auto &body = *found->second;
            if (body.terrainTile || body.kind == BodyKind::Terrain)
                reject("UNSUPPORTED_TARGET", terrainProtectedMessage);
            const auto face = body.surface.faces.find(faceId);
            if (face == body.surface.faces.end())
                reject("NOT_FOUND", "Face " + std::to_string(faceId) + " of body " +
                                        std::to_string(bodyId) + " does not exist");
            if (!seen.insert({bodyId, faceId}).second)
                reject("INVALID_REQUEST", "Face " + std::to_string(faceId) + " of body " +
                                              std::to_string(bodyId) + " is given more than once");
            ++input.faceCount;
            const auto world = doc.worldTransform(bodyId);
            std::vector<std::vector<std::array<double, 2>>> loops;
            for (const auto &loop : face->second.loops) {
                std::vector<Vec3> model;
                for (const auto vertex : loop) {
                    const auto p = body.surface.vertices.find(vertex);
                    if (p == body.surface.vertices.end())
                        reject("INVALID_REQUEST", "A face loop names a missing vertex");
                    model.push_back(world.point(p->second));
                }
                auto plan = drapePlan(model, true, toLocal, input.cleaning);
                if (!plan.empty())
                    loops.push_back(std::move(plan));
            }
            if (loops.empty()) {
                ++input.verticalFaces; // a vertical face has no plan area
                continue;
            }
            for (const auto &loop : loops) {
                data.lines.push_back({loop, true});
                input.edgeLines.push_back(false);
            }
            const auto sides = faceMaterials(body, faceId);
            input.faces.push_back(loops);
            input.faceMaterials.push_back(sides);
            if (paintRegions) {
                const Id material = sides.front ? sides.front : sides.back;
                if (material)
                    data.paint.push_back({loops, material});
                else
                    ++input.unpainted;
            }
        }
    }
    if (data.lines.empty())
        reject("INVALID_REQUEST", "The edges and faces have no length in plan (vertical edges and "
                                  "faces cannot be draped)");
    std::size_t points = 0;
    for (const auto &line : data.lines)
        points += line.points.size();
    if (data.lines.size() > terrainDrapeLines || points > terrainDrapePoints)
        reject("LIMIT_EXCEEDED", "A drape has at most 4,096 lines and 20,000 points (this one: " +
                                     std::to_string(data.lines.size()) + " lines, " +
                                     std::to_string(points) + " points); drape fewer at a time");
    return input;
}
// A paint loop whose outline crosses one horizontal band too often is refused with the band
// located, as sculpt regions and pad footprints are.
void checkPaintBands(const TerrainDrapeData &data, const TerrainRecord &record,
                     const Transform &world) {
    std::size_t sum = terrainPaintBandEdges(record);
    for (const auto &region : data.paint)
        for (const auto &loop : region.loops) {
            const auto bands = terrainSculptRegionBands(loop);
            sum += bands.edges;
            if (bands.edges <= terrainSculptBandEdges && sum <= terrainDrapePaintBandEdges)
                continue;
            double minX = loop[0][0], maxX = minX;
            for (const auto &p : loop) {
                minX = std::min(minX, p[0]);
                maxX = std::max(maxX, p[0]);
            }
            const auto at = world.point(
                {(minX + maxX) / 2, bands.low + (double(bands.busiest) + 0.5) * bands.height, 0});
            const std::string message =
                bands.edges > terrainSculptBandEdges
                    ? "A painted face's outline has " + std::to_string(bands.edges) +
                          " edges in one horizontal band (at most 512); simplify it"
                    : "The terrain's paint regions would be too complex to paint (over 4,096 "
                      "edges in their busiest bands in all); paint fewer or simpler faces";
            TerrainReport report;
            report.locate(TerrainReport::Severity::Refusal, "limit-exceeded", message,
                          {"paint", {}, std::array<double, 2>{at.x, at.y}});
            throw TerrainImportError("LIMIT_EXCEEDED", message, sourceEditReport(report, {}),
                                     "terrainReport");
        }
}
QJsonObject drapeEnvelope(const TerrainDrapePlan &plan, const DrapeInput &input,
                          const Transform &world) {
    QJsonObject envelope{{"lines", double(input.data->lines.size())},
                         {"runs", double(plan.resolved->runs.size())},
                         {"points", double(plan.points)},
                         {"intersections", double(plan.intersections)},
                         {"crossings", double(plan.crossings)},
                         {"snapped", double(plan.snapped)},
                         {"verticesInserted", double(plan.inserted)},
                         {"trianglesSplit", double(plan.change.removed.size())},
                         {"trianglesAdded", double(plan.change.added.size())},
                         {"sourcesSplit", double(plan.splitSegments)},
                         {"clipped", double(plan.clipped)}};
    if (plan.clipped) {
        const auto at = world.point({plan.clippedAt.x, plan.clippedAt.y, 0});
        envelope["clippedAt"] = QJsonArray{at.x, at.y};
    }
    return envelope;
}
void drapeNotices(TerrainReport &report, const TerrainDrapePlan &plan, const DrapeInput &input,
                  const Transform &world) {
    if (plan.clipped) {
        const auto at = world.point({plan.clippedAt.x, plan.clippedAt.y, 0});
        const char *message = "Parts of the lines outside the terrain's outline or in a hole were "
                              "clipped.";
        report.add(TerrainReport::Severity::Notice, "drape-clipped", plan.clipped, message);
        report.locate(TerrainReport::Severity::Notice, "drape-clipped", message,
                      {"drape", {}, std::array<double, 2>{at.x, at.y},
                       std::to_string(plan.clipped) + " parts clipped"},
                      false);
    }
    if (input.verticalFaces)
        report.add(TerrainReport::Severity::Notice, "vertical-faces", input.verticalFaces,
                   "Vertical faces have no area in plan and were not draped.");
    if (input.cleaning.zeroLength)
        report.add(TerrainReport::Severity::Notice, "zero-length-segments",
                   input.cleaning.zeroLength,
                   "Segments with no length in plan (vertical edges, repeated points) were "
                   "dropped.");
    if (input.cleaning.spikes)
        report.add(TerrainReport::Severity::Notice, "collinear-spikes", input.cleaning.spikes,
                   "Points where a line doubles back on itself in plan were removed.");
}
QJsonObject drapeImprint(Document &doc, Id terrain, const QJsonObject &command,
                         DrapeInput input, ChangeReport &changes) {
    const auto world = doc.worldTransform(terrain);
    const auto &record = *doc.bodies().at(terrain)->terrain;
    if (!input.data->paint.empty())
        checkPaintBands(*input.data, record, world);
    try {
        validateTerrainDrapeData(*input.data);
    } catch (const std::invalid_argument &error) {
        reject("INVALID_REQUEST", error.what());
    }
    TerrainReport report;
    const auto refused = [&](const TerrainEditRefused &error) {
        addProblems(report, error.problems(), world);
        throw TerrainImportError(error.code(), error.what(), sourceEditReport(report, {}),
                                 "terrainReport");
    };
    TerrainDrapePlan plan;
    try {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto state = terrainSourceState(record);
        const TerrainEdit entry{record.nextEditId, terrainDrapeKind, input.data};
        plan = planTerrainDrape({*mesh, *state, record.parameters}, entry);
    } catch (const TerrainEditRefused &error) {
        refused(error);
    }
    {
        const auto &first = plan.resolved->runs.front().front();
        const auto at = world.point(first);
        refuseOverTerrainLimit(doc,
                               std::ptrdiff_t(plan.change.added.size()) -
                                   std::ptrdiff_t(plan.change.removed.size()),
                               {at.x, at.y}, "Drape on terrain");
    }
    // The dry run is the kind's own outcome for this entry on the same mesh and sources, so the
    // commit uses it rather than planning again.
    TerrainEditApplied applied;
    try {
        const TerrainEditOutcome outcome{plan.change, {}, plan.resolved};
        applied = applyTerrainEdit(doc, terrain, terrainDrapeKind, plan.resolved,
                                   "Drape on terrain", &outcome);
    } catch (const TerrainEditRefused &error) {
        refused(error);
    }
    changes = std::move(applied.tiles.changes);
    drapeNotices(report, plan, input, world);
    if (input.unpainted)
        report.add(TerrainReport::Severity::Notice, "paint-without-material", input.unpainted,
                   "Faces without a material were imprinted but paint no region.");
    if (applied.tiles.paintDropped.faces)
        report.add(TerrainReport::Severity::Notice, "face-paint-dropped",
                   applied.tiles.paintDropped.faces,
                   "Per-face paint on the rebuilt tiles cannot follow the new mesh and was "
                   "removed; the tiles' own material is kept.");
    auto envelope = drapeEnvelope(plan, input, world);
    envelope["edit"] = double(applied.edit);
    envelope["mode"] = "imprint";
    envelope["paintRegions"] = double(input.data->paint.size());
    envelope["tilesReplaced"] = double(applied.tiles.removed.size());
    envelope["tilesAdded"] = double(applied.tiles.added.size());
    envelope["tilesRepainted"] = double(applied.tiles.repainted.size());
    return {{"command", command["command"]},
            {"terrain", QString::number(terrain)},
            {"mode", "imprint"},
            {"edit", double(applied.edit)},
            {"changed", true},
            {"tiles", QJsonObject{{"removed", idList(applied.tiles.removed)},
                                  {"added", idList(applied.tiles.added)},
                                  {"repainted", idList(applied.tiles.repainted)}}},
            {"terrainReport", sourceEditReport(report, envelope)}};
}
QJsonObject drapeOverlay(Document &doc, Id terrain, const QJsonObject &command, DrapeInput input,
                         double lift, Id context, ChangeReport &changes) {
    const auto world = doc.worldTransform(terrain);
    const auto &record = *doc.bodies().at(terrain)->terrain;
    try {
        validateTerrainDrapeData(*input.data);
    } catch (const std::invalid_argument &error) {
        reject("INVALID_REQUEST", error.what());
    }
    TerrainReport report;
    const auto refused = [&](const TerrainEditRefused &error) {
        addProblems(report, error.problems(), world);
        throw TerrainImportError(error.code(), error.what(), sourceEditReport(report, {}),
                                 "terrainReport");
    };
    const auto budget = terrainExplodeBudget(doc, terrain);
    const std::size_t room =
        budget.faceLimit > budget.modelFaces ? budget.faceLimit - budget.modelFaces : 0;
    // Lift is model metres up; in the terrain frame (level, maybe scaled) it is this much Z.
    const double liftLocal = world.inverse().vector({0, 0, lift}).z;
    TerrainDrapePlan plan;
    TerrainDrapeOverlay overlay;
    try {
        const auto mesh = terrainEditMeshCache().get(doc, terrain, true);
        const auto state = terrainSourceState(record);
        const TerrainEdit entry{record.nextEditId, terrainDrapeKind, input.data};
        plan = planTerrainDrape({*mesh, *state, record.parameters}, entry, true);
        overlay = terrainDrapeOverlay(*mesh, plan, input.edgeLines, input.faces, liftLocal,
                                      std::min<std::size_t>(room, 100000));
    } catch (const TerrainEditRefused &error) {
        if (std::any_of(error.problems().begin(), error.problems().end(),
                        [](const auto &problem) { return problem.kind == "limit-exceeded"; }) &&
            room < 100000)
            report.add(TerrainReport::Severity::Notice, "face-limit", budget.modelFaces,
                       "The document has " + groupedCount(budget.modelFaces) + " of " +
                           groupedCount(budget.faceLimit) + " faces.");
        refused(error);
    }
    if (overlay.faces.empty() && overlay.edges.empty())
        reject("INVALID_REQUEST", "Nothing to overlay: the faces cover no terrain triangle and "
                                  "no edge lies on the terrain");
    // The group, in the active context, holds the geometry in the terrain's frame.
    const Transform contextWorld = context ? doc.worldTransform(context) : Transform{};
    auto group = std::make_shared<Body>();
    group->id = doc.nextId();
    group->name = "Drape";
    group->kind = BodyKind::Group;
    group->parent = context;
    group->transform = contextWorld.inverse() * world;
    auto body = std::make_shared<Body>();
    body->id = group->id + 1;
    body->name = "Draped geometry";
    body->parent = group->id;
    std::unordered_map<TerrainPositionKey, Id, TerrainPositionHash> vertices;
    auto vertex = [&](const Vec3 &p) {
        const auto [it, fresh] = vertices.try_emplace(TerrainPositionKey::of(p), Id{});
        if (fresh) {
            it->second = body->surface.nextId++;
            body->surface.vertices.emplace(it->second, p);
        }
        return it->second;
    };
    std::set<std::pair<Id, Id>> faceEdges;
    for (const auto &face : overlay.faces) {
        std::vector<std::vector<Id>> loops;
        for (const auto &loop : face.loops) {
            loops.emplace_back();
            for (const auto &p : loop)
                loops.back().push_back(vertex(p));
            for (std::size_t i = 0; i < loops.back().size(); ++i) {
                const auto a = loops.back()[i], b = loops.back()[(i + 1) % loops.back().size()];
                faceEdges.insert({std::min(a, b), std::max(a, b)});
            }
        }
        const Id id = body->surface.nextId++;
        body->surface.faces.emplace(id, Face{id, std::move(loops)});
        const auto &sides = input.faceMaterials[face.face];
        if (sides != MaterialSides{})
            body->faceMaterials[id] = sides;
    }
    std::set<std::pair<Id, Id>> wires;
    for (const auto &[a, b] : overlay.edges) {
        const auto ia = vertex(a), ib = vertex(b);
        const std::pair edge{std::min(ia, ib), std::max(ia, ib)};
        if (ia == ib || faceEdges.contains(edge) || !wires.insert(edge).second)
            continue;
        body->surface.wires.push_back({ia, ib});
    }
    try {
        body->topology = Topology::rebuild(body->surface, {});
    } catch (const std::exception &error) {
        reject("INVALID_REQUEST", std::string("The draped geometry is not valid: ") + error.what());
    }
    Edit edit{"Drape on terrain", {}};
    edit.changes.push_back({group->id, nullptr, group});
    edit.changes.push_back({body->id, nullptr, body});
    const auto faces = body->surface.faces.size(), edgeCount = body->topology.edges.size();
    changes = doc.apply(std::move(edit), doc.revision());
    drapeNotices(report, plan, input, world);
    auto envelope = drapeEnvelope(plan, input, world);
    envelope["mode"] = "overlay";
    envelope["lift"] = lift;
    envelope["faces"] = double(faces);
    envelope["edges"] = double(edgeCount);
    return {{"command", command["command"]},
            {"terrain", QString::number(terrain)},
            {"mode", "overlay"},
            {"group", QString::number(group->id)},
            {"body", QString::number(body->id)},
            {"faces", double(faces)},
            {"edges", double(edgeCount)},
            {"changed", true},
            {"terrainReport", sourceEditReport(report, envelope)}};
}
QJsonObject drapeTerrain(Document &doc, const QJsonObject &command, ChangeReport &changes) {
    onlyFields(command, {"command", "terrain", "edges", "faces", "mode", "paintRegions", "lift",
                         "context"});
    const auto terrain = identity(command["terrain"]);
    if (!isTerrain(doc, terrain))
        reject("NOT_FOUND", "Terrain body does not exist");
    const auto mode = command["mode"].toString();
    if (!command["mode"].isString() || (mode != "imprint" && mode != "overlay"))
        reject("INVALID_REQUEST", "mode is imprint or overlay");
    const bool overlay = mode == "overlay";
    bool paintRegions = false;
    if (command.contains("paintRegions")) {
        if (overlay)
            reject("INVALID_REQUEST", "paintRegions applies to imprint");
        if (!command["paintRegions"].isBool())
            reject("INVALID_REQUEST", "paintRegions is true or false");
        paintRegions = command["paintRegions"].toBool();
    }
    double lift = terrainDrapeLiftDefault;
    if (command.contains("lift")) {
        if (!overlay)
            reject("INVALID_REQUEST", "lift applies to overlay");
        const auto value = command["lift"];
        if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0 ||
            value.toDouble() > terrainDrapeLiftMax)
            reject("INVALID_REQUEST", "lift is 0 to 1,000 m");
        lift = value.toDouble();
    }
    Id context = 0;
    if (command.contains("context")) {
        if (!overlay)
            reject("INVALID_REQUEST", "context applies to overlay (the group's parent)");
        if (command["context"].toString() != "0") {
            context = identity(command["context"]);
            const auto found = doc.bodies().find(context);
            if (found == doc.bodies().end())
                reject("NOT_FOUND", "The context does not exist");
            if (found->second->kind != BodyKind::Group)
                reject("UNSUPPORTED_TARGET", "The context is a group (or 0 for the model)");
            if (persistentlyLocked(doc, context))
                reject("INVALID_OPERATION", "Cannot edit a locked entity or its contents");
            if (componentBound(doc, context))
                reject("UNSUPPORTED_TARGET", "Open the component's own context to add geometry "
                                             "to a component; overlay adds to a group or the "
                                             "model");
        }
    }
    if (!overlay)
        requireUnlockedTerrain(doc, terrain);
    requireLevelTerrain(doc, terrain);
    const auto toLocal = doc.worldTransform(terrain).inverse();
    auto input = drapeInput(doc, command, toLocal, paintRegions);
    return overlay ? drapeOverlay(doc, terrain, command, std::move(input), lift, context, changes)
                   : drapeImprint(doc, terrain, command, std::move(input), changes);
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
    } else if (name == "terrain.update_edit") {
        onlyFields(command, {"command", "terrain", "edit", "restamp", "elevation", "mode",
                             "cutSlope", "fillSlope", "maxDistance", "width"});
        request.label = "Edit terrain change";
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
    } else if (name == "terrain.update_edit") {
        request.required = updatedStamp(doc, terrain, command, request.record);
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
                                request.notices, progress, request.required);
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
           name == "terrain.add_source" || name == "terrain.remove_source" ||
           name == "terrain.sculpt" || name == "terrain.stamp_pad" ||
           name == "terrain.update_edit" || name == "terrain.drape";
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
    if (name == "terrain.sculpt") {
        ChangeReport changes;
        auto record = sculptTerrain(doc, command, changes);
        if (operation)
            *operation = std::move(record);
        return changes;
    }
    if (name == "terrain.drape") {
        ChangeReport changes;
        auto record = drapeTerrain(doc, command, changes);
        if (operation)
            *operation = std::move(record);
        return changes;
    }
    if (name == "terrain.stamp_pad") {
        ChangeReport changes;
        auto record = stampTerrain(doc, command, changes);
        if (operation)
            *operation = std::move(record);
        return changes;
    }
    if (name == "terrain.create_from_edges" || name == "terrain.set_parameters" ||
        name == "terrain.regenerate" || name == "terrain.reset_edits" ||
        name == "terrain.remove_edit" || name == "terrain.update_edit") {
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
    const QJsonObject planPoint{{"type", "array"},
                                {"items", coordinateSchema},
                                {"minItems", 2},
                                {"maxItems", 2}};
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
        // R096.b (ADR 0169 §4.1): one of path (brush) or region; amount for raise, strength
        // and iterations for smooth.
        spec("terrain.sculpt", "Sculpt terrain",
             {{"terrain", terrainId},
              {"mode", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"raise", "smooth"}},
                                   {"description", "raise (a negative amount lowers; \"Sculpt "
                                                   "terrain\") or smooth (\"Smooth terrain\")"}}},
              {"path", QJsonObject{{"type", "array"},
                                   {"minItems", 1},
                                   {"maxItems", int(terrainSculptPathPoints)},
                                   {"items", planPoint},
                                   {"description", "Brush stroke as model [x, y]; a click is one "
                                                   "point. The stroke is swept: each vertex moves "
                                                   "once, by its distance to the whole path"}}},
              {"radius", QJsonObject{{"type", "number"},
                                     {"minimum", terrainSculptRadiusMin},
                                     {"maximum", terrainSculptRadiusMax},
                                     {"description", "Brush radius, plan metres"}}},
              {"falloff", QJsonObject{{"type", "string"},
                                      {"enum", QJsonArray{"smooth", "linear", "constant"}},
                                      {"description", "Weight from 1 at the path or inside the "
                                                      "region to 0 at the radius or falloff "
                                                      "width; default smooth"}}},
              {"region", QJsonObject{{"type", "array"},
                                     {"minItems", 3},
                                     {"maxItems", int(terrainSculptRegionPoints) + 1},
                                     {"items", planPoint},
                                     {"description", "Closed polygon as model [x, y] (at most "
                                                     "20,000 corners; a last point repeating the "
                                                     "first is dropped)"}}},
              {"falloffWidth", QJsonObject{{"type", "number"},
                                           {"minimum", 0},
                                           {"maximum", terrainSculptWidthMax},
                                           {"description", "Plan metres outside the region over "
                                                           "which the weight falls to 0; default "
                                                           "0"}}},
              {"amount", QJsonObject{{"type", "number"},
                                     {"minimum", -terrainSculptAmountMax},
                                     {"maximum", terrainSculptAmountMax},
                                     {"description", "raise: metres at full weight, not 0 "
                                                     "(negative lowers)"}}},
              {"strength", QJsonObject{{"type", "number"},
                                       {"exclusiveMinimum", 0},
                                       {"maximum", 1},
                                       {"description", "smooth: the step toward the neighbours' "
                                                       "mean per iteration; default 0.5"}}},
              {"iterations", QJsonObject{{"type", "integer"},
                                         {"minimum", 1},
                                         {"maximum", terrainSmoothIterationsMax},
                                         {"description", "smooth: default 1"}}}},
             {"terrain", "mode"}),
        // R096.d (ADR 0169 §4.3): one footprint form; daylight slopes or a fixed width.
        spec("terrain.stamp_pad", "Stamp pad",
             {{"terrain", terrainId},
              {"footprint",
               QJsonObject{
                   {"type", "object"},
                   {"description",
                    "{body, face}: a horizontal face; {body}: the lowest horizontal face of a "
                    "group, component or body; {edges}: model edges of one closed loop; "
                    "{polygon}: model points [x, y] or [x, y, z] (at most 20,000, simple, no "
                    "holes)"},
                   {"properties",
                    QJsonObject{{"body", idSchema},
                                {"face", idSchema},
                                {"edges", QJsonObject{{"type", "array"},
                                                      {"maxItems", int(terrainEdgeLimit)},
                                                      {"items", edgeSchema}}},
                                {"polygon",
                                 QJsonObject{{"type", "array"},
                                             {"minItems", 3},
                                             {"maxItems", int(terrainStampFootprintPoints) + 1},
                                             {"items", QJsonObject{{"type", "array"},
                                                                   {"items", coordinateSchema},
                                                                   {"minItems", 2},
                                                                   {"maxItems", 3}}}}}}},
                   {"additionalProperties", false}}},
              {"elevation", metres("Pad elevation (model Z, metres); default the footprint's own "
                                   "height")},
              {"mode", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"daylight", "width"}},
                                   {"description", "daylight (default): cut and fill slopes to "
                                                   "the ground; width: a band of fixed width"}}},
              {"cutSlope", QJsonObject{{"type", "number"},
                                       {"minimum", terrainStampSlopeMin},
                                       {"maximum", terrainStampSlopeMax},
                                       {"description", "Rise over run where the pad is below the "
                                                       "ground; default 0.5 (1:2)"}}},
              {"fillSlope", QJsonObject{{"type", "number"},
                                        {"minimum", terrainStampSlopeMin},
                                        {"maximum", terrainStampSlopeMax},
                                        {"description", "Rise over run where the pad is above the "
                                                        "ground; default 0.5 (1:2)"}}},
              {"maxDistance", QJsonObject{{"type", "number"},
                                          {"exclusiveMinimum", 0},
                                          {"maximum", terrainStampDistanceMax},
                                          {"description", "Plan metres a grading ray may run "
                                                          "before it daylights; default 100"}}},
              {"width", QJsonObject{{"type", "number"},
                                    {"minimum", terrainStampWidthMin},
                                    {"maximum", terrainStampDistanceMax},
                                    {"description", "mode width: the band's plan width in "
                                                    "metres"}}}},
             {"terrain", "footprint"}),
        spec("terrain.drape", "Drape on terrain",
             {{"terrain", terrainId},
              {"edges", QJsonObject{{"type", "array"},
                                    {"maxItems", int(terrainEdgeLimit)},
                                    {"items", edgeSchema},
                                    {"description", "Model edges and curves {body, edge} in any "
                                                    "context, projected vertically; chained as "
                                                    "terrain.create_from_edges chains them"}}},
              {"faces", QJsonObject{{"type", "array"},
                                    {"maxItems", int(terrainDrapeFaceLimit)},
                                    {"items", QJsonObject{{"type", "object"},
                                                          {"properties",
                                                           QJsonObject{{"body", idSchema},
                                                                       {"face", idSchema}}},
                                                          {"required", QJsonArray{"body", "face"}},
                                                          {"additionalProperties", false}}},
                                    {"description", "Model faces {body, face} in any context; "
                                                    "every loop is draped"}}},
              {"mode", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"imprint", "overlay"}},
                                   {"description", "imprint: split the terrain's triangles along "
                                                   "the lines (shape unchanged) and keep them as "
                                                   "breaklines; overlay: ordinary geometry in a "
                                                   "new group, terrain unchanged"}}},
              {"paintRegions", QJsonObject{{"type", "boolean"},
                                           {"description", "imprint: paint the terrain inside "
                                                           "each face with the face's material, "
                                                           "on every rebuild too"}}},
              {"lift", QJsonObject{{"type", "number"},
                                   {"minimum", 0},
                                   {"maximum", terrainDrapeLiftMax},
                                   {"description", "overlay: metres above the surface; default "
                                                   "0.01"}}},
              {"context", QJsonObject{{"type", "string"},
                                      {"pattern", "^(0|[1-9][0-9]*)$"},
                                      {"maxLength", 20},
                                      {"description", "overlay: the group the new group goes in "
                                                      "(the active context); 0 or omitted for "
                                                      "the model"}}}},
             {"terrain", "mode"}),
        spec("terrain.update_edit", "Edit terrain change",
             {{"terrain", terrainId},
              {"edit", QJsonObject{{"type", "integer"},
                                   {"minimum", 1},
                                   {"maximum", 4294967295.0},
                                   {"description", "ID of a recorded pad (stamp) change; the "
                                                   "list is replayed"}}},
              {"restamp", QJsonObject{{"type", "boolean"},
                                      {"description", "Re-read the footprint from the pad's "
                                                      "source body"}}},
              {"elevation", metres("New pad elevation (model Z, metres)")},
              {"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"daylight", "width"}}}},
              {"cutSlope", QJsonObject{{"type", "number"},
                                       {"minimum", terrainStampSlopeMin},
                                       {"maximum", terrainStampSlopeMax}}},
              {"fillSlope", QJsonObject{{"type", "number"},
                                        {"minimum", terrainStampSlopeMin},
                                        {"maximum", terrainStampSlopeMax}}},
              {"maxDistance", QJsonObject{{"type", "number"},
                                          {"exclusiveMinimum", 0},
                                          {"maximum", terrainStampDistanceMax}}},
              {"width", QJsonObject{{"type", "number"},
                                    {"minimum", terrainStampWidthMin},
                                    {"maximum", terrainStampDistanceMax}}}},
             {"terrain", "edit"}),
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
namespace {
// R096.e: the paint regions (imprints with region paint), with their material and extent in model
// coordinates; at most 200 listed.
QJsonObject paintRegionsJson(const TerrainRecord &record, const Transform &world) {
    QJsonArray entries;
    std::size_t count = 0;
    for (const auto &edit : record.edits) {
        if (edit->kind != terrainDrapeKind)
            continue;
        const auto &data = dynamic_cast<const TerrainDrapeData &>(*edit->data);
        for (const auto &region : data.paint) {
            ++count;
            if (entries.size() >= 200)
                continue;
            std::array<double, 4> box{std::numeric_limits<double>::infinity(),
                                      std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity(),
                                      -std::numeric_limits<double>::infinity()};
            for (const auto &loop : region.loops)
                for (const auto &p : loop) {
                    box[0] = std::min(box[0], p[0]);
                    box[1] = std::min(box[1], p[1]);
                    box[2] = std::max(box[2], p[0]);
                    box[3] = std::max(box[3], p[1]);
                }
            entries.append(QJsonObject{{"edit", double(edit->id)},
                                       {"material", QString::number(region.material)},
                                       {"loops", double(region.loops.size())},
                                       {"extent", modelBox(world, box)},
                                       {"applied", data.resolved()}});
        }
    }
    return {{"count", double(count)},
            {"space", "model"},
            {"entries", entries},
            {"entriesOmitted", double(count - std::size_t(entries.size()))}};
}
// R096.d: the pads (stamp entries) with their elevation and extent, in model coordinates; at
// most 200 listed.
QJsonObject padsJson(const TerrainRecord &record, const Transform &world) {
    QJsonArray entries;
    std::size_t count = 0;
    for (const auto &edit : record.edits) {
        if (edit->kind != terrainStampKind)
            continue;
        ++count;
        if (entries.size() < 200)
            entries.append(padJson(world, edit->id,
                                   dynamic_cast<const TerrainStampData &>(*edit->data)));
    }
    return {{"count", double(count)},
            {"space", "model"},
            {"entries", entries},
            {"entriesOmitted", double(count - std::size_t(entries.size()))}};
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
        {"pads", padsJson(record, world)},
        {"paintRegions", paintRegionsJson(record, world)},
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
