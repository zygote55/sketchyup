# R083.e — Coverage-guided parser fuzzing

2026-10-10. Source `6bdfe8de` (the recovered parser fuzz targets on `8534bb67`);
the follow-up commit only adds this record and its links. The ten
[opt-in libFuzzer targets](../PARSER_FUZZING.md) ran for **300 s each** under
ASan/UBSan with leak detection. Under the documented options, **four targets ran
to time and six stopped on their first seed** with an ASan alloc-dealloc mismatch
inside Qt JSON parsing. A supplementary run that disabled only that check found
no further crash, leak, hang, sanitizer report, invariant failure or RSS-limit
breach. This is local hardening evidence, not release acceptance.

## Environment and commands

Omarchy (Arch), Linux 7.2.5, clang/compiler-rt 22.1.8, Qt 6.11.2, CMake 4.4.3,
Ninja 1.13.2. The default `dev` preset uses GCC 16.2.1. Host memory was shared
with unrelated work, so the campaign ran **at most two targets concurrently**
with `-rss_limit_mb=2048` instead of the documented 3072. The fuzz build used
`--parallel 8`, and the default build check used `--parallel 4`.

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -g' \
  -DBUILD_TESTING=OFF -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=OFF \
  -DSKETCHYUP_BUILD_FUZZERS=ON
cmake --build build/fuzz --target parser_fuzzers --parallel 8
build/fuzz/parser_fuzz_seeds "$CAMPAIGN/seeds" > "$CAMPAIGN/seeds.json"
cp -r "$CAMPAIGN/seeds" "$CAMPAIGN/corpus"
# per format, two at a time:
TMPDIR="$CAMPAIGN/scratch" ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/fuzz/parser_fuzz_<format> "$CAMPAIGN/corpus/<format>" \
  -max_len=1048576 -max_total_time=300 -timeout=10 -rss_limit_mb=2048 \
  -detect_leaks=1 -print_final_stats=1 -artifact_prefix="$CAMPAIGN/artifacts/<format>/"
