#pragma once
// R095.a terrain persistence (ADR 0168 §2): the TerrainRecord and site origin in the
// model JSON, and the binary terrain-mesh chunk that stores every tile's mesh.
#include "core/body.hpp"
#include "core/terrain_records.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <functional>
#include <string>
namespace sketchy {
// The record (ADR 0108 schema 29 with the R095.e2 and R096.a addenda). R096.a: each source
// has `id`; the record has `tiling` {origin: [x, y], cell} and, when not empty, `edits`.
QJsonObject encodeTerrainRecord(const TerrainRecord &record);
// With `legacy`, a record in the R095 development form (no source ids, no tiling, no edits) is
// accepted unvalidated and *legacy set: the document decoder derives the ids and the tiling
// (deriveLegacyTerrain) and then validates it. Without `legacy` that form is refused.
TerrainRecord decodeTerrainRecord(const QJsonValue &value, bool *legacy = nullptr);
// R096.a edit entries: {id, kind, ...the kind's fields}. The kind's codec writes and reads its
// own fields; R096.a registers none in production (tests register theirs).
struct TerrainEditCodec {
    std::string kind;
    std::function<QJsonObject(const TerrainEditData &)> encode;
    // Must check every bound before allocating; throws std::runtime_error on invalid input.
    std::function<std::shared_ptr<const TerrainEditData>(const QJsonObject &)> decode;
};
void registerTerrainEditCodec(TerrainEditCodec codec);
QJsonObject encodeTerrainEdit(const TerrainEdit &edit);
TerrainEditPtr decodeTerrainEdit(const QJsonValue &value);
// The edit list decoder alone (the `edits` array), for the parser fuzz target.
std::vector<TerrainEditPtr> decodeTerrainEdits(const QJsonValue &value);
// R096.a tile markers: {key: [row, column, path, part]} (`true` is the R095 form; *legacy).
QJsonObject encodeTerrainTileMarker(const TerrainTileKey &key);
TerrainTileKey decodeTerrainTileMarker(const QJsonValue &value, bool *legacy = nullptr);
QJsonValue encodeSiteOrigin(const std::optional<SiteOrigin> &origin);
std::optional<SiteOrigin> decodeSiteOrigin(const QJsonValue &value);
// terrain-mesh chunk, little-endian:
//   "SKTMESH1", u32 tile count,
//   per tile {u64 body ID, u32 vertex count, u32 triangle count},
//   then per tile f64 XYZ per vertex and u32 x3 per triangle.
// Tiles appear in ascending body ID order. Vertex and face IDs are implicit (tile
// base 1 plus index); edges come from Topology::rebuild.
struct TerrainMeshTile {
    Id body{};
    std::vector<Vec3> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
};
// Largest chunk the limits admit: 10,000 tiles and 250,000 triangles of three
// unshared vertices each.
inline constexpr std::size_t terrainMeshChunkLimit =
    12 + 16 * 10000 + 250000 * (3 * 24 + 12);
// Empty when the bodies contain no terrain tile.
QByteArray encodeTerrainMesh(const std::map<Id, BodyPtr> &bodies);
// Checks the tile count, every tile's counts, the terrain triangle limit and the exact
// byte length before allocating any tile, then every index and coordinate.
std::vector<TerrainMeshTile> decodeTerrainMesh(const QByteArray &bytes);
} // namespace sketchy
