#pragma once
#include "core/asset_records.hpp"
#include <cstddef>
namespace sketchy {
// Shared by native readers, writers, recovery and capability declarations.
// Legacy JSON retains its own bound when the current native profile evolves.
// fileBytes and modelBytes were raised in 1.7.0 with the unique geometry limits (R110, ADR 0005):
// a file written by 1.7.0 that is within the 1.6.1 bounds still reads in 1.6.1, and a larger one
// is refused there with that version's own message.
struct NativeLimits {
    static constexpr std::size_t fileBytes = 256 * 1024 * 1024;
    // A legacy or import raw JSON file (no container magic) keeps the 1.6.1 bound of 128 MiB: its
    // parse memory at 256 MiB was not measured. The in-process inline form (encodeDocument with
    // inline asset data, used by presentation capture and by the native round-trip checks) is
    // not bounded by this constant: by the bounds, a valid at-limit document with its assets
    // inline is about 208 MiB, so that form keeps fileBytes (R110, ADR 0108, ADR 0005).
    static constexpr std::size_t rawJsonBytes = 128 * 1024 * 1024;
    static constexpr std::size_t modelBytes = 96 * 1024 * 1024;
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
