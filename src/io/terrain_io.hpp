#pragma once
// R095.a terrain persistence (ADR 0168 §2): the TerrainRecord and site origin in the
// model JSON, and the binary terrain-mesh chunk that stores every tile's mesh.
#include "core/body.hpp"
#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>
namespace sketchy {
QJsonObject encodeTerrainRecord(const TerrainRecord &record);
TerrainRecord decodeTerrainRecord(const QJsonValue &value);
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
