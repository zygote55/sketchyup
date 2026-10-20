#include "io/terrain_io.hpp"
#include "core/document_limits.hpp"
#include "core/terrain.hpp"
#include "core/terrain_edit.hpp"
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
std::vector<TerrainEditCodec> &codecs() {
    static std::vector<TerrainEditCodec> registered;
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
        bytes += edits.back()->data->bytes();
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
