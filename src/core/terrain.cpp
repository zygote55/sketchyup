#include "core/terrain.hpp"
#include "core/terrain_source_edit.hpp"
#include "core/terrain_sculpt.hpp"
#include "core/terrain_stamp.hpp"
#include "core/terrain_drape.hpp"
#include "core/component_parameter_records.hpp"
#include "core/document_limits.hpp"
#include "core/terrain_edit.hpp"
#include "core/terrain_edit_mesh.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <utility>
namespace sketchy {
static_assert(TerrainLimits::triangles == terrainTriangleLimit);
// terrainHistoryMessage names the budget.
static_assert(Document::terrainHistoryLimit == 512u * 1024 * 1024);
namespace {
[[noreturn]] void refuse(const char *code, const std::string &message) {
    throw TerrainError(code, message);
}
[[noreturn]] void protectedTarget() { refuse("UNSUPPORTED_TARGET", terrainProtectedMessage); }
// Little-endian; on a little-endian host the bytes are appended as they are (R096.a: the
// digest's input at 250k triangles is about 7.5 MB, built on every edit).
void u32(std::string &out, std::uint32_t value) {
    if constexpr (std::endian::native == std::endian::little) {
        char bytes[4];
        std::memcpy(bytes, &value, 4);
        out.append(bytes, 4);
    } else
        for (int i = 0; i < 4; ++i)
            out.push_back(char((value >> (8 * i)) & 0xFF));
}
void f64(std::string &out, double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    if constexpr (std::endian::native == std::endian::little) {
        char bytes[8];
        std::memcpy(bytes, &bits, 8);
        out.append(bytes, 8);
    } else
        for (int i = 0; i < 8; ++i)
            out.push_back(char((bits >> (8 * i)) & 0xFF));
}
bool boundedText(const std::string &text, std::size_t bytes) {
    return text.size() <= bytes &&
           std::none_of(text.begin(), text.end(), [](char c) {
               return static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
           });
}
bool sha256Text(const std::string &text) {
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
const Body *bodyOf(const Document &doc, Id id) {
    const auto found = doc.bodies().find(id);
    return found == doc.bodies().end() ? nullptr : found->second.get();
}
// Every field a generic tool could change that defines the tile's mesh, its frame
// or its membership. Paint, texture mapping, tags, names, properties, visibility and
// lock are deliberately not here.
bool protectedChange(const Body &before, const Body &after) {
    return before.surface != after.surface || before.topology != after.topology ||
           before.edgeAppearances != after.edgeAppearances || before.curves != after.curves ||
           before.guides != after.guides || before.transform != after.transform ||
           before.parent != after.parent || before.kind != after.kind ||
           before.terrainTile != after.terrainTile || before.tileKey != after.tileKey ||
           before.textSource != after.textSource ||
           before.referenceImage != after.referenceImage || before.terrain != after.terrain;
}
std::string limitMessage(std::size_t triangles) {
    return "Terrain exceeds the document terrain limit: terrain triangles would be " +
           groupedCount(triangles) + " of " + groupedCount(TerrainLimits::triangles);
}
} // namespace
void validateTerrainRecord(const TerrainRecord &record) {
    if (record.sources.size() > TerrainLimits::sources)
        throw std::runtime_error("Terrain record has too many sources");
    std::size_t points = 0;
    std::set<std::uint32_t> ids;
    std::uint32_t maxBaseId = 0;
    for (const auto &source : record.sources) {
        maxBaseId = std::max(maxBaseId, source.id);
        if (!source.id || !ids.insert(source.id).second)
            throw std::runtime_error("Terrain source ids must be unique and not zero");
        // R096.b: the ids from terrainSplitSourceIdFloor up name the pieces of split lines.
        if (source.id >= terrainSplitSourceIdFloor)
            throw std::runtime_error("Terrain source ids are below 3,221,225,472");
        if (!boundedText(source.reference, TerrainLimits::referenceBytes))
            throw std::runtime_error("Invalid terrain source reference");
        const std::size_t minimum =
            source.role == TerrainSourceRole::Boundary || source.role == TerrainSourceRole::Hole
                ? 3
            : source.role == TerrainSourceRole::Points ? 1
                                                        : 2;
        if (source.points.size() < minimum)
            throw std::runtime_error("Terrain source has too few points for its role");
        points += source.points.size();
        if (points > TerrainLimits::sourceVertices)
            throw std::runtime_error("Terrain sources exceed 250,000 vertices");
        for (const auto point : source.points)
            checkPoint(point);
    }
    const auto &parameters = record.parameters;
    if (!std::isfinite(parameters.maxEdgeLength) || parameters.maxEdgeLength < 0 ||
        parameters.maxEdgeLength > 2 * coordinateLimit ||
        (parameters.boundary != TerrainBoundaryMode::ConvexHull &&
         parameters.boundary != TerrainBoundaryMode::MaxEdgeLength &&
         parameters.boundary != TerrainBoundaryMode::Boundary))
        throw std::runtime_error("Invalid terrain triangulation parameters");
    if (record.generator.empty() || !boundedText(record.generator, TerrainLimits::generatorBytes))
        throw std::runtime_error("Invalid terrain generator identity");
    const auto &provenance = record.provenance;
    if ((!provenance.sha256.empty() && !sha256Text(provenance.sha256)) ||
        provenance.units.empty() || !boundedText(provenance.units, TerrainLimits::unitBytes) ||
        !std::isfinite(provenance.metresPerUnit) || provenance.metresPerUnit <= 0 ||
        provenance.metresPerUnit > 1e6)
        throw std::runtime_error("Invalid terrain import provenance");
    validateTerrainDisplay(record.display);
    if (!sha256Text(record.meshDigest))
        throw std::runtime_error("Invalid terrain mesh digest");
    // R096.a: the stored tile grid and the edit list.
    const auto &grid = record.tiling;
    if (!std::isfinite(grid.originX) || !std::isfinite(grid.originY) ||
        std::abs(grid.originX) > coordinateLimit || std::abs(grid.originY) > coordinateLimit ||
        !std::isfinite(grid.cell) || !(grid.cell > 0) || grid.cell > 4 * coordinateLimit)
        throw std::runtime_error("Invalid terrain tile grid");
    if (record.edits.size() > TerrainLimits::edits)
        throw std::runtime_error("Terrain edit list exceeds 4,096 entries");
    std::set<std::uint32_t> editIds;
    if (!record.nextEditId)
        throw std::runtime_error("Invalid terrain next edit ID");
    for (const auto &edit : record.edits) {
        if (!edit || !edit->data || !edit->id || !editIds.insert(edit->id).second)
            throw std::runtime_error("Terrain edit ids must be unique and not zero");
        if (edit->id >= record.nextEditId)
            throw std::runtime_error("A terrain edit ID is not below the next edit ID");
        if (edit->kind.empty() || !boundedText(edit->kind, TerrainLimits::editKindBytes) ||
            !findTerrainEditKind(edit->kind))
            throw std::runtime_error("Unknown terrain edit kind");
        // R096.c: an added source's id is the largest base id plus the entry's id, so a file
        // cannot claim an id a later entry will take.
        if (edit->kind == terrainAddSourceKind) {
            const auto *data = dynamic_cast<const TerrainSourceEditData *>(edit->data.get());
            if (!data || std::uint64_t(data->source) != std::uint64_t(maxBaseId) + edit->id)
                throw std::runtime_error("An added terrain source's id must be the largest base "
                                         "source id plus its change's id");
        }
    }
    // R096.b: a sculpt's recorded pieces take ids from its own block.
    for (const auto &edit : record.edits)
        if (edit->kind == terrainSculptKind || edit->kind == terrainSmoothKind) {
            const auto *data = dynamic_cast<const TerrainSculptData *>(edit->data.get());
            if (!data)
                throw std::runtime_error("Invalid terrain sculpt entry");
            const auto high = terrainSplitSourceId(edit->id, 0),
                       low = terrainSplitSourceId(edit->id, terrainSculptPiecesPerEdit - 1);
            for (const auto &[parent, id] : data->pieces)
                if (!high || id > *high || id < *low)
                    throw std::runtime_error("A terrain sculpt's pieces take ids from its own "
                                             "block (ADR 0108, R096.b)");
        }
    // R096.d: a pad's fields, and its recorded pieces from its own block (ordinals 0 and 1 name
    // its outline and daylight line, so pieces start below them).
    for (const auto &edit : record.edits)
        if (edit->kind == terrainStampKind) {
            const auto *data = dynamic_cast<const TerrainStampData *>(edit->data.get());
            if (!data)
                throw std::runtime_error("Invalid terrain stamp entry");
            try {
                validateTerrainStampData(*data);
            } catch (const std::invalid_argument &error) {
                throw std::runtime_error(error.what());
            }
            const auto high = terrainSplitSourceId(edit->id, 2),
                       low = terrainSplitSourceId(edit->id, terrainSculptPiecesPerEdit - 1);
            for (const auto &[parent, id] : data->pieces)
                if (!high || id > *high || id < *low)
                    throw std::runtime_error("A terrain stamp's pieces take ids from its own "
                                             "block (ADR 0108, R096.d)");
        }
    // R096.e: a drape's fields, and the paint regions' total band bound (a centroid test reads at
    // most that many edges).
    for (const auto &edit : record.edits)
        if (edit->kind == terrainDrapeKind) {
            const auto *data = dynamic_cast<const TerrainDrapeData *>(edit->data.get());
            if (!data)
                throw std::runtime_error("Invalid terrain drape entry");
            try {
                validateTerrainDrapeData(*data);
            } catch (const std::invalid_argument &error) {
                throw std::runtime_error(error.what());
            }
        }
    if (terrainPaintBandEdges(record) > terrainDrapePaintBandEdges)
        throw std::runtime_error("A terrain's paint regions have more than 4,096 edges in their "
                                 "busiest bands in all");
    if (terrainEditBytes(record.edits) > TerrainLimits::editBytes)
        throw std::runtime_error("Terrain edit list exceeds 8 MiB");
}
void validateTerrainDisplay(const TerrainDisplay &display) {
    if (!std::isfinite(display.contourInterval) ||
        display.contourInterval < terrainContourIntervalMin ||
        display.contourInterval > terrainContourIntervalMax ||
        (display.shading != TerrainShading::Material &&
         display.shading != TerrainShading::ElevationBands))
        throw TerrainError("INVALID_REQUEST",
                           "The contour interval is 0.01 to 1,000 m and shading is material or "
                           "elevation bands");
}
namespace {
std::size_t recordOwnBytes(const TerrainRecord &record) {
    return sizeof(TerrainRecord) + record.generator.size() + record.provenance.sha256.size() +
           record.provenance.units.size() + record.meshDigest.size() +
           record.edits.size() * sizeof(TerrainEditPtr);
}
std::size_t sourceBlockBytes(const TerrainSources &sources) {
    std::size_t bytes = 64;
    for (const auto &source : sources)
        bytes += sizeof(TerrainSource) + 64 + source.reference.size() +
                 source.points.size() * sizeof(Vec3);
    return bytes;
}
} // namespace
std::size_t terrainEditEntryBytes(const TerrainEdit &edit) {
    return sizeof(TerrainEdit) + 64 + edit.kind.size() + (edit.data ? edit.data->bytes() : 0);
}
std::size_t terrainEditBytes(const std::vector<TerrainEditPtr> &edits) {
    std::size_t bytes = 0;
    for (const auto &edit : edits)
        if (edit)
            bytes += terrainEditEntryBytes(*edit);
    return bytes;
}
std::size_t terrainRecordBytes(const TerrainRecord &record) {
    return recordOwnBytes(record) + sourceBlockBytes(record.sources) +
           terrainEditBytes(record.edits);
}
std::size_t terrainRecordChangeBytes(const TerrainRecord *before, const TerrainRecord *after) {
    if (!before && !after)
        return 0;
    if (!before || !after)
        return terrainRecordBytes(before ? *before : *after);
    std::size_t bytes = recordOwnBytes(*before) + recordOwnBytes(*after);
    if (before->sources.block() != after->sources.block())
        bytes += sourceBlockBytes(before->sources) + sourceBlockBytes(after->sources);
    // Entries are compared by identity: a shared entry is held once, whoever holds it.
    std::set<const TerrainEdit *> beforeEntries, afterEntries;
    for (const auto &edit : before->edits)
        beforeEntries.insert(edit.get());
    for (const auto &edit : after->edits)
        afterEntries.insert(edit.get());
    for (const auto &edit : before->edits)
        if (edit && !afterEntries.contains(edit.get()))
            bytes += terrainEditEntryBytes(*edit);
    for (const auto &edit : after->edits)
        if (edit && !beforeEntries.contains(edit.get()))
            bytes += terrainEditEntryBytes(*edit);
    return bytes;
}
void validateSiteOrigin(const SiteOrigin &origin) {
    for (const auto value : {origin.easting, origin.northing, origin.elevation})
        if (!std::isfinite(value) || std::abs(value) > siteCoordinateLimit)
            throw TerrainError("INVALID_REQUEST",
                               "Site origin coordinates must be finite and within 100,000 km");
    if (!boundedText(origin.crs, siteCrsBytes))
        throw TerrainError("INVALID_REQUEST",
                           "The CRS label is at most 64 bytes without control characters");
    if (!boundedText(origin.established.source, siteSourceBytes) ||
        (!origin.established.sha256.empty() && !sha256Text(origin.established.sha256)))
        throw TerrainError("INVALID_REQUEST", "Invalid site origin provenance");
}
TerrainTiling tileTerrainTriangles(const TerrainMesh &mesh, TerrainProgress *progress) {
    TerrainTiling result;
    const auto count = mesh.triangles.size();
    terrainCheckpoint(progress, TerrainStage::Tile, 0);
    if (!count)
        return result;
    std::vector<std::array<double, 2>> centroids(count);
    double minX = std::numeric_limits<double>::infinity(), minY = minX, maxX = -minX,
           maxY = -minX;
    for (std::size_t i = 0; i < count; ++i) {
        const auto &t = mesh.triangles[i];
        const auto a = mesh.vertices.at(t[0]), b = mesh.vertices.at(t[1]),
                   c = mesh.vertices.at(t[2]);
        centroids[i] = {(a.x + b.x + c.x) / 3, (a.y + b.y + c.y) / 3};
        minX = std::min(minX, centroids[i][0]);
        maxX = std::max(maxX, centroids[i][0]);
        minY = std::min(minY, centroids[i][1]);
        maxY = std::max(maxY, centroids[i][1]);
    }
    const double width = std::max(maxX - minX, 1e-6), height = std::max(maxY - minY, 1e-6);
    constexpr auto limit = TerrainLimits::tileTriangles;
    // Aim at half the limit on average, then halve the cell until no cell overflows.
    double cell = std::max(width, height) * 1.0001;
    if (count > limit)
        cell = std::min(cell, std::sqrt(width * height * double(limit / 2) / double(count)));
    using Key = std::pair<std::uint64_t, std::uint64_t>; // row, column
    std::map<Key, std::vector<std::uint32_t>> buckets;
    constexpr int attempts = 24;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        buckets.clear();
        std::size_t largest = 0;
        terrainCheckpoint(progress, TerrainStage::Tile, .1 + .03 * attempt);
        for (std::size_t i = 0; i < count; ++i) {
            if (progress && (i & 0x3fff) == 0)
                progress->check();
            const auto column = std::uint64_t(std::floor((centroids[i][0] - minX) / cell));
            const auto row = std::uint64_t(std::floor((centroids[i][1] - minY) / cell));
            auto &bucket = buckets[{row, column}];
            bucket.push_back(std::uint32_t(i));
            largest = std::max(largest, bucket.size());
        }
        // The cell is halved only for another attempt: after the last one the buckets (and the
        // stored grid) keep the cell they were built with, and clusters split into parts.
        if (largest <= limit || attempt + 1 == attempts)
            break;
        cell *= .5;
    }
    terrainCheckpoint(progress, TerrainStage::Tile, .9);
    result.cell = cell;
    result.originX = minX;
    result.originY = minY;
    // A cluster that the grid still cannot separate is split in triangle order; the
    // tiles remain a partition of the triangle set.
    for (auto &[key, bucket] : buckets)
        for (std::size_t start = 0, part = 0; start < bucket.size(); start += limit, ++part) {
            result.tiles.emplace_back(bucket.begin() + std::ptrdiff_t(start),
                                      bucket.begin() +
                                          std::ptrdiff_t(std::min(bucket.size(), start + limit)));
            result.keys.push_back({std::int64_t(key.first), std::int64_t(key.second), {},
                                   std::uint32_t(part)});
        }
    return result;
}
std::map<Id, EdgeAppearance> terrainTileEdgeAppearances(const Surface &surface,
                                                        const Topology &topology) {
    std::map<std::pair<Id, Id>, int> uses;
    for (const auto &[id, face] : surface.faces)
        for (const auto &loop : face.loops)
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const auto a = loop[i], b = loop[(i + 1) % loop.size()];
                ++uses[{std::min(a, b), std::max(a, b)}];
            }
    std::map<Id, EdgeAppearance> result;
    for (const auto &[id, edge] : topology.edges) {
        const auto found = uses.find({std::min(edge.a, edge.b), std::max(edge.a, edge.b)});
        if (found != uses.end() && found->second >= 2)
            result.emplace_hint(result.end(), id, EdgeAppearance{false, true, true});
    }
    return result;
}
Body makeTerrainTile(Id id, Id parent, std::string name, const std::vector<Vec3> &vertices,
                     const std::vector<std::array<std::uint32_t, 3>> &triangles) {
    if (triangles.empty() || triangles.size() > TerrainLimits::tileTriangles ||
        vertices.size() > 3 * triangles.size())
        throw std::runtime_error("A terrain tile holds 1 to 2,048 triangles");
    Body body;
    body.id = id;
    body.parent = parent;
    body.name = std::move(name);
    body.terrainTile = true;
    const Id count = vertices.size();
    for (std::size_t i = 0; i < vertices.size(); ++i)
        body.surface.vertices.emplace_hint(body.surface.vertices.end(), Id(i + 1), vertices[i]);
    for (std::size_t j = 0; j < triangles.size(); ++j) {
        const auto &t = triangles[j];
        if (t[0] >= count || t[1] >= count || t[2] >= count || t[0] == t[1] || t[1] == t[2] ||
            t[0] == t[2])
            throw std::runtime_error("Terrain triangle references an invalid vertex");
        const Id face = count + 1 + j;
        body.surface.faces.emplace_hint(
            body.surface.faces.end(), face,
            Face{face, {{Id(t[0]) + 1, Id(t[1]) + 1, Id(t[2]) + 1}}});
    }
    body.surface.nextId = count + triangles.size() + 1;
    body.topology = Topology::rebuild(body.surface, {});
    body.edgeAppearances = terrainTileEdgeAppearances(body.surface, body.topology);
    return body;
}
TerrainMesh terrainTileMesh(const Body &tile) {
    TerrainMesh mesh;
    mesh.vertices.reserve(tile.surface.vertices.size());
    for (const auto &[id, point] : tile.surface.vertices)
        mesh.vertices.push_back(point);
    mesh.triangles.reserve(tile.surface.faces.size());
    for (const auto &[id, face] : tile.surface.faces) {
        const auto &loop = face.loops.at(0);
        mesh.triangles.push_back({std::uint32_t(loop.at(0) - 1), std::uint32_t(loop.at(1) - 1),
                                  std::uint32_t(loop.at(2) - 1)});
    }
    return mesh;
}
void appendTerrainTileBytes(std::string &out, const Body &tile) {
    // The canonical mesh (terrainTileMesh) streamed without a copy.
    u32(out, std::uint32_t(tile.surface.vertices.size()));
    u32(out, std::uint32_t(tile.surface.faces.size()));
    for (const auto &[id, point] : tile.surface.vertices) {
        f64(out, point.x);
        f64(out, point.y);
        f64(out, point.z);
    }
    for (const auto &[id, face] : tile.surface.faces)
        for (const auto vertex : face.loops.at(0))
            u32(out, std::uint32_t(vertex - 1));
}
void validateTerrainBody(const Body &b) {
    if (b.kind == BodyKind::Terrain) {
        if (!b.terrain)
            throw std::runtime_error("A terrain body requires its terrain record");
        validateTerrainRecord(*b.terrain);
        if (!b.surface.vertices.empty() || !b.surface.faces.empty() || !b.surface.wires.empty() ||
            !b.topology.edges.empty() || !b.curves.empty() || !b.guides.empty() ||
            !b.faceColors.empty() || !b.faceMaterials.empty() || !b.faceTextureMappings.empty() ||
            !b.edgeAppearances.empty() || b.textSource || b.referenceImage || b.terrainTile)
            throw std::runtime_error("A terrain owns only its tiles");
    } else if (b.terrain)
        throw std::runtime_error("Only a terrain body carries a terrain record");
    if (!b.terrainTile) {
        if (b.tileKey != TerrainTileKey{})
            throw std::runtime_error("Only a terrain tile carries a tile key");
        return;
    }
    const auto &key = b.tileKey;
    if (key.row < -terrainTileKeyLimit || key.row > terrainTileKeyLimit ||
        key.column < -terrainTileKeyLimit || key.column > terrainTileKeyLimit ||
        key.path.size() > terrainTileMaxDepth || key.part >= 10000 ||
        std::any_of(key.path.begin(), key.path.end(), [](char c) { return c < '0' || c > '3'; }))
        throw std::runtime_error("Invalid terrain tile key");
    if (b.kind != BodyKind::Geometry || b.textSource || b.referenceImage)
        throw std::runtime_error("A terrain tile is ordinary geometry");
    if (!b.surface.wires.empty() || !b.curves.empty() || !b.guides.empty())
        throw std::runtime_error("A terrain tile holds triangles only");
    if (b.transform != Transform{})
        throw std::runtime_error("A terrain tile shares its terrain's frame");
    const auto vertices = b.surface.vertices.size(), faces = b.surface.faces.size();
    if (!faces || faces > TerrainLimits::tileTriangles || vertices > 3 * faces)
        throw std::runtime_error("A terrain tile holds 1 to 2,048 triangles");
    // Canonical implicit IDs: vertices 1..n, faces n+1..n+m.
    if (b.surface.vertices.begin()->first != 1 || b.surface.vertices.rbegin()->first != vertices ||
        b.surface.faces.begin()->first != vertices + 1 ||
        b.surface.faces.rbegin()->first != vertices + faces ||
        b.surface.nextId < vertices + faces + 1)
        throw std::runtime_error("A terrain tile uses its canonical vertex and face IDs");
    for (const auto &[id, face] : b.surface.faces)
        if (face.loops.size() != 1 || face.loops[0].size() != 3)
            throw std::runtime_error("A terrain tile holds triangles only");
}
void validateTerrainTileAppearance(const Body &tile) {
    if (tile.edgeAppearances != terrainTileEdgeAppearances(tile.surface, tile.topology))
        throw std::runtime_error("A terrain tile's edge appearance is implied by its mesh");
}
void checkTerrainParent(const Body &body, const Body *parent) {
    const bool terrainParent = parent && parent->kind == BodyKind::Terrain;
    if (body.terrainTile && !terrainParent)
        throw std::runtime_error("A terrain tile must be a child of its terrain");
    if (terrainParent && !body.terrainTile)
        throw std::runtime_error("A terrain owns only its tiles");
}
std::set<Id> terrainRecordChanges(const Edit &edit) {
    std::set<Id> result;
    for (const auto &change : edit.changes) {
        const bool before = change.before && change.before->kind == BodyKind::Terrain,
                   after = change.after && change.after->kind == BodyKind::Terrain;
        if ((before || after) &&
            (before != after || change.before->terrain != change.after->terrain))
            result.insert(change.id);
    }
    return result;
}
void checkTerrainChange(const Change &change, const std::set<Id> &recordChanges) {
    const bool wasTile = change.before && change.before->terrainTile,
               isTile = change.after && change.after->terrainTile;
    if (!wasTile && !isTile)
        return;
    if (wasTile && isTile && !protectedChange(*change.before, *change.after))
        return;
    if ((wasTile && !recordChanges.contains(change.before->parent)) ||
        (isTile && !recordChanges.contains(change.after->parent)))
        protectedTarget();
}
void requireOrdinaryGeometry(const Document &doc, Id body) {
    if (const auto *record = bodyOf(doc, body);
        record && (record->terrainTile || record->kind == BodyKind::Terrain))
        protectedTarget();
}
bool isTerrain(const Document &doc, Id body) {
    const auto *record = bodyOf(doc, body);
    return record && record->kind == BodyKind::Terrain;
}
std::vector<Id> terrainTiles(const Document &doc, Id terrain) {
    std::vector<Id> tiles;
    for (const auto &[id, body] : doc.bodies())
        if (body->terrainTile && body->parent == terrain)
            tiles.push_back(id);
    return tiles;
}
std::size_t terrainTriangleCount(const Document &doc, Id terrain) {
    std::size_t count = 0;
    for (const auto id : terrainTiles(doc, terrain))
        count += doc.bodies().at(id)->surface.faces.size();
    return count;
}
std::string terrainMeshDigest(const std::vector<const Body *> &input) {
    auto tiles = input;
    std::stable_sort(tiles.begin(), tiles.end(),
                     [](const Body *a, const Body *b) { return a->tileKey < b->tileKey; });
    std::string bytes;
    std::size_t size = 4;
    for (const auto *tile : tiles)
        size += 8 + tile->surface.vertices.size() * 24 + tile->surface.faces.size() * 12;
    bytes.reserve(size);
    u32(bytes, std::uint32_t(tiles.size()));
    for (const auto *tile : tiles)
        appendTerrainTileBytes(bytes, *tile);
    return digestHex(sha256Digest(bytes));
}
std::vector<Id> terrainTilesInKeyOrder(const Document &doc, Id terrain) {
    auto tiles = terrainTiles(doc, terrain);
    std::stable_sort(tiles.begin(), tiles.end(), [&](Id a, Id b) {
        return doc.bodies().at(a)->tileKey < doc.bodies().at(b)->tileKey;
    });
    return tiles;
}
std::string terrainMeshDigest(const Document &doc, Id terrain) {
    std::vector<const Body *> tiles;
    for (const auto id : terrainTiles(doc, terrain))
        tiles.push_back(doc.bodies().at(id).get());
    return terrainMeshDigest(tiles);
}
bool terrainChangedSinceGenerated(const Document &doc, Id terrain) {
    const auto *record = bodyOf(doc, terrain);
    if (!record || !record->terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    return terrainMeshDigest(doc, terrain) != record->terrain->meshDigest;
}
namespace {
// The mesh checks shared by creation and regeneration; `existing` is the document's
// terrain triangles outside the terrain being built.
void checkTerrainMesh(const TerrainMesh &mesh, std::size_t existing) {
    if (mesh.triangles.empty())
        refuse("INVALID_REQUEST", "A terrain needs at least one triangle");
    if (mesh.triangles.size() > TerrainLimits::triangles ||
        existing + mesh.triangles.size() > TerrainLimits::triangles)
        refuse("LIMIT_EXCEEDED", limitMessage(existing + mesh.triangles.size()));
    if (mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max())
        refuse("LIMIT_EXCEEDED", "Terrain mesh has too many vertices");
    for (const auto point : mesh.vertices)
        checkPoint(point);
    for (const auto &triangle : mesh.triangles)
        for (const auto index : triangle)
            if (index >= mesh.vertices.size())
                refuse("INVALID_REQUEST", "Terrain triangle references a missing vertex");
}
// The tile bodies of `set` (key order), the k-th with body ID ids[k], its key and its name.
std::vector<std::shared_ptr<Body>> terrainTileBodies(const TerrainTileSet &set,
                                                     const std::vector<Id> &ids, Id terrain,
                                                     MaterialSides materials) {
    std::vector<std::shared_ptr<Body>> tiles;
    tiles.reserve(set.tiles.size());
    for (std::size_t t = 0; t < set.tiles.size(); ++t) {
        const auto &content = *set.tiles[t];
        auto tile = std::make_shared<Body>(
            makeTerrainTile(ids.at(t), terrain, content.name, content.vertices, content.triangles));
        tile->tileKey = content.key;
        tile->materials = materials;
        tiles.push_back(std::move(tile));
    }
    return tiles;
}
std::string tilesDigest(const std::vector<std::shared_ptr<Body>> &tiles) {
    std::vector<const Body *> views;
    for (const auto &tile : tiles)
        views.push_back(tile.get());
    return terrainMeshDigest(views);
}
// R095.e2: the "Terrain" material, reused when the document has one of that name; otherwise
// `created` receives the material change that adds it in the same edit.
Id terrainMaterial(const Document &doc, std::optional<MaterialChange> &created) {
    for (const auto &[id, material] : doc.materials())
        if (material->name == terrainMaterialName)
            return id;
    if (doc.materials().size() >= 1024)
        return 0; // The material table is full: the tiles keep their plain colour.
    const auto id = doc.nextMaterialId();
    created = MaterialChange{id, nullptr,
                             std::make_shared<MaterialRecord>(MaterialRecord{
                                 id, terrainMaterialName, terrainMaterialColor, 1, 0})};
    return id;
}
// The tiles' shared default material sides: what Regenerate gives the new tiles. Tiles that
// all share one default keep it; mixed tiles return to the "Terrain" material.
std::optional<MaterialSides> sharedTileMaterials(const Document &doc, Id terrain) {
    std::optional<MaterialSides> shared;
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &sides = doc.bodies().at(id)->materials;
        if (shared && *shared != sides)
            return std::nullopt;
        shared = sides;
    }
    return shared;
}
// The record and tiles of a new terrain as one edit, not yet applied.
std::pair<Edit, TerrainCreated> terrainEdit(Document &doc, const TerrainMesh &mesh,
                                            TerrainRecord record, Id parent, std::string name,
                                            std::string label) {
    checkTerrainMesh(mesh, doc.materializedTotals().terrainTriangles);
    if (parent) {
        const auto *owner = bodyOf(doc, parent);
        if (!owner || owner->kind != BodyKind::Group)
            refuse("INVALID_REQUEST", "A terrain's parent must be a group");
    }
    if (name.empty() || name.size() > 1024)
        refuse("INVALID_REQUEST", "Invalid terrain name");
    const auto set = terrainTileSet(mesh);
    const Id terrain = doc.nextId();
    if (terrain > UINT64_MAX - set.tiles.size() - 1)
        refuse("LIMIT_EXCEEDED", "Entity identity space exhausted");
    std::vector<Id> ids;
    for (std::size_t t = 0; t < set.tiles.size(); ++t)
        ids.push_back(terrain + 1 + t);
    std::optional<MaterialChange> created;
    const auto material = terrainMaterial(doc, created);
    auto tiles = terrainTileBodies(set, ids, terrain, {material, material});
    record.meshDigest = tilesDigest(tiles);
    record.tiling = set.grid;
    assignTerrainSourceIds(record);
    validateTerrainRecord(record);
    auto container = std::make_shared<Body>();
    container->id = terrain;
    container->name = std::move(name);
    container->parent = parent;
    container->kind = BodyKind::Terrain;
    container->terrain = std::move(record);
    Edit edit{std::move(label), {}};
    if (created)
        edit.materials.push_back(std::move(*created));
    edit.changes.push_back({terrain, nullptr, container, {}, {}, {}, true});
    TerrainCreated result;
    result.terrain = terrain;
    for (auto &tile : tiles) {
        result.tiles.push_back(tile->id);
        edit.changes.push_back({tile->id, nullptr, std::move(tile), {}, {}, {}, true});
    }
    return {std::move(edit), std::move(result)};
}
} // namespace
TerrainCreated createTerrain(Document &doc, const TerrainMesh &mesh, TerrainRecord record,
                             Id parent, std::string name) {
    auto [edit, created] =
        terrainEdit(doc, mesh, std::move(record), parent, std::move(name), "Create terrain");
    created.changes = doc.apply(std::move(edit), doc.revision());
    return created;
}
TerrainCreated importTerrain(Document &doc, const TerrainMesh &mesh, TerrainRecord record,
                             std::optional<SiteOrigin> establish, std::string name) {
    if (establish) {
        if (doc.site())
            refuse("INVALID_REQUEST", "The site origin is already set; re-anchor to change it");
        validateSiteOrigin(*establish);
    }
    auto [edit, created] =
        terrainEdit(doc, mesh, std::move(record), 0, std::move(name), "Import terrain");
    if (establish)
        edit.site =
            std::pair{std::optional<SiteOrigin>{}, std::optional<SiteOrigin>{std::move(*establish)}};
    created.changes = doc.apply(std::move(edit), doc.revision());
    return created;
}
TerrainExplodeBudget terrainExplodeBudget(const Document &doc, Id terrain) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || found->second->kind != BodyKind::Terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    TerrainExplodeBudget budget;
    for (const auto id : terrainTiles(doc, terrain))
        budget.terrainFaces += doc.bodies().at(id)->surface.faces.size();
    std::map<Id, std::size_t> placements;
    for (const auto &[root, instance] : doc.instances())
        ++placements[instance->definition];
    const auto totals = doc.materializedTotals();
    budget.modelFaces = uniqueGeometryCounts(totals, placements, doc.definitions(),
                                             &doc.componentVariants())
                            .faces;
    budget.faceLimit = DocumentLimits::unique.faces;
    budget.documentTerrainTriangles = totals.terrainTriangles;
    budget.terrainLimit = TerrainLimits::triangles;
    return budget;
}
ChangeReport explodeTerrain(Document &doc, Id terrain) {
    const auto budget = terrainExplodeBudget(doc, terrain);
    const auto found = doc.bodies().find(terrain);
    const auto tiles = terrainTiles(doc, terrain);
    if (!budget.fits())
        refuse("LIMIT_EXCEEDED",
               "Explode terrain would make " + groupedCount(budget.facesAfter()) +
                   " faces, over the " + groupedCount(budget.faceLimit) +
                   "-face modelling limit (the terrain has " + groupedCount(budget.terrainFaces) +
                   " triangles)");
    Edit edit{"Explode terrain", {}};
    auto group = std::make_shared<Body>(*found->second);
    group->kind = BodyKind::Group;
    group->terrain.reset();
    edit.changes.push_back({terrain, found->second, group, {}, {}, {}, true});
    for (const auto id : tiles) {
        const auto &before = doc.bodies().at(id);
        auto ordinary = std::make_shared<Body>(*before);
        ordinary->terrainTile = false;
        ordinary->tileKey = {};
        edit.changes.push_back({id, before, ordinary, {}, {}, {}, true});
    }
    return doc.apply(std::move(edit), doc.revision());
}
TerrainPaint terrainTilePaint(const Document &doc, Id terrain) {
    TerrainPaint paint;
    const Body plain;
    // R095.e2: a default material every tile shares (the "Terrain" material, or one painted on
    // all tiles) is carried to the new tiles, so it is not paint that Regenerate drops.
    const auto shared = sharedTileMaterials(doc, terrain);
    MaterialSides fallback; // What mixed tiles return to: the existing "Terrain" material.
    if (!shared) {
        std::optional<MaterialChange> added;
        const auto material = terrainMaterial(doc, added);
        if (!added)
            fallback = {material, material};
    }
    // R096.e: paint the record's paint regions give a face again is not dropped.
    const TerrainPaintRegions regions(*doc.bodies().at(terrain)->terrain);
    for (const auto id : terrainTiles(doc, terrain)) {
        const auto &tile = *doc.bodies().at(id);
        paint.faces += terrainTileOwnPaint(tile, regions);
        if ((!shared && tile.materials != fallback) || tile.color != plain.color)
            ++paint.tiles;
    }
    return paint;
}
void requireUnlockedTerrain(const Document &doc, Id terrain) {
    for (Id id = terrain; id; id = doc.bodies().at(id)->parent)
        if (doc.bodies().at(id)->locked)
            refuse("INVALID_OPERATION", "Cannot edit a locked entity or its contents");
    for (const auto id : terrainTiles(doc, terrain))
        if (doc.bodies().at(id)->locked)
            refuse("INVALID_OPERATION", "Cannot edit a locked entity or its contents");
}
TerrainRegenerated regenerateTerrain(Document &doc, Id terrain, const TerrainMesh &mesh,
                                     TerrainRecord record, std::string label) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || found->second->kind != BodyKind::Terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    std::size_t current = 0;
    for (const auto id : terrainTiles(doc, terrain))
        current += doc.bodies().at(id)->surface.faces.size();
    checkTerrainMesh(mesh, doc.materializedTotals().terrainTriangles - current);
    return regenerateTerrain(doc, terrain, terrainTileSet(mesh), std::move(record),
                             std::move(label));
}
TerrainRegenerated regenerateTerrain(Document &doc, Id terrain, const TerrainTileSet &set,
                                     TerrainRecord record, std::string label) {
    const auto found = doc.bodies().find(terrain);
    if (found == doc.bodies().end() || found->second->kind != BodyKind::Terrain)
        refuse("NOT_FOUND", "Terrain body does not exist");
    requireUnlockedTerrain(doc, terrain);
    const auto old = terrainTiles(doc, terrain);
    std::size_t current = 0, triangles = 0;
    std::map<TerrainTileKey, Id> oldByKey;
    for (const auto id : old) {
        const auto &tile = *doc.bodies().at(id);
        current += tile.surface.faces.size();
        oldByKey.emplace(tile.tileKey, id);
    }
    for (const auto &tile : set.tiles)
        triangles += tile->triangles.size();
    if (!triangles)
        refuse("INVALID_REQUEST", "A terrain needs at least one triangle");
    const auto others = doc.materializedTotals().terrainTriangles - current;
    if (triangles > TerrainLimits::triangles || others + triangles > TerrainLimits::triangles)
        refuse("LIMIT_EXCEEDED", limitMessage(others + triangles));
    // Tile IDs (R096.a, the generalised rule of ADR 0169 §3). A body's vertex, face and edge IDs
    // never go back (retired IDs are not reused), so a tile body can only be kept when its mesh
    // is unchanged; the mesh digest reads tiles in key order, so IDs need no order. Rule: a new
    // tile whose key and mesh equal an old tile's keeps that tile's ID; every other new tile
    // takes a new ID from the document allocator in key order, and old tiles not kept are
    // removed. The same mesh therefore regenerates to the same IDs, and regenerating twice
    // gives identical records.
    // New tiles take the old tiles' shared default material (R095.e2); mixed tiles return to
    // the "Terrain" material, added in this edit when the document has none.
    std::optional<MaterialChange> createdMaterial;
    auto materials = sharedTileMaterials(doc, terrain);
    if (!materials) {
        const auto material = terrainMaterial(doc, createdMaterial);
        materials = MaterialSides{material, material};
    }
    std::vector<Id> ids;
    std::set<Id> kept;
    Id next = doc.nextId();
    for (const auto &content : set.tiles) {
        Id id = 0;
        if (const auto prior = oldByKey.find(content->key); prior != oldByKey.end()) {
            const auto before = terrainTileMesh(*doc.bodies().at(prior->second));
            if (before.vertices == content->vertices && before.triangles == content->triangles)
                id = prior->second;
        }
        if (id)
            kept.insert(id);
        else {
            if (next == UINT64_MAX)
                refuse("LIMIT_EXCEEDED", "Entity identity space exhausted");
            id = next++;
        }
        ids.push_back(id);
    }
    auto tiles = terrainTileBodies(set, ids, terrain, *materials);
    // R096.e: the record's paint regions paint every tile (decision 7), so a tile whose mesh and
    // regions did not change compares equal and is left alone.
    if (const TerrainPaintRegions regions(record); !regions.empty())
        for (auto &tile : tiles)
            paintTerrainTile(*tile, regions);
    record.meshDigest = tilesDigest(tiles);
    record.tiling = set.grid;
    assignTerrainSourceIds(record);
    validateTerrainRecord(record);
    TerrainRegenerated result;
    result.tiles = ids;
    const auto &before = found->second;
    Edit edit{std::move(label), {}};
    if (createdMaterial)
        edit.materials.push_back(std::move(*createdMaterial));
    if (*before->terrain != record) {
        auto container = std::make_shared<Body>(*before);
        container->terrain = std::move(record);
        edit.changes.push_back({terrain, before, std::move(container), {}, {}, {}, true});
    }
    for (std::size_t t = 0; t < tiles.size(); ++t) {
        const auto existing = doc.bodies().find(ids[t]);
        const BodyPtr prior = existing == doc.bodies().end() ? nullptr : existing->second;
        if (prior && *prior == *tiles[t])
            continue;
        edit.changes.push_back({ids[t], prior, std::move(tiles[t]), {}, {}, {}, true});
    }
    for (const auto id : old)
        if (!kept.contains(id))
            edit.changes.push_back({id, doc.bodies().at(id), nullptr, {}, {}, {}, true});
    // A terrain whose mesh, record and tiles are already what the sources give is left
    // alone: no edit and no history entry, as for other commands with nothing to change.
    if (edit.changes.empty())
        return result; // (A created material implies changed tiles, so it is never left alone.)
    result.changed = true;
    result.changes = doc.apply(std::move(edit), doc.revision());
    return result;
}
void assignTerrainSourceIds(TerrainRecord &record) {
    if (std::any_of(record.sources.begin(), record.sources.end(),
                    [](const TerrainSource &source) { return source.id != 0; }))
        return;
    auto sources = record.sources.get();
    for (std::size_t i = 0; i < sources.size(); ++i)
        sources[i].id = std::uint32_t(i + 1);
    record.sources = std::move(sources);
}
void setSiteOrigin(Document &doc, SiteOrigin origin) {
    if (doc.site())
        refuse("INVALID_REQUEST",
               "The site origin is already set; re-anchor to change it");
    validateSiteOrigin(origin);
    Edit edit{"Set site origin", {}};
    edit.site = std::pair{std::optional<SiteOrigin>{}, std::optional<SiteOrigin>{std::move(origin)}};
    doc.apply(std::move(edit), doc.revision());
}
ChangeReport reanchorSite(Document &doc, double easting, double northing, double elevation) {
    if (!doc.site())
        refuse("NOT_FOUND", "The document has no site origin; set it first");
    auto origin = *doc.site();
    origin.easting = easting;
    origin.northing = northing;
    origin.elevation = elevation;
    validateSiteOrigin(origin);
    if (origin == *doc.site())
        refuse("INVALID_REQUEST", "Re-anchor needs a different site origin");
    // local = site - origin, so every local point moves by old origin - new origin.
    const Vec3 delta{doc.site()->easting - easting, doc.site()->northing - northing,
                     doc.site()->elevation - elevation};
    const auto move = Transform::translation(delta);
    Edit edit{"Re-anchor site origin", {}};
    edit.site = std::pair{doc.site(), std::optional<SiteOrigin>{origin}};
    for (const auto &[id, body] : doc.bodies()) {
        if (body->parent)
            continue;
        auto moved = std::make_shared<Body>(*body);
        moved->transform = move * body->transform;
        edit.changes.push_back({id, body, moved, {}, {}, {}, true});
    }
    for (const auto &[id, section] : doc.sections()) {
        if (section->context)
            continue;
        auto moved = std::make_shared<SectionRecord>(*section);
        moved->plane.offset -= dot(moved->plane.normal, delta);
        edit.sections.push_back({id, section, moved});
    }
    for (const auto &[id, scene] : doc.scenes()) {
        const auto &snapshot = scene->snapshot;
        if (!snapshot.camera && !(snapshot.section && snapshot.section->plane) &&
            !(snapshot.style && delta.z != 0))
            continue;
        auto moved = std::make_shared<SceneRecord>(*scene);
        if (moved->snapshot.camera)
            moved->snapshot.camera->target = moved->snapshot.camera->target + delta;
        if (moved->snapshot.section && moved->snapshot.section->plane) {
            auto &plane = *moved->snapshot.section->plane;
            plane[3] -= plane[0] * delta.x + plane[1] * delta.y + plane[2] * delta.z;
        }
        if (moved->snapshot.style)
            moved->snapshot.style->groundHeight += delta.z;
        if (*moved != *scene)
            edit.scenes.push_back({id, scene, moved});
    }
    // The ground plane is a model-Z height: it keeps its site elevation.
    if (delta.z != 0) {
        auto style = doc.style();
        style.groundHeight += delta.z;
        edit.style = std::pair{doc.style(), style};
    }
    // Annotation and paper anchors keep a world fallback (the fixed point of a point
    // anchor, or the last resolved position of an entity anchor).
    auto shift = [&](AnnotationAnchor &anchor) { anchor.fallback = anchor.fallback + delta; };
    for (const auto &[id, annotation] : doc.annotations()) {
        if (annotation->anchors.empty())
            continue;
        auto moved = std::make_shared<AnnotationRecord>(*annotation);
        for (auto &anchor : moved->anchors)
            shift(anchor);
        edit.annotations.push_back({id, annotation, moved});
    }
    edit.annotationsResolved = true;
    for (const auto &[id, item] : doc.drawingItems()) {
        auto moved = std::make_shared<DrawingItemRecord>(*item);
        bool changed = false;
        if (auto *label = std::get_if<DrawingLabelItem>(&moved->data);
            label && label->target.kind == DrawingLabelTargetKind::View) {
            shift(label->target.anchor);
            changed = true;
        } else if (auto *dimension = std::get_if<DrawingDimensionItem>(&moved->data)) {
            for (auto &anchor : dimension->anchors)
                shift(anchor);
            changed = true;
        }
        if (changed)
            edit.drawingItems.push_back({id, item, moved});
    }
    edit.drawingAnchorsResolved = true;
    return doc.apply(std::move(edit), doc.revision());
}
Vec3 modelToSite(const SiteOrigin &origin, Vec3 model) {
    return {model.x + origin.easting, model.y + origin.northing, model.z + origin.elevation};
}
Vec3 siteToModel(const SiteOrigin &origin, Vec3 site) {
    return {site.x - origin.easting, site.y - origin.northing, site.z - origin.elevation};
}
std::vector<TerrainElevation> terrainElevationAt(const Document &doc, Id terrain,
                                                 const std::vector<std::array<double, 2>> &points) {
    if (!isTerrain(doc, terrain))
        refuse("NOT_FOUND", "Terrain body does not exist");
    if (points.size() > terrainPointBatch)
        refuse("LIMIT_EXCEEDED", "Ask for at most 200 points at a time");
    const auto frame = doc.worldTransform(terrain);
    const auto &m = frame.m;
    const bool translation = m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 0 &&
                             m[5] == 1 && m[6] == 0 && m[7] == 0 && m[8] == 0 && m[9] == 0 &&
                             m[10] == 1 && m[11] == 0 && m[15] == 1;
    if (translation && !terrainTiles(doc, terrain).empty()) {
        // R096.a: candidates from the edit mesh's grid, then R095's exact tests in R095's order.
        // A pure translation maps x to fl(x + t), which is monotonic, so a tile's world box is
        // its terrain-frame box plus t, computed the same way.
        const auto mesh = terrainEditMeshCache().get(doc, terrain);
        const double tx = m[12], ty = m[13];
        std::vector<TerrainElevation> result;
        result.reserve(points.size());
        for (const auto &[x, y] : points) {
            if (!std::isfinite(x) || !std::isfinite(y))
                refuse("INVALID_REQUEST", "Elevation points must be finite");
            const double slack = 1e-6 + 1e-12 * (std::abs(x) + std::abs(y) + std::abs(tx) +
                                                 std::abs(ty));
            auto candidates = mesh->tilesAt(x - tx, y - ty, slack);
            std::sort(candidates.begin(), candidates.end(),
                      [](const auto *a, const auto *b) { return a->body < b->body; });
            TerrainElevation best;
            for (const auto *tile : candidates) {
                constexpr double tileSlack = 1e-9;
                if (x < (tile->minX + tx) - tileSlack || x > (tile->maxX + tx) + tileSlack ||
                    y < (tile->minY + ty) - tileSlack || y > (tile->maxY + ty) + tileSlack)
                    continue;
                const auto &content = *tile->content;
                const Id base = content.vertices.size() + 1;
                for (std::size_t j = 0; j < content.triangles.size(); ++j) {
                    const auto &t = content.triangles[j];
                    const auto a = frame.point(content.vertices[t[0]]),
                               b = frame.point(content.vertices[t[1]]),
                               c = frame.point(content.vertices[t[2]]);
                    const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                    if (std::abs(area) < 1e-18)
                        continue;
                    const double u = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / area,
                                 v = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / area,
                                 w = 1 - u - v;
                    auto beyond = [&](double weight, Vec3 p, Vec3 q) {
                        return weight < 0 && -weight * std::abs(area) >
                                                 tolerance * std::hypot(q.x - p.x, q.y - p.y);
                    };
                    if (beyond(u, b, c) || beyond(v, c, a) || beyond(w, a, b))
                        continue;
                    const double z = u * a.z + v * b.z + w * c.z;
                    if (!best.found || z > best.elevation)
                        best = {true, z, tile->body, base + j};
                }
            }
            result.push_back(best);
        }
        return result;
    }
    struct Tile {
        Id id{};
        double minX{}, minY{}, maxX{}, maxY{};
        std::map<Id, Vec3> world;
        const Body *body{};
    };
    std::vector<Tile> tiles;
    for (const auto id : terrainTiles(doc, terrain)) {
        Tile tile;
        tile.id = id;
        tile.body = doc.bodies().at(id).get();
        const auto frame = doc.worldTransform(id);
        tile.minX = tile.minY = std::numeric_limits<double>::infinity();
        tile.maxX = tile.maxY = -std::numeric_limits<double>::infinity();
        for (const auto &[vertex, point] : tile.body->surface.vertices) {
            const auto world = frame.point(point);
            tile.world.emplace_hint(tile.world.end(), vertex, world);
            tile.minX = std::min(tile.minX, world.x);
            tile.maxX = std::max(tile.maxX, world.x);
            tile.minY = std::min(tile.minY, world.y);
            tile.maxY = std::max(tile.maxY, world.y);
        }
        tiles.push_back(std::move(tile));
    }
    std::vector<TerrainElevation> result;
    result.reserve(points.size());
    for (const auto &[x, y] : points) {
        if (!std::isfinite(x) || !std::isfinite(y))
            refuse("INVALID_REQUEST", "Elevation points must be finite");
        TerrainElevation best;
        for (const auto &tile : tiles) {
            constexpr double slack = 1e-9;
            if (x < tile.minX - slack || x > tile.maxX + slack || y < tile.minY - slack ||
                y > tile.maxY + slack)
                continue;
            for (const auto &[face, record] : tile.body->surface.faces) {
                const auto &loop = record.loops[0];
                const auto a = tile.world.at(loop[0]), b = tile.world.at(loop[1]),
                           c = tile.world.at(loop[2]);
                const double area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (std::abs(area) < 1e-18)
                    continue; // Vertical in plan: no unique surface point.
                const double u = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / area,
                             v = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / area,
                             w = 1 - u - v;
                // Inside, or within the 1e-7 m geometry tolerance of an edge in plan.
                auto beyond = [&](double weight, Vec3 p, Vec3 q) {
                    return weight < 0 &&
                           -weight * std::abs(area) > tolerance * std::hypot(q.x - p.x, q.y - p.y);
                };
                if (beyond(u, b, c) || beyond(v, c, a) || beyond(w, a, b))
                    continue;
                const double z = u * a.z + v * b.z + w * c.z;
                if (!best.found || z > best.elevation)
                    best = {true, z, tile.id, face};
            }
        }
        result.push_back(best);
    }
    return result;
}
} // namespace sketchy
