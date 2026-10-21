#include "io/terrain_io.hpp"
#include "core/document_limits.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/terrain_source_edit.hpp"
#include "io/native_limits.hpp"
#include <QJsonArray>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
namespace sketchy {
static_assert(NativeLimits::terrainMeshBytes == terrainMeshChunkLimit);
namespace {
const QByteArray meshMagic("SKTMESH1", 8);
QJsonArray array(const QJsonValue &value) {
    if (!value.isArray())
        throw std::runtime_error("Expected array");
    return value.toArray();
}
QJsonObject object(const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Expected object");
    return value.toObject();
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Expected finite number");
    return value.toDouble();
}
std::string text(const QJsonValue &value) {
    if (!value.isString())
        throw std::runtime_error("Expected string");
    return value.toString().toStdString();
}
bool flag(const QJsonValue &value) {
    if (!value.isBool())
        throw std::runtime_error("Expected boolean");
    return value.toBool();
}
void exactFields(const QJsonObject &record, const QStringList &fields, const char *what) {
    if (record.size() != fields.size())
        throw std::runtime_error(std::string("Unexpected fields in ") + what);
    for (const auto &field : fields)
        if (!record.contains(field))
            throw std::runtime_error(std::string("Missing field in ") + what);
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
    throw std::runtime_error("Unknown terrain source role");
}
TerrainSourceRole role(const QJsonValue &value) {
    for (auto candidate : {TerrainSourceRole::Contour, TerrainSourceRole::Breakline,
                           TerrainSourceRole::Boundary, TerrainSourceRole::Hole,
                           TerrainSourceRole::Points})
        if (value == roleName(candidate))
            return candidate;
    throw std::runtime_error("Unknown terrain source role");
}
const char *boundaryName(TerrainBoundaryMode mode) {
    switch (mode) {
    case TerrainBoundaryMode::ConvexHull:
        return "convex-hull";
    case TerrainBoundaryMode::MaxEdgeLength:
        return "max-edge-length";
    case TerrainBoundaryMode::Boundary:
        return "boundary";
    }
    throw std::runtime_error("Unknown terrain boundary mode");
}
TerrainBoundaryMode boundary(const QJsonValue &value) {
    for (auto candidate : {TerrainBoundaryMode::ConvexHull, TerrainBoundaryMode::MaxEdgeLength,
                           TerrainBoundaryMode::Boundary})
        if (value == boundaryName(candidate))
            return candidate;
    throw std::runtime_error("Unknown terrain boundary mode");
}
const char *shadingName(TerrainShading mode) {
    return mode == TerrainShading::ElevationBands ? "elevation-bands" : "material";
}
TerrainShading shading(const QJsonValue &value) {
    for (auto candidate : {TerrainShading::Material, TerrainShading::ElevationBands})
        if (value == shadingName(candidate))
            return candidate;
    throw std::runtime_error("Unknown terrain shading");
}
// A JSON integer in [low, high] (doubles hold every integer up to 2^53 exactly).
double integer(const QJsonValue &value, double low, double high) {
    const auto v = number(value);
    if (v != std::floor(v) || v < low || v > high)
        throw std::runtime_error("Expected an integer in range");
    return v;
}
std::vector<TerrainEditCodec> builtinCodecs();
std::vector<TerrainEditCodec> &codecs() {
    static std::vector<TerrainEditCodec> registered = builtinCodecs();
    return registered;
}
const TerrainEditCodec *codecOf(const std::string &kind) {
    for (const auto &codec : codecs())
        if (codec.kind == kind)
            return &codec;
    return nullptr;
}
void append32(QByteArray &out, std::uint32_t value) {
    char bytes[4];
    qToLittleEndian<quint32>(value, bytes);
    out.append(bytes, 4);
}
void append64(QByteArray &out, std::uint64_t value) {
    char bytes[8];
    qToLittleEndian<quint64>(value, bytes);
    out.append(bytes, 8);
}
// R096.c source edits (ADR 0169 §7): {source, role, points, before} as the kind needs them.
// Every bound is checked before allocating.
std::vector<TerrainEditCodec> builtinCodecs() {
    std::vector<TerrainEditCodec> list;
    for (const std::string kind :
         {terrainEditSourceKind, terrainAddSourceKind, terrainRemoveSourceKind}) {
        const bool hasPoints = kind != terrainRemoveSourceKind;
        const bool hasBefore = kind != terrainAddSourceKind;
        list.push_back(
            {kind,
             [hasPoints, hasBefore](const TerrainEditData &data) {
                 const auto &edit = dynamic_cast<const TerrainSourceEditData &>(data);
                 QJsonObject object{{"source", double(edit.source)}};
                 if (hasPoints) {
                     object["role"] = roleName(edit.role);
                     QJsonArray points;
                     for (const auto &p : edit.points)
                         points.append(QJsonArray{p.x, p.y, p.z});
                     object["points"] = points;
                 }
                 if (hasBefore)
                     object["before"] =
                         QJsonArray{edit.before[0], edit.before[1], edit.before[2], edit.before[3]};
                 return object;
             },
             [kind, hasPoints, hasBefore](const QJsonObject &entry)
                 -> std::shared_ptr<const TerrainEditData> {
                 QStringList fields{"source"};
                 if (hasPoints)
                     fields << "role" << "points";
                 if (hasBefore)
                     fields << "before";
                 exactFields(entry, fields, "terrain source edit");
                 auto data = std::make_shared<TerrainSourceEditData>();
                 data->source = std::uint32_t(integer(entry["source"], 1, 4294967295.0));
                 if (hasPoints) {
                     data->role = role(entry["role"]);
                     const auto points = array(entry["points"]);
                     if (std::size_t(points.size()) > terrainSourceEditPoints)
                         throw std::runtime_error("A terrain source edit has too many points");
                     data->points.reserve(std::size_t(points.size()));
                     for (const auto &value : points) {
                         const auto xyz = array(value);
                         if (xyz.size() != 3)
                             throw std::runtime_error("A terrain source edit point is [x, y, z]");
                         data->points.push_back({number(xyz[0]), number(xyz[1]), number(xyz[2])});
                     }
                 }
                 if (hasBefore) {
                     const auto box = array(entry["before"]);
                     if (box.size() != 4)
                         throw std::runtime_error("A terrain source edit box is four numbers");
                     for (int i = 0; i < 4; ++i)
                         data->before[std::size_t(i)] = number(box[i]);
                 }
                 try {
                     validateTerrainSourceEditData(kind, *data);
                 } catch (const std::invalid_argument &error) {
                     throw std::runtime_error(error.what());
                 }
                 return data;
             }});
    }
    // R096.b sculpt and smooth (ADR 0169 §4.1, §7): a brush {path, radius} or a region
    // {region, falloffWidth}, the falloff, and {amount} or {strength, iterations}. Plan points
    // are [x, y] in the terrain frame.
    for (const std::string kind : {terrainSculptKind, terrainSmoothKind}) {
        const bool sculpt = kind == terrainSculptKind;
        list.push_back(
            {kind,
             [sculpt](const TerrainEditData &data) {
                 const auto &edit = dynamic_cast<const TerrainSculptData &>(data);
                 auto plan = [](const std::vector<std::array<double, 2>> &points) {
                     QJsonArray list;
                     for (const auto &p : points)
                         list.append(QJsonArray{p[0], p[1]});
                     return list;
                 };
                 QJsonObject object{{"falloff", terrainFalloffName(edit.falloff)}};
                 if (edit.brush()) {
                     object["path"] = plan(edit.path);
                     object["radius"] = edit.radius;
                 } else {
                     object["region"] = plan(edit.region);
                     object["falloffWidth"] = edit.width;
                 }
                 if (sculpt)
                     object["amount"] = edit.amount;
                 else {
                     object["strength"] = edit.strength;
                     object["iterations"] = edit.iterations;
                 }
                 if (!edit.pieces.empty()) {
                     QJsonArray pieces;
                     for (const auto &[parent, id] : edit.pieces)
                         pieces.append(QJsonArray{double(parent), double(id)});
                     object["pieces"] = pieces;
                 }
                 return object;
             },
             [kind, sculpt](const QJsonObject &entry) -> std::shared_ptr<const TerrainEditData> {
                 const bool brush = entry.contains("path");
                 QStringList fields{"falloff"};
                 fields << (brush ? "path" : "region") << (brush ? "radius" : "falloffWidth");
                 if (sculpt)
                     fields << "amount";
                 else
                     fields << "strength" << "iterations";
                 if (entry.contains("pieces"))
                     fields << "pieces";
                 exactFields(entry, fields, "terrain sculpt");
                 auto data = std::make_shared<TerrainSculptData>();
                 const auto falloff = terrainFalloffOf(text(entry["falloff"]));
                 if (!falloff)
                     throw std::runtime_error("Unknown terrain sculpt falloff");
                 data->falloff = *falloff;
                 const auto points = array(entry[brush ? "path" : "region"]);
                 if (std::size_t(points.size()) >
                     (brush ? terrainSculptPathPoints : terrainSculptRegionPoints))
                     throw std::runtime_error("A terrain sculpt has too many points");
                 auto &list = brush ? data->path : data->region;
                 list.reserve(std::size_t(points.size()));
                 for (const auto &value : points) {
                     const auto xy = array(value);
                     if (xy.size() != 2)
                         throw std::runtime_error("A terrain sculpt point is [x, y]");
                     list.push_back({number(xy[0]), number(xy[1])});
                 }
                 (brush ? data->radius : data->width) =
                     number(entry[brush ? "radius" : "falloffWidth"]);
                 if (sculpt)
                     data->amount = number(entry["amount"]);
                 else {
                     data->strength = number(entry["strength"]);
                     data->iterations =
                         int(integer(entry["iterations"], 1, terrainSmoothIterationsMax));
                 }
                 if (entry.contains("pieces")) {
                     const auto pieces = array(entry["pieces"]);
                     if (pieces.isEmpty() ||
                         std::size_t(pieces.size()) > terrainSculptPiecesPerEdit)
                         throw std::runtime_error("A terrain sculpt records 1 to 4,096 pieces");
                     for (const auto &value : pieces) {
                         const auto pair = array(value);
                         if (pair.size() != 2)
                             throw std::runtime_error("A terrain sculpt piece is [source, id]");
                         data->pieces.push_back(
                             {std::uint32_t(integer(pair[0], 1, 4294967295.0)),
                              std::uint32_t(integer(pair[1], 1, 4294967295.0))});
                     }
                 }
                 try {
                     validateTerrainSculptData(kind, *data);
                 } catch (const std::invalid_argument &error) {
                     throw std::runtime_error(error.what());
                 }
                 return data;
             }});
    }
    // R096.d stamp (ADR 0169 §4.3, §7): the parameters {footprint, elevation, mode, cutSlope,
    // fillSlope, maxDistance | width, sourceBody?, sourceFace?} and, when the pad applied, its
    // resolved geometry {outline, rays [[start, x, y, z]], dropped?, pieces?}. Plan points are
    // [x, y] in the terrain frame; body and face IDs are decimal strings.
    list.push_back(
        {terrainStampKind,
         [](const TerrainEditData &data) {
             const auto &stamp = dynamic_cast<const TerrainStampData &>(data);
             auto plan = [](const std::vector<std::array<double, 2>> &points) {
                 QJsonArray list;
                 for (const auto &p : points)
                     list.append(QJsonArray{p[0], p[1]});
                 return list;
             };
             QJsonObject object{{"footprint", plan(stamp.footprint)},
                                {"elevation", stamp.elevation},
                                {"mode", terrainStampModeName(stamp.mode)}};
             if (stamp.mode == TerrainStampMode::Daylight) {
                 object["cutSlope"] = stamp.cutSlope;
                 object["fillSlope"] = stamp.fillSlope;
                 object["maxDistance"] = stamp.maxDistance;
             } else
                 object["width"] = stamp.width;
             if (stamp.sourceBody)
                 object["sourceBody"] = QString::number(stamp.sourceBody);
             if (stamp.sourceFace)
                 object["sourceFace"] = QString::number(stamp.sourceFace);
             if (stamp.resolved()) {
                 object["outline"] = plan(stamp.outline);
                 QJsonArray rays;
                 for (const auto &ray : stamp.rays)
                     rays.append(QJsonArray{double(ray.start), ray.daylight.x, ray.daylight.y,
                                            ray.daylight.z});
                 object["rays"] = rays;
                 if (stamp.dropped)
                     object["dropped"] = double(stamp.dropped);
                 if (!stamp.pieces.empty()) {
                     QJsonArray pieces;
                     for (const auto &[parent, id] : stamp.pieces)
                         pieces.append(QJsonArray{double(parent), double(id)});
                     object["pieces"] = pieces;
                 }
             }
             return object;
         },
         [](const QJsonObject &entry) -> std::shared_ptr<const TerrainEditData> {
             auto data = std::make_shared<TerrainStampData>();
             const auto mode = text(entry["mode"]);
             if (mode != "daylight" && mode != "width")
                 throw std::runtime_error("A terrain stamp's mode is daylight or width");
             data->mode = mode == "width" ? TerrainStampMode::Width : TerrainStampMode::Daylight;
             QStringList fields{"footprint", "elevation", "mode"};
             if (data->mode == TerrainStampMode::Daylight)
                 fields << "cutSlope" << "fillSlope" << "maxDistance";
             else
                 fields << "width";
             for (const char *optional : {"sourceBody", "sourceFace", "dropped", "pieces"})
                 if (entry.contains(optional))
                     fields << optional;
             const bool resolved = entry.contains("outline") || entry.contains("rays");
             if (resolved)
                 fields << "outline" << "rays";
             exactFields(entry, fields, "terrain stamp");
             auto plan = [](const QJsonValue &value, std::size_t limit,
                            std::vector<std::array<double, 2>> &out) {
                 const auto points = array(value);
                 if (std::size_t(points.size()) > limit)
                     throw std::runtime_error("A terrain stamp has too many points");
                 out.reserve(std::size_t(points.size()));
                 for (const auto &p : points) {
                     const auto xy = array(p);
                     if (xy.size() != 2)
                         throw std::runtime_error("A terrain stamp point is [x, y]");
                     out.push_back({number(xy[0]), number(xy[1])});
                 }
             };
             plan(entry["footprint"], terrainStampFootprintPoints, data->footprint);
             data->elevation = number(entry["elevation"]);
             if (data->mode == TerrainStampMode::Daylight) {
                 data->cutSlope = number(entry["cutSlope"]);
                 data->fillSlope = number(entry["fillSlope"]);
                 data->maxDistance = number(entry["maxDistance"]);
             } else
                 data->width = number(entry["width"]);
             auto id = [](const QJsonValue &value) {
                 bool ok = false;
                 const auto text = value.toString();
                 const auto parsed = text.toULongLong(&ok);
                 if (!value.isString() || !ok || !parsed || QString::number(parsed) != text)
                     throw std::runtime_error("A terrain stamp's source is a stable ID");
                 return Id(parsed);
             };
             if (entry.contains("sourceBody"))
                 data->sourceBody = id(entry["sourceBody"]);
             if (entry.contains("sourceFace"))
                 data->sourceFace = id(entry["sourceFace"]);
             if (resolved) {
                 plan(entry["outline"], terrainStampOutlinePoints, data->outline);
                 if (data->outline.empty())
                     throw std::runtime_error("A resolved terrain stamp has an outline");
                 const auto rays = array(entry["rays"]);
                 if (std::size_t(rays.size()) > terrainStampRays)
                     throw std::runtime_error("A terrain stamp has too many rays");
                 data->rays.reserve(std::size_t(rays.size()));
                 for (const auto &value : rays) {
                     const auto ray = array(value);
                     if (ray.size() != 4)
                         throw std::runtime_error("A terrain stamp ray is [start, x, y, z]");
                     data->rays.push_back(
                         {std::uint32_t(integer(ray[0], 0, double(terrainStampOutlinePoints))),
                          {number(ray[1]), number(ray[2]), number(ray[3])}});
                 }
             }
             if (entry.contains("dropped"))
                 data->dropped =
                     std::uint32_t(integer(entry["dropped"], 1, double(terrainStampRays) * 4));
             if (entry.contains("pieces")) {
                 const auto pieces = array(entry["pieces"]);
                 if (pieces.isEmpty() || std::size_t(pieces.size()) > terrainSculptPiecesPerEdit)
                     throw std::runtime_error("A terrain stamp records 1 to 4,096 pieces");
                 for (const auto &value : pieces) {
                     const auto pair = array(value);
                     if (pair.size() != 2)
                         throw std::runtime_error("A terrain stamp piece is [source, id]");
                     data->pieces.push_back({std::uint32_t(integer(pair[0], 1, 4294967295.0)),
                                             std::uint32_t(integer(pair[1], 1, 4294967295.0))});
                 }
             }
             try {
                 validateTerrainStampData(*data);
             } catch (const std::invalid_argument &error) {
                 throw std::runtime_error(error.what());
             }
             return data;
         }});
    // R096.e drape (ADR 0169 §4.4, §7): the parameters {lines [{points [[x, y]], closed?}],
    // paint? [{loops [[[x, y]]], material}]} and, when the imprint applied, its resolved geometry
    // {runs [[[x, y, z]]], splits? [{a [x, y], b [x, y], points [[x, y, z]]}]}. Terrain frame;
    // material IDs are decimal strings. Every count is checked before anything is allocated.
    list.push_back(
        {terrainDrapeKind,
         [](const TerrainEditData &data) {
             const auto &drape = dynamic_cast<const TerrainDrapeData &>(data);
             auto plan = [](const std::vector<std::array<double, 2>> &points) {
                 QJsonArray list;
                 for (const auto &p : points)
                     list.append(QJsonArray{p[0], p[1]});
                 return list;
             };
             auto space = [](const std::vector<Vec3> &points) {
                 QJsonArray list;
                 for (const auto &p : points)
                     list.append(QJsonArray{p.x, p.y, p.z});
                 return list;
             };
             QJsonArray lines;
             for (const auto &line : drape.lines) {
                 QJsonObject entry{{"points", plan(line.points)}};
                 if (line.closed)
                     entry["closed"] = true;
                 lines.append(entry);
             }
             QJsonObject object{{"lines", lines}};
             if (!drape.paint.empty()) {
                 QJsonArray paint;
                 for (const auto &region : drape.paint) {
                     QJsonArray loops;
                     for (const auto &loop : region.loops)
                         loops.append(plan(loop));
                     paint.append(QJsonObject{{"loops", loops},
                                              {"material", QString::number(region.material)}});
                 }
                 object["paint"] = paint;
             }
             if (drape.resolved()) {
                 QJsonArray runs;
                 for (const auto &run : drape.runs)
                     runs.append(space(run));
                 object["runs"] = runs;
                 if (!drape.splits.empty()) {
                     QJsonArray splits;
                     for (const auto &split : drape.splits)
                         splits.append(QJsonObject{{"a", QJsonArray{split.a[0], split.a[1]}},
                                                   {"b", QJsonArray{split.b[0], split.b[1]}},
                                                   {"points", space(split.points)}});
                     object["splits"] = splits;
                 }
             }
             return object;
         },
         [](const QJsonObject &entry) -> std::shared_ptr<const TerrainEditData> {
             auto data = std::make_shared<TerrainDrapeData>();
             QStringList fields{"lines"};
             for (const char *optional : {"paint", "runs", "splits"})
                 if (entry.contains(optional))
                     fields << optional;
             exactFields(entry, fields, "terrain drape");
             std::size_t budget = terrainDrapePoints;
             auto plan = [&](const QJsonValue &value, std::vector<std::array<double, 2>> &out) {
                 const auto points = array(value);
                 if (std::size_t(points.size()) > budget)
                     throw std::runtime_error("A terrain drape has too many points");
                 budget -= std::size_t(points.size());
                 out.reserve(std::size_t(points.size()));
                 for (const auto &p : points) {
                     const auto xy = array(p);
                     if (xy.size() != 2)
                         throw std::runtime_error("A terrain drape point is [x, y]");
                     out.push_back({number(xy[0]), number(xy[1])});
                 }
             };
             std::size_t spaceBudget = 0;
             auto space = [&](const QJsonValue &value, std::vector<Vec3> &out) {
                 const auto points = array(value);
                 if (std::size_t(points.size()) > spaceBudget)
                     throw std::runtime_error("A terrain drape has too many resolved points");
                 spaceBudget -= std::size_t(points.size());
                 out.reserve(std::size_t(points.size()));
                 for (const auto &p : points) {
                     const auto xyz = array(p);
                     if (xyz.size() != 3)
                         throw std::runtime_error("A terrain drape resolved point is [x, y, z]");
                     out.push_back({number(xyz[0]), number(xyz[1]), number(xyz[2])});
                 }
             };
             const auto lines = array(entry["lines"]);
             if (lines.isEmpty() || std::size_t(lines.size()) > terrainDrapeLines)
                 throw std::runtime_error("A terrain drape has 1 to 4,096 lines");
             data->lines.reserve(std::size_t(lines.size()));
             for (const auto &value : lines) {
                 const auto line = object(value);
                 TerrainDrapeLine parsed;
                 if (line.contains("closed")) {
                     exactFields(line, {"points", "closed"}, "terrain drape line");
                     if (!flag(line["closed"]))
                         throw std::runtime_error("A terrain drape line writes closed only when "
                                                  "true");
                     parsed.closed = true;
                 } else
                     exactFields(line, {"points"}, "terrain drape line");
                 plan(line["points"], parsed.points);
                 data->lines.push_back(std::move(parsed));
             }
             if (entry.contains("paint")) {
                 const auto paint = array(entry["paint"]);
                 if (paint.isEmpty() || std::size_t(paint.size()) > terrainDrapePaintRegions)
                     throw std::runtime_error("A terrain drape has 1 to 1,024 paint regions");
                 budget = terrainDrapePoints;
                 data->paint.reserve(std::size_t(paint.size()));
                 for (const auto &value : paint) {
                     const auto region = object(value);
                     exactFields(region, {"loops", "material"}, "terrain drape paint region");
                     TerrainDrapePaint parsed;
                     const auto material = region["material"].toString();
                     bool ok = false;
                     const auto id = material.toULongLong(&ok);
                     if (!region["material"].isString() || !ok || !id ||
                         QString::number(id) != material)
                         throw std::runtime_error("A terrain drape's material is a stable ID");
                     parsed.material = Id(id);
                     const auto loops = array(region["loops"]);
                     if (loops.isEmpty() || std::size_t(loops.size()) > budget)
                         throw std::runtime_error("A terrain drape paint region has loops");
                     for (const auto &loop : loops) {
                         // A loop has at least three points, so empty loops cannot be many.
                         if (!loop.isArray() || loop.toArray().size() < 3)
                             throw std::runtime_error("A terrain drape paint loop has at least "
                                                      "three points");
                         parsed.loops.emplace_back();
                         plan(loop, parsed.loops.back());
                     }
                     data->paint.push_back(std::move(parsed));
                 }
             }
             if (entry.contains("runs")) {
                 const auto runs = array(entry["runs"]);
                 if (runs.isEmpty() || std::size_t(runs.size()) > terrainDrapeRuns)
                     throw std::runtime_error("A resolved terrain drape has 1 to 4,096 runs");
                 spaceBudget = terrainDrapeResolvedPoints;
                 data->runs.reserve(std::size_t(runs.size()));
                 for (const auto &run : runs) {
                     data->runs.emplace_back();
                     space(run, data->runs.back());
                 }
             } else if (entry.contains("splits"))
                 throw std::runtime_error("A terrain drape that did not apply splits no source");
             if (entry.contains("splits")) {
                 const auto splits = array(entry["splits"]);
                 if (splits.isEmpty() || std::size_t(splits.size()) > terrainDrapeSplitPoints)
                     throw std::runtime_error("A terrain drape records 1 to 65,536 splits");
                 spaceBudget = terrainDrapeSplitPoints;
                 data->splits.reserve(std::size_t(splits.size()));
                 for (const auto &value : splits) {
                     const auto split = object(value);
                     exactFields(split, {"a", "b", "points"}, "terrain drape split");
                     TerrainDrapeSplit parsed;
                     for (auto [key, out] : {std::pair{"a", &parsed.a}, std::pair{"b", &parsed.b}}) {
                         const auto xy = array(split[key]);
                         if (xy.size() != 2)
                             throw std::runtime_error("A terrain drape split end is [x, y]");
                         *out = {number(xy[0]), number(xy[1])};
                     }
                     space(split["points"], parsed.points);
                     data->splits.push_back(std::move(parsed));
                 }
             }
             try {
                 validateTerrainDrapeData(*data);
             } catch (const std::invalid_argument &error) {
                 throw std::runtime_error(error.what());
             }
             return data;
         }});
    return list;
}
} // namespace
void registerTerrainEditCodec(TerrainEditCodec codec) {
    if (codec.kind.empty() || !codec.encode || !codec.decode)
        throw std::invalid_argument("Invalid terrain edit codec");
    for (auto &existing : codecs())
        if (existing.kind == codec.kind) {
            existing = std::move(codec);
            return;
        }
    codecs().push_back(std::move(codec));
}
QJsonObject encodeTerrainEdit(const TerrainEdit &edit) {
    const auto *codec = codecOf(edit.kind);
    if (!codec || !edit.data)
        throw std::runtime_error("Unknown terrain edit kind");
    auto object = codec->encode(*edit.data);
    if (object.contains("id") || object.contains("kind"))
        throw std::runtime_error("A terrain edit codec wrote a reserved field");
    object["id"] = double(edit.id);
    object["kind"] = QString::fromStdString(edit.kind);
    return object;
}
TerrainEditPtr decodeTerrainEdit(const QJsonValue &value) {
    auto entry = object(value);
    auto edit = std::make_shared<TerrainEdit>();
    edit->id = std::uint32_t(integer(entry["id"], 1, 4294967295.0));
    edit->kind = text(entry["kind"]);
    if (edit->kind.size() > TerrainLimits::editKindBytes)
        throw std::runtime_error("Unknown terrain edit kind");
    const auto *codec = codecOf(edit->kind);
    if (!codec || !findTerrainEditKind(edit->kind))
        throw std::runtime_error("Unknown terrain edit kind");
    entry.remove("id");
    entry.remove("kind");
    edit->data = codec->decode(entry);
    if (!edit->data)
        throw std::runtime_error("Invalid terrain edit");
    return edit;
}
std::vector<TerrainEditPtr> decodeTerrainEdits(const QJsonValue &value) {
    const auto list = array(value);
    // The count is checked before any entry is decoded.
    if (list.isEmpty())
        throw std::runtime_error("An empty terrain edit list is written as no list");
    if (std::size_t(list.size()) > TerrainLimits::edits)
        throw std::runtime_error("Terrain edit list exceeds 4,096 entries");
    std::vector<TerrainEditPtr> edits;
    edits.reserve(std::size_t(list.size()));
    std::size_t bytes = 0;
    std::set<std::uint32_t> ids;
    for (const auto &entry : list) {
        edits.push_back(decodeTerrainEdit(entry));
        if (!ids.insert(edits.back()->id).second)
            throw std::runtime_error("Terrain edit ids must be unique");
        bytes += terrainEditEntryBytes(*edits.back());
        if (bytes > TerrainLimits::editBytes)
            throw std::runtime_error("Terrain edit list exceeds 8 MiB");
    }
    return edits;
}
QJsonObject encodeTerrainTileMarker(const TerrainTileKey &key) {
    return {{"key", QJsonArray{double(key.row), double(key.column), QString::fromStdString(key.path),
                               double(key.part)}}};
}
TerrainTileKey decodeTerrainTileMarker(const QJsonValue &value, bool *legacy) {
    if (value == QJsonValue(true)) {
        if (!legacy)
            throw std::runtime_error("A terrain tile marker needs its key");
        *legacy = true;
        return {};
    }
    const auto marker = object(value);
    exactFields(marker, {"key"}, "terrain tile marker");
    const auto key = array(marker["key"]);
    if (key.size() != 4)
        throw std::runtime_error("A terrain tile key is [row, column, path, part]");
    const double limit = double(terrainTileKeyLimit);
    TerrainTileKey result;
    result.row = std::int64_t(integer(key[0], -limit, limit));
    result.column = std::int64_t(integer(key[1], -limit, limit));
    result.path = text(key[2]);
    if (result.path.size() > terrainTileMaxDepth ||
        std::any_of(result.path.begin(), result.path.end(),
                    [](char c) { return c < '0' || c > '3'; }))
        throw std::runtime_error("Invalid terrain tile key path");
    result.part = std::uint32_t(integer(key[3], 0, 9999));
    if (legacy)
        *legacy = false;
    return result;
}
QJsonObject encodeTerrainRecord(const TerrainRecord &record) {
    validateTerrainRecord(record);
    QJsonArray sources;
    for (const auto &source : record.sources) {
        QJsonArray points;
        for (const auto point : source.points)
            points.append(QJsonArray{point.x, point.y, point.z});
        sources.append(QJsonObject{{"id", double(source.id)},
                                   {"role", roleName(source.role)},
                                   {"reference", QString::fromStdString(source.reference)},
                                   {"points", points}});
    }
    const auto &parameters = record.parameters;
    QJsonObject encoded{{"sources", sources},
            {"parameters", QJsonObject{{"boundary", boundaryName(parameters.boundary)},
                                       {"maxEdgeLength", parameters.maxEdgeLength},
                                       {"flipFlatTriangles", parameters.flipFlatTriangles},
                                       {"steinerFill", parameters.steinerFill}}},
            {"generator", QString::fromStdString(record.generator)},
            {"provenance",
             QJsonObject{{"sha256", QString::fromStdString(record.provenance.sha256)},
                         {"units", QString::fromStdString(record.provenance.units)},
                         {"metresPerUnit", record.provenance.metresPerUnit}}},
            {"meshDigest", QString::fromStdString(record.meshDigest)},
            {"tiling", QJsonObject{{"origin", QJsonArray{record.tiling.originX,
                                                         record.tiling.originY}},
                                   {"cell", record.tiling.cell}}}};
    if (record.nextEditId > 1)
        encoded["nextEdit"] = double(record.nextEditId);
    if (!record.edits.empty()) {
        QJsonArray edits;
        for (const auto &edit : record.edits)
            edits.append(encodeTerrainEdit(*edit));
        encoded["edits"] = edits;
    }
    // R095.e2 display settings: optional, each field written only when it differs from its
    // default, and the object only when one does, so default records encode as before.
    const TerrainDisplay defaults;
    QJsonObject display;
    if (record.display.contours != defaults.contours)
        display["contours"] = record.display.contours;
    if (record.display.contourInterval != defaults.contourInterval)
        display["contourInterval"] = record.display.contourInterval;
    if (record.display.shading != defaults.shading)
        display["shading"] = shadingName(record.display.shading);
    if (!display.isEmpty())
        encoded["display"] = display;
    return encoded;
}
TerrainRecord decodeTerrainRecord(const QJsonValue &value, bool *legacy) {
    const auto root = object(value);
    // R096.a: `tiling` is required; its absence marks the R095 development form.
    const bool old = !root.contains("tiling");
    if (old && !legacy)
        throw std::runtime_error("Missing field in terrain record");
    QStringList fields{"sources", "parameters", "generator", "provenance", "meshDigest"};
    if (!old)
        fields.append("tiling");
    if (root.contains("display"))
        fields.append("display");
    if (!old && root.contains("edits"))
        fields.append("edits");
    if (!old && root.contains("nextEdit"))
        fields.append("nextEdit");
    exactFields(root, fields, "terrain record");
    TerrainRecord record;
    TerrainSources::Vector decodedSources;
    const auto sources = array(root["sources"]);
    if (size_t(sources.size()) > TerrainLimits::sources)
        throw std::runtime_error("Terrain record has too many sources");
    size_t points = 0;
    for (const auto &entry : sources) {
        const auto source = object(entry);
        if (old)
            exactFields(source, {"role", "reference", "points"}, "terrain source");
        else
            exactFields(source, {"id", "role", "reference", "points"}, "terrain source");
        TerrainSource decoded;
        if (!old)
            decoded.id = std::uint32_t(integer(source["id"], 1, 4294967295.0));
        decoded.role = role(source["role"]);
        decoded.reference = text(source["reference"]);
        const auto values = array(source["points"]);
        points += size_t(values.size());
        if (points > TerrainLimits::sourceVertices)
            throw std::runtime_error("Terrain sources exceed 250,000 vertices");
        decoded.points.reserve(size_t(values.size()));
        for (const auto &point : values) {
            const auto xyz = array(point);
            if (xyz.size() != 3)
                throw std::runtime_error("Terrain source point requires three coordinates");
            decoded.points.push_back({number(xyz[0]), number(xyz[1]), number(xyz[2])});
        }
        decodedSources.push_back(std::move(decoded));
    }
    record.sources = std::move(decodedSources);
    const auto parameters = object(root["parameters"]);
    exactFields(parameters, {"boundary", "maxEdgeLength", "flipFlatTriangles", "steinerFill"},
                "terrain parameters");
    record.parameters.boundary = boundary(parameters["boundary"]);
    record.parameters.maxEdgeLength = number(parameters["maxEdgeLength"]);
    record.parameters.flipFlatTriangles = flag(parameters["flipFlatTriangles"]);
    record.parameters.steinerFill = flag(parameters["steinerFill"]);
    record.generator = text(root["generator"]);
    const auto provenance = object(root["provenance"]);
    exactFields(provenance, {"sha256", "units", "metresPerUnit"}, "terrain provenance");
    record.provenance.sha256 = text(provenance["sha256"]);
    record.provenance.units = text(provenance["units"]);
    record.provenance.metresPerUnit = number(provenance["metresPerUnit"]);
    record.meshDigest = text(root["meshDigest"]);
    if (root.contains("display")) {
        const auto display = object(root["display"]);
        for (const auto &key : display.keys())
            if (key != "contours" && key != "contourInterval" && key != "shading")
                throw std::runtime_error("Unexpected fields in terrain display");
        if (display.isEmpty())
            throw std::runtime_error("An empty terrain display is written as no display");
        if (display.contains("contours"))
            record.display.contours = flag(display["contours"]);
        if (display.contains("contourInterval"))
            record.display.contourInterval = number(display["contourInterval"]);
        if (display.contains("shading"))
            record.display.shading = shading(display["shading"]);
    }
    if (old) {
        *legacy = true;
        return record; // Derived and validated by the document decoder.
    }
    if (legacy)
        *legacy = false;
    const auto tiling = object(root["tiling"]);
    exactFields(tiling, {"origin", "cell"}, "terrain tiling");
    const auto origin = array(tiling["origin"]);
    if (origin.size() != 2)
        throw std::runtime_error("A terrain tiling origin is [x, y]");
    record.tiling = {number(origin[0]), number(origin[1]), number(tiling["cell"])};
    if (root.contains("edits"))
        record.edits = decodeTerrainEdits(root["edits"]);
    // Written only above its default 1; must exceed every entry's ID (validateTerrainRecord).
    if (root.contains("nextEdit"))
        record.nextEditId = std::uint32_t(integer(root["nextEdit"], 2, 4294967295.0));
    validateTerrainRecord(record);
    return record;
}
QJsonValue encodeSiteOrigin(const std::optional<SiteOrigin> &origin) {
    if (!origin)
        return QJsonValue::Null;
    validateSiteOrigin(*origin);
    return QJsonObject{
        {"easting", origin->easting},
        {"northing", origin->northing},
        {"elevation", origin->elevation},
        {"crs", QString::fromStdString(origin->crs)},
        {"established",
         QJsonObject{{"source", QString::fromStdString(origin->established.source)},
                     {"sha256", QString::fromStdString(origin->established.sha256)}}}};
}
std::optional<SiteOrigin> decodeSiteOrigin(const QJsonValue &value) {
    if (value.isNull())
        return std::nullopt;
    const auto root = object(value);
    exactFields(root, {"easting", "northing", "elevation", "crs", "established"}, "site origin");
    const auto established = object(root["established"]);
    exactFields(established, {"source", "sha256"}, "site provenance");
    SiteOrigin origin{number(root["easting"]),
                      number(root["northing"]),
                      number(root["elevation"]),
                      text(root["crs"]),
                      {text(established["source"]), text(established["sha256"])}};
    validateSiteOrigin(origin);
    return origin;
}
QByteArray encodeTerrainMesh(const std::map<Id, BodyPtr> &bodies) {
    std::vector<const Body *> tiles;
    for (const auto &[id, body] : bodies)
        if (body->terrainTile)
            tiles.push_back(body.get());
    if (tiles.empty())
        return {};
    QByteArray out = meshMagic;
    append32(out, std::uint32_t(tiles.size()));
    std::string payload;
    for (const auto *tile : tiles) {
        append64(out, tile->id);
        append32(out, std::uint32_t(tile->surface.vertices.size()));
        append32(out, std::uint32_t(tile->surface.faces.size()));
        std::string bytes;
        appendTerrainTileBytes(bytes, *tile);
        payload.append(bytes, 8, std::string::npos); // The counts are in the header.
    }
    out.append(payload.data(), qsizetype(payload.size()));
    if (size_t(out.size()) > terrainMeshChunkLimit)
        throw std::runtime_error("Terrain mesh exceeds its storage limit");
    return out;
}
std::vector<TerrainMeshTile> decodeTerrainMesh(const QByteArray &bytes) {
    const auto size = std::uint64_t(bytes.size());
    if (size > terrainMeshChunkLimit || size < 12 || !bytes.startsWith(meshMagic))
        throw std::runtime_error("Invalid terrain mesh chunk header");
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    const auto tileCount = qFromLittleEndian<quint32>(data + 8);
    if (!tileCount || tileCount > DocumentLimits::unique.bodies ||
        size < 12 + std::uint64_t(tileCount) * 16)
        throw std::runtime_error("Invalid terrain mesh tile count");
    // Every count is checked against the limits, and the exact length against the
    // counts, before any tile is allocated.
    struct Header {
        Id body;
        std::uint32_t vertices, triangles;
    };
    std::vector<Header> headers(tileCount);
    std::uint64_t expected = 12 + std::uint64_t(tileCount) * 16, triangles = 0;
    for (std::uint32_t i = 0; i < tileCount; ++i) {
        const auto *entry = data + 12 + std::size_t(i) * 16;
        Header header{qFromLittleEndian<quint64>(entry), qFromLittleEndian<quint32>(entry + 8),
                      qFromLittleEndian<quint32>(entry + 12)};
        if (!header.body || (i && header.body <= headers[i - 1].body))
            throw std::runtime_error("Terrain mesh tiles must be in ascending body ID order");
        if (!header.triangles || header.triangles > TerrainLimits::tileTriangles ||
            header.vertices < 3 || header.vertices > 3 * std::uint64_t(header.triangles))
            throw std::runtime_error("Terrain mesh tile counts exceed tile limits");
        triangles += header.triangles;
        checkTerrainLimit(triangles, "Document load");
        expected += std::uint64_t(header.vertices) * 24 + std::uint64_t(header.triangles) * 12;
        headers[i] = header;
    }
    if (expected != size)
        throw std::runtime_error("Terrain mesh chunk length disagrees with its counts");
    std::vector<TerrainMeshTile> tiles;
    tiles.reserve(tileCount);
    const auto *cursor = data + 12 + std::size_t(tileCount) * 16;
    auto f64 = [&] {
        const auto bits = qFromLittleEndian<quint64>(cursor);
        cursor += 8;
        double value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    };
    for (const auto &header : headers) {
        TerrainMeshTile tile;
        tile.body = header.body;
        tile.vertices.reserve(header.vertices);
        for (std::uint32_t v = 0; v < header.vertices; ++v) {
            const auto x = f64(), y = f64(), z = f64();
            const Vec3 point{x, y, z};
            checkPoint(point);
            tile.vertices.push_back(point);
        }
        tile.triangles.reserve(header.triangles);
        for (std::uint32_t t = 0; t < header.triangles; ++t) {
            std::array<std::uint32_t, 3> triangle{};
            for (auto &index : triangle) {
                index = qFromLittleEndian<quint32>(cursor);
                cursor += 4;
                if (index >= header.vertices)
                    throw std::runtime_error("Terrain mesh triangle index is out of range");
            }
            if (triangle[0] == triangle[1] || triangle[1] == triangle[2] ||
                triangle[0] == triangle[2])
                throw std::runtime_error("Terrain mesh triangle repeats a vertex");
            tile.triangles.push_back(triangle);
        }
        tiles.push_back(std::move(tile));
    }
    return tiles;
}
} // namespace sketchy
