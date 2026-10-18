// R095.b libFuzzer target for the terrain triangulation adapter (ADR 0168 section 3).
// The input is the compact binary encoding in terrain_triangulation_harness.hpp. Refusals
// are ordinary results; a failed invariant or a crash is a finding.
#include "terrain_triangulation_harness.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size) {
    if (size > 8192)
        return 0;
    sketchy::fuzz::runTerrainHarness(data, size);
    return 0;
}
