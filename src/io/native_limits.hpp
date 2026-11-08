#pragma once
#include "core/asset_records.hpp"
#include "core/document_limits.hpp"
#include <cstddef>
namespace sketchy {
// Shared by native readers, writers, recovery and capability declarations.
// Legacy JSON retains its own bound when the current native profile evolves.
// fileBytes and modelBytes were raised in 1.7.0 with the unique geometry limits (R110, ADR 0005):
// a file written by 1.7.0 that is within the 1.6.1 bounds still reads in 1.6.1, and a larger one
// is refused there with that version's own message. R115 (2026-11-07) raises them again, to the
// values R114 measured: a 258 MB model wrote and read through these bounds with no format change
// (docs/verification/R114-hardware-limits.md, "Binary storage").
struct NativeLimits {
    // Every byte bound in this struct scales with the benchmark-only limit scale (R114). The scale
    // is 1 in every product build, so each value is the published one.
    static constexpr std::size_t fileBytes = 2ull * 1024 * 1024 * 1024 * DocumentLimits::scale;
    // A legacy or import raw JSON file (no container magic) is bounded by this constant, 1 GiB in
    // R115 (from 128 MiB). It is never smaller than modelBytes. The in-process inline form
    // (encodeDocument with inline asset data, used by presentation capture and by the native
    // round-trip checks) is not bounded by this constant: a valid at-limit document with its assets
    // inline is bounded by fileBytes (R110, ADR 0108, ADR 0005).
    static constexpr std::size_t rawJsonBytes = 1024ull * 1024 * 1024 * DocumentLimits::scale;
    static constexpr std::size_t modelBytes = 768ull * 1024 * 1024 * DocumentLimits::scale;
    static constexpr std::size_t legacyModelBytes = 32 * 1024 * 1024;
    static constexpr std::size_t manifestBytes = 32ull * 1024 * 1024 * DocumentLimits::scale;
    // The terrain-mesh chunk (schema 29): 10,000 tiles, 250,000 triangles of three
    // unshared vertices. Mirrors io/terrain_io.hpp terrainMeshChunkLimit.
    static constexpr std::size_t terrainMeshBytes = 12 + 16 * 10000 + 250000 * (3 * 24 + 12);
    static constexpr std::size_t containerBytes =
        16 + manifestBytes + modelBytes + assetTotalLimit + terrainMeshBytes;
};
static_assert(NativeLimits::containerBytes <= NativeLimits::fileBytes);
} // namespace sketchy
