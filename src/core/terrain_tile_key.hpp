#pragma once
// A terrain tile's key, apart from the terrain records (core/terrain_records.hpp) so that
// core/body.hpp needs only this (build fan-out, ADR 0170).
#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
namespace sketchy {
// A tile's place in the grid: row and column (negative outside the original extent), the
// sub-cell path (quadrant digits '0'..'3', digit = 2 x upper half + right half, at most
// terrainTileMaxDepth deep) and the part number of a cluster. Tiles are ordered by key
// (row, column, path, part): row-major, as at creation, so for a terrain that was never edited
// key order is ID order.
struct TerrainTileKey {
    std::int64_t row{}, column{};
    std::string path;
    std::uint32_t part{};
    auto operator<=>(const TerrainTileKey &) const = default;
    bool operator==(const TerrainTileKey &) const = default;
};
inline constexpr std::size_t terrainTileMaxDepth = 24;
inline constexpr std::int64_t terrainTileKeyLimit = std::int64_t(1) << 53; // |row|, |column|
} // namespace sketchy
