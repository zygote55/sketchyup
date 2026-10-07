# R082.c — 100 MB native file fixture and core timings

2026-10-07. The opt-in `native_persistence_benchmark` constructs a deterministic
100,671,415-byte native file (100.67 decimal MB). It contains 1,000 independent
square faces, bounded per-entity metadata, and four deterministic 16 MiB opaque
local assets. This deliberately stresses metadata/asset storage; it is not a
complex-geometry, image-decode or representative architectural scene benchmark.
All existing per-record, aggregate asset and model/container limits remain intact.

Fixture SHA-256:
`d74a68105014d648676afbfb37998fd3e08fd95a9148a36f0d8b29d03dd60f37`.
[Exact source, environment and six reports](R082c-native-persistence.json) retain
three fresh-process warm reads and three fresh-process reads following Linux
per-file `POSIX_FADV_DONTNEED`. The latter is advisory: no cold-cache guarantee
is claimed, and global host caches are not dropped. Warm policy reads the entire
input immediately before the timed load. Every sample then times synchronous
core load/validation and durable save, and requires byte-identical input/output
hashes outside the measured interval. It does not measure UI event processing.

| Debug run group | Open range | Save range | Peak process RSS range |
| --- | ---: | ---: | ---: |
| Warm, three samples | 663.84–683.88 ms | 519.54–536.77 ms | 524,955,648–525,742,080 B |
| Advised cold, three samples | 710.87–718.67 ms | 521.53–552.96 ms | 518,782,976–519,131,136 B |

These measurements used Qt 6.11.2 / GCC 16.2.1 on this development machine while
other builds/tests ran. They are exploratory. They do not accept the reference
SSD five-second budget, UI responsiveness, an explicit memory budget or R082.
RSS includes normal load/save allocations; final verification hashing is excluded
from the recorded peak query. All six outputs preserve canonical bytes. Both
prepare and measure refuse an existing output without altering it, and an invalid
cache-policy argument is rejected before creating a file. Original fixture and
outputs remain in the private build directory.

Reproduce with an optimized build and a new output directory:

```sh
cmake --build build/release --target native_persistence_benchmark --parallel 2
build/release/native_persistence_benchmark --prepare build/persistence/fixture.sketchyup
build/release/native_persistence_benchmark --measure build/persistence/fixture.sketchyup build/persistence/warm-1.sketchyup warm
build/release/native_persistence_benchmark --measure build/persistence/fixture.sketchyup build/persistence/cold-1.sketchyup advised-cold
```

The [source archive check](R082d-source-package.json) verifies all 218 installed
inputs byte for byte, including the updated benchmark contract.

The executable is excluded from the default build and has no variable-host timing
CTest threshold. Configure `build/release` with testing enabled and create the
output directory first. Use a distinct output for every sample. Subsequent model
schema changes may change the canonical hash; retain versioned fixture reports
rather than comparing unlike inputs. Controlled reference runs, actual cold-cache
verification, UI responsiveness, the million-triangle instance fixture and the
remaining R082 scenarios/gates are still open.
