#include "automation/terrain_commands.hpp"
#include "core/terrain.hpp"
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
} // namespace
bool isTerrainCommand(const QString &name) {
    return name == "site.set" || name == "site.reanchor" || name == "terrain.explode";
}
ChangeReport executeTerrainCommand(Document &doc, const QJsonObject &command) {
    const auto name = command["command"].toString();
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
    reject("UNSUPPORTED_CAPABILITY", "Unavailable terrain command");
}
QJsonArray terrainCommandSpecs() {
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
             {"body"})};
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
