#include "io/terrain_io.hpp"
#include "core/document_limits.hpp"
#include "core/terrain.hpp"
#include "io/native_limits.hpp"
#include <QJsonArray>
#include <QtEndian>
#include <cmath>
#include <cstring>
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
QJsonObject encodeTerrainRecord(const TerrainRecord &record) {
    validateTerrainRecord(record);
    QJsonArray sources;
    for (const auto &source : record.sources) {
        QJsonArray points;
        for (const auto point : source.points)
            points.append(QJsonArray{point.x, point.y, point.z});
        sources.append(QJsonObject{{"role", roleName(source.role)},
                                   {"reference", QString::fromStdString(source.reference)},
                                   {"points", points}});
    }
    const auto &parameters = record.parameters;
    return {{"sources", sources},
            {"parameters", QJsonObject{{"boundary", boundaryName(parameters.boundary)},
                                       {"maxEdgeLength", parameters.maxEdgeLength},
                                       {"flipFlatTriangles", parameters.flipFlatTriangles},
                                       {"steinerFill", parameters.steinerFill}}},
            {"generator", QString::fromStdString(record.generator)},
            {"provenance",
             QJsonObject{{"sha256", QString::fromStdString(record.provenance.sha256)},
                         {"units", QString::fromStdString(record.provenance.units)},
                         {"metresPerUnit", record.provenance.metresPerUnit}}},
            {"meshDigest", QString::fromStdString(record.meshDigest)}};
}
TerrainRecord decodeTerrainRecord(const QJsonValue &value) {
    const auto root = object(value);
    exactFields(root, {"sources", "parameters", "generator", "provenance", "meshDigest"},
                "terrain record");
    TerrainRecord record;
    const auto sources = array(root["sources"]);
    if (size_t(sources.size()) > TerrainLimits::sources)
        throw std::runtime_error("Terrain record has too many sources");
    size_t points = 0;
    for (const auto &entry : sources) {
        const auto source = object(entry);
        exactFields(source, {"role", "reference", "points"}, "terrain source");
        TerrainSource decoded;
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
        record.sources.push_back(std::move(decoded));
    }
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
