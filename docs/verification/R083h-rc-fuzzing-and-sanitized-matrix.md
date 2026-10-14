# R083.h — Parser fuzzing and sanitized failure matrix at the RC

2026-10-13 (local; 2026-10-14 UTC). Source: the 1.0.0 release candidate
`7b01bb7a1d3fa19b6b5cc7f1a4c27a2ad5cd03a2` ([M9](M9.md#release-candidate)), built
unmodified. [Exact values](R083h-rc-fuzzing-and-sanitized-matrix.json). The ID R083.h is
used because [R083.f](R083f-file-worker-hang.md) and [R083.g](R083g-unexpected-modal-guard.md)
are already taken.

This record closes the two R083 checks that had not run at the RC: the 13-suite failure
matrix under ASan/UBSan, and the coverage-guided parser fuzzing campaign
([R088.f](R088f-core-parity-report.md#rc-evidence-thinner-than-r088d-claimed)).

- **Sanitized matrix: 13/13 pass in 200.96 s**, including the five suites last sanitized
  in [R083.d](R083d-local-failure-matrix.md).
- **Fuzzing: no new finding.** Under the documented options, four targets ran to time and
  six stopped on their first seed with the known Qt JSON alloc-dealloc mismatch
  (F1, [D-022](R089a-defect-triage.md)). The supplementary run with only that check
  disabled ran all six to time with no crash, leak, hang, sanitizer report, invariant
  failure or RSS-limit breach.

## Sanitized 13-suite matrix

The `sanitize` preset build of the RC from the [R089.d](R089d-release-candidate.md)
walkthrough was reused (GCC 16.2.1, Qt 6.11.2, CMake 4.4.3, Ninja 1.13.2; image
`sketchyup-native-fonts-check:20261008`, `sha256:873ba078…51bd08`). Only the missing suite
targets were built. The job ran in one bounded container: 6 CPUs, 10 GiB, `-j4`.

```sh
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
cmake --build --preset sanitize --target geometry_sequence_tests persistence_tests \
  m4_persistence_tests recovery_tests recovery_cli_tests geometry_fuzz_tests \
  new_file_fault_tests parser_fault_corpus_tests native_format_tests \
  transaction_sequence_tests transaction_coordinator_tests outcome_store_tests \
  staging_tests sketchyup-cli --parallel 4
ctest --test-dir build/sanitize -R '^(geometry_fuzz|new_file_faults|parser_fault_corpus|native_format|transaction_sequence|transaction_coordinator|outcome_store|staging|geometry_sequence|recovery_cli|recovery|m4_persistence|persistence)$' \
  --output-on-failure --timeout 1500
```

The build step took 24 s and built the five missing test executables. CTest ran the suites
serially, as R083.d did.

| Suite | RC (s) | R083.d (s) | Sanitized at the RC before this record |
| --- | --: | --: | --- |
| geometry_fuzz | 16.12 | 16.14 | yes |
| new_file_faults | 0.08 | 0.08 | yes |
| parser_fault_corpus | 19.82 | 21.21 | yes |
| native_format | 18.51 | 18.78 | yes |
| transaction_sequence | 16.92 | 46.88 | yes |
| transaction_coordinator | 3.25 | 9.65 | yes |
| outcome_store | 93.18 | 124.62 | yes |
| staging | 1.30 | 12.80 | yes |
| geometry_sequence | 4.65 | 9.43 | **no** |
| recovery_cli | 4.05 | 4.20 | **no** |
| recovery | 20.41 | 29.34 | **no** |
| m4_persistence | 0.47 | 0.68 | **no** |
| persistence | 2.21 | 2.50 | **no** |
| **Total** | **200.96** | **296.33** | |

All 13 pass. No sanitizer report, leak or failure occurred, so no re-run was needed.
Times are test durations under a bounded container, not performance measurements;
R083.d ran on a more loaded host.

## Coverage-guided parser fuzzing

Host: Omarchy (Arch), Linux 7.2.5, clang/compiler-rt 22.1.8, Qt 6.11.2, CMake 4.4.3,
Ninja 1.13.2, the same toolchain as [R083.e](R083e-parser-fuzzing.md). As in R083.e,
host memory was shared, so **at most two targets ran at once** with
`-rss_limit_mb=2048`. The fuzz build used `--parallel 6` and took 4 min 16 s. The build
dir was `.local/wt-rc/build/fuzz-rc`; the campaign dir `.local/fuzz-rc` holds corpora,
logs, artifacts and binaries outside the repository.

```sh
cmake -S . -B build/fuzz-rc -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -g' \
  -DBUILD_TESTING=OFF -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=OFF \
  -DSKETCHYUP_BUILD_FUZZERS=ON
cmake --build build/fuzz-rc --target parser_fuzzers --parallel 6
build/fuzz-rc/parser_fuzz_seeds "$CAMPAIGN/seeds" > "$CAMPAIGN/seeds.json"
cp -r "$CAMPAIGN/seeds" "$CAMPAIGN/corpus"
# per format, two at a time:
TMPDIR="$CAMPAIGN/scratch" ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/fuzz-rc/parser_fuzz_<format> "$CAMPAIGN/corpus/<format>" \
  -max_len=1048576 -max_total_time=300 -timeout=10 -rss_limit_mb=2048 \
  -detect_leaks=1 -print_final_stats=1 -artifact_prefix="$CAMPAIGN/artifacts/<format>/"
```

The supplementary run used fresh seed copies and the same command with
`ASAN_OPTIONS=detect_leaks=1:abort_on_error=1:alloc_dealloc_mismatch=0`, for the six
targets that stopped. No harness, CMake or application change was needed.

### Seeds

`parser_fuzz_seeds` produced 12 valid seeds, and every seed passed the accepted-result
invariants. The native JSON, container, template and component seeds are **schema 26**,
the version the RC writes; R083.e's were schema 24. The other six seed sets are
byte-identical to R083.e's. The seed-set hash is SHA-256 over `sha256sum ./*` in each
format directory, as in R083.e.

| Format | Seeds | Seed-set SHA-256 | Same as R083.e |
| --- | --: | --- | --- |
| native-json | 2 | `2b3663ab754c300a2da091c5f705f9ba004447ce6a065103e3450d7d005044a2` | no (schema 26) |
| native-container | 2 | `c11b658d554e739a2d73a38a1ba174dd968eda387270249a0d6fe2fd292345c7` | no (schema 26) |
| glb | 1 | `672d8b3a4e72ac0d52fb91bbfc8ff6f0154748570ff885da749c5b60d0fe0380` | yes |
| stl-binary | 1 | `6b30f10cd9b74297b7aa51a1be9f9ff88c44de274cc8e97001096855dbef58fd` | yes |
| stl-ascii | 1 | `0930c147e524735246f6a13ea6b78553f606f0f764843cadf19cbc8a97702a6c` | yes |
| obj | 1 | `ce660cddf1ee4efffdbfee8cb8d7442d54703369687fe342cce3c68ae74543b6` | yes |
| dxf | 1 | `77b3a7667050a722ea74eddc7d6b4e595e2b0f9413529ba23da2a0d18fc42fe5` | yes |
| template | 1 | `fecf1f64f8bf113af84ee427254a87901aea02862ba33d48d345ccc05a83a8fe` | no (schema 26) |
| component | 1 | `a46c2efc85296ea352321aa40c73ec9a71b5d5c66c9f3397d17f9dc8ee6ac23d` | no (schema 26) |
| extension | 1 | `ec13f723b3e23da6c06ae8e23928fa9a376288db767ada5033ef88c3d118a7db` | yes |

### Results

libFuzzer final statistics; cov and ft are from the final `DONE` line. Corpus is the
number of files retained on disk. An asterisk marks the supplementary run. The R083.e
columns repeat its cov and ft for comparison.

| Target | Elapsed | Executions | Exec/s | cov | ft | Peak RSS | Corpus | Findings | R083.e cov / ft |
| --- | --: | --: | --: | --: | --: | --: | --: | --- | --: |
| native-json | 2 s | 2 | — | — | — | 59 MB | 2 | F1 on seed | — |
| native-json* | 301 s | 1,715,678 | 5,699 | 7,717 | 23,741 | 507 MB | 367 | none | 6,939 / 20,937 |
| native-container | 2 s | 2 | — | — | — | 59 MB | 2 | F1 on seed | — |
| native-container* | 301 s | 2,020,714 | 6,713 | 7,429 | 14,581 | 542 MB | 90 | none | 6,716 / 13,005 |
| glb | 2 s | 2 | — | — | — | 89 MB | 1 | F1 on seed | — |
| glb* | 301 s | 12,637 | 41 | 13,743 | 20,886 | 132 MB | 154 | none | 13,189 / 19,838 |
| stl-binary | 301 s | 4,909,505 | 16,310 | 326 | 753 | 522 MB | 77 | none | 326 / 753 |
| stl-ascii | 301 s | 1,454,528 | 4,832 | 574 | 2,366 | 568 MB | 147 | none | 537 / 1,944 |
| obj | 302 s | 1,468,262 | 4,877 | 877 | 4,583 | 569 MB | 481 | none | 876 / 4,546 |
| dxf | 301 s | 2,438,921 | 8,102 | 1,341 | 5,600 | 589 MB | 463 | none | 1,341 / 5,085 |
| template | 2 s | 2 | — | — | — | 59 MB | 1 | F1 on seed | — |
| template* | 302 s | 2,355,411 | 7,825 | 7,447 | 13,174 | 560 MB | 40 | none | 6,809 / 12,015 |
| component | 1 s | 2 | — | — | — | 59 MB | 1 | F1 on seed | — |
| component* | 302 s | 1,814,164 | 6,027 | 9,562 | 15,461 | 543 MB | 42 | none | 8,831 / 14,485 |
| extension | 2 s | 2 | — | — | — | 59 MB | 1 | F1 on seed | — |
| extension* | 301 s | 118,512 | 393 | 3,048 | 3,935 | 522 MB | 79 | none | 3,046 / 3,873 |

The main run took 10 min 8 s and the supplementary run 15 min 4 s of wall time. Every
target reached equal or higher coverage than in R083.e. The four native-format targets
reach more code at schema 26, as expected after the schema 25 and 26 parser changes.

### F1 at the RC

The six crash artifacts are byte-identical to an unmodified seed: native-json and
native-container `empty-document`, and the `block` seed of glb, template, component and
extension. Each report is the same ASan `alloc-dealloc-mismatch (operator new vs free)`
as in R083.e: allocated at `libQt6Core.so.6+0x241dc1` and freed at `+0x241f16`, under
`QJsonDocument::fromJson`. The callers are `decodeDocument` (`document_io.cpp:734`, which
moved from line 574 in R083.e), `decodeContainer`, `GltfPackage::Impl::open`,
`decodeBundle` and `parseExtensionManifest`. R083.e's minimized input `{"":2}` (SHA-256
`cfd6d63e…660d1c`) reproduced twice on the RC native-json target, each with exit 134. This
is the known D-022 finding, unchanged. It is not a new finding, so nothing was minimized
again and no code was changed.

## Comparison

| Check | R083.d / R083.e | R083.h at the RC |
| --- | --- | --- |
| Sanitized 13-suite matrix | 13/13 in 296.33 s at `7563bbed` (R083.d) | 13/13 in 200.96 s at `7b01bb7a` |
| Fuzz targets to time, documented options | 4 of 10 (R083.e, `6bdfe8de`) | 4 of 10 |
| Supplementary run, `alloc_dealloc_mismatch=0` | 6 of 6 to time, no finding | 6 of 6 to time, no finding |
| F1 (D-022) | Open; root cause not established | Reproduces unchanged; still open |
| Native seed schema | 24 | 26 |

## Limits

A finite 300 s campaign gives measured coverage, not proof that every malformed file is
safe. The six JSON-based targets meet the documented options only with the one ASan
check disabled, as in R083.e. The campaign is not in CI. The reduced RSS limit and
shared host memory affect throughput. Intentional process-kill tests in the matrix model
process interruption, not physical power loss (D-014).