```

The supplementary run used fresh seed copies and the same command with
`ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:alloc_dealloc_mismatch=0`. It ran
only the six blocked formats. No harness, CMake or application change was needed
for API drift. Corpora, logs and binaries stay outside the repository.

## Seeds

`parser_fuzz_seeds` produced 12 valid seeds, and every seed passed the
accepted-result invariants before publication. The corpus hash is SHA-256 over
`sha256sum` of each file, sorted by name.

| Format | Seeds | Seed-set SHA-256 |
| --- | --: | --- |
| native-json | 2 | `a382b79887b063cdc9f9a5d54b2393d1e8f9ab8b95aacefbf6c06c7033c0419d` |
| native-container | 2 | `e1bc04363b8339650f0fc5818cf03f4d04fad1804833eebb72d3ca162baba645` |
| glb | 1 | `672d8b3a4e72ac0d52fb91bbfc8ff6f0154748570ff885da749c5b60d0fe0380` |
| stl-binary | 1 | `6b30f10cd9b74297b7aa51a1be9f9ff88c44de274cc8e97001096855dbef58fd` |
| stl-ascii | 1 | `0930c147e524735246f6a13ea6b78553f606f0f764843cadf19cbc8a97702a6c` |
| obj | 1 | `ce660cddf1ee4efffdbfee8cb8d7442d54703369687fe342cce3c68ae74543b6` |
| dxf | 1 | `77b3a7667050a722ea74eddc7d6b4e595e2b0f9413529ba23da2a0d18fc42fe5` |
| template | 1 | `800bca1c8c76f2d35b6db566c308257481d1318415cb857f34102506df113957` |
| component | 1 | `2006a8f325b826ce0ae6dc188d7ca5b63ea3909b009807296ac3cc26a6de4ad4` |
| extension | 1 | `ec13f723b3e23da6c06ae8e23928fa9a376288db767ada5033ef88c3d118a7db` |

## Results

These values are the libFuzzer final statistics. Corpus is the number of files
retained on disk. An asterisk marks the supplementary run.

| Target | Elapsed | Executions | Exec/s | cov | ft | Peak RSS | Corpus | Findings |
| --- | --: | --: | --: | --: | --: | --: | --: | --- |
| native-json | <1 s | 2 | — | — | — | 58 MB | 2 | F1 on seed |
| native-json* | 301 s | 671,846 | 2,232 | 6,939 | 20,937 | 491 MB | 296 | none |
| native-container | <1 s | 2 | — | — | — | 58 MB | 2 | F1 on seed |
| native-container* | 301 s | 983,876 | 3,268 | 6,716 | 13,005 | 508 MB | 83 | none |
| glb | <1 s | 2 | — | — | — | 88 MB | 1 | F1 on seed |
| glb* | 301 s | 8,757 | 29 | 13,189 | 19,838 | 136 MB | 144 | none |
| stl-binary | 301 s | 3,826,255 | 12,711 | 326 | 753 | 525 MB | 81 | none |
| stl-ascii | 301 s | 1,358,074 | 4,511 | 537 | 1,944 | 573 MB | 120 | none |
| obj | 301 s | 1,406,462 | 4,672 | 876 | 4,546 | 554 MB | 485 | none |
| dxf | 301 s | 2,281,837 | 7,580 | 1,341 | 5,085 | 536 MB | 438 | none |
| template | <1 s | 2 | — | — | — | 57 MB | 1 | F1 on seed |
| template* | 301 s | 1,757,119 | 5,837 | 6,809 | 12,015 | 512 MB | 41 | none |
| component | <1 s | 2 | — | — | — | 57 MB | 1 | F1 on seed |
| component* | 301 s | 1,187,497 | 3,945 | 8,831 | 14,485 | 517 MB | 40 | none |
| extension | <1 s | 2 | — | — | — | 57 MB | 1 | F1 on seed |
| extension* | 301 s | 92,532 | 307 | 3,046 | 3,873 | 519 MB | 61 | none |

The GLB target writes one private scratch file per input and runs the full
import, so its throughput is far lower than the in-memory parsers.

## Finding F1 — ASan alloc-dealloc mismatch in `QJsonDocument::fromJson`

Each JSON-based target aborts on its first, unmodified valid seed. The six crash
artifacts are byte-identical to the seeds. Their SHA-256 values are
`43c84993…` (native-json `empty-document`), `e2528fa5…` (native-container
`empty-document`), `ec9382c1…` (glb), `a696bddd…` (template), `b7254387…`
(component) and `81c09be9…` (extension). Each reproduced twice with exit 134.
`-minimize_crash=1` reduced the native-json input to the 6-byte `{"":2}`
(SHA-256 `cfd6d63ed693596ba47f553c434fd852b232b903dfc952811ba3def019660d1c`),
which also reproduced twice.

ASan reports memory allocated by `operator new(size_t, std::nothrow_t const&)` and
released by `free`. Both stacks are entirely inside `libQt6Core.so.6`
(`+0x241dc1` allocates and `+0x241f16` frees) under `QJsonDocument::fromJson`.
The callers are `decodeDocument` (`document_io.cpp:574`), `decodeContainer`
(`container.cpp:137`), `GltfPackage::Impl::open` (`gltf_package.cpp:155`),
`decodeBundle` (`library_bundle.cpp:129`) and `parseExtensionManifest`
(`extension.cpp:126`). A three-line libFuzzer program that passes the minimized
input only to `QJsonDocument::fromJson` reproduces the report, with no SketchyUp
code linked. The same program linked with `-fsanitize=address` but without
libFuzzer reports nothing, and `parser_fuzz_seeds` passes every seed through the
same ASan/UBSan-instrumented parsers without a report. The evidence points to an
interaction between Qt 6.11.2's JSON parser and the libFuzzer/ASan runtime, not to
a SketchyUp defect. The root cause is not established. The finding stays open,
and no application code or regression test was changed in this layer.

## Default build

`cmake --preset dev` configures with `SKETCHYUP_BUILD_FUZZERS=OFF` and generates
no fuzz targets. `cmake --build --preset dev --target sketchyup_io --parallel 4`
succeeds. The fuzz targets are `EXCLUDE_FROM_ALL` and have no install rule.

## Limits

A finite campaign gives measured coverage, not proof that every malformed file is
safe. Six formats were fuzzed only with one ASan check disabled, so that run does
not satisfy the documented options. The campaign is not in CI. The harness caps
inputs at 1 MiB, and production size limits remain covered by the existing
boundary and parser-corruption tests. The reduced RSS limit and contention from
other work affect throughput. Continuous fuzzing, the F1 resolution,
release-candidate revalidation and final R083 acceptance remain open.
