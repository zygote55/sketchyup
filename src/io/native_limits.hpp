#pragma once
#include "core/asset_records.hpp"
#include <cstddef>
namespace sketchy {
// Shared by native readers, writers, recovery and capability declarations.
// Legacy JSON retains its own bound when the current native profile evolves.
struct NativeLimits {
    static constexpr std::size_t fileBytes = 128 * 1024 * 1024;
    static constexpr std::size_t modelBytes = 32 * 1024 * 1024;
    static constexpr std::size_t legacyModelBytes = 32 * 1024 * 1024;
    static constexpr std::size_t manifestBytes = 4 * 1024 * 1024;
    // The terrain-mesh chunk (schema 29): 10,000 tiles, 250,000 triangles of three
    // unshared vertices. Mirrors io/terrain_io.hpp terrainMeshChunkLimit.
    static constexpr std::size_t terrainMeshBytes = 12 + 16 * 10000 + 250000 * (3 * 24 + 12);
    static constexpr std::size_t containerBytes =
        16 + manifestBytes + modelBytes + assetTotalLimit + terrainMeshBytes;
};
static_assert(NativeLimits::containerBytes <= NativeLimits::fileBytes);
} // namespace sketchy
