// R096.c libFuzzer target for the terrain cavity adapter (ADR 0169 §3, §9). The input is the
// compact binary encoding in terrain_cavity_harness.hpp; every input runs three times (twice as
// given, once with the kept vertices reversed). Refusals are ordinary results; a failed invariant
// or a crash is a finding.
#include "terrain_cavity_harness.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size) {
    if (size > 4096)
        return 0;
    sketchy::fuzz::runCavityHarness(data, size);
    return 0;
}
