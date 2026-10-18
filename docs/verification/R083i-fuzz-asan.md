# R083.i — Parser fuzz targets under ASan: D-022 root cause and fix

2026-10-18. Source: `main` at `8a2619b1` plus branch `test/d022-fuzz-asan`. Local evidence,
not release acceptance.

[D-022](R089a-defect-triage.md) (F1 in [R083.e](R083e-parser-fuzzing.md) and
[R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md#f1-at-the-rc)): the six JSON-based libFuzzer
targets stopped on their first valid seed with an ASan `alloc-dealloc-mismatch (operator new vs
free)` inside `QJsonDocument::fromJson`. This record finds the cause in how the libFuzzer
runtime is linked, fixes it in the fuzz harness with the check still enabled, and re-runs the
documented campaign.

- **Cause:** Qt allocates and frees with a correct pair (nothrow `new`, sized `delete`), but in a
  `-fsanitize=fuzzer` link the two operators come from different runtimes: the nothrow `new`
  from ASan, the `delete` from libFuzzer's private libc++, which calls `free`.
- **Fix:** the fuzz targets define the nothrow `new`/`delete` forms
  (`tests/fuzz/fuzzer_allocation.cpp`), so every operator in the target comes from one family.
  No ASan option is changed.
- **Result:** all 11 targets pass every seed and run 60 s each under the documented options
  with no finding.

## Environment

Omarchy (Arch), Linux 7.2.5, clang/compiler-rt 22.1.8 (`compiler-rt 22.1.8-1`), Qt 6.11.2
(`qt6-base 6.11.2-3`, `libQt6Core.so.6` build ID `3e89a2b2af7893db9e9a63269b878a94627e4142`),
GCC 16.2.1 `libstdc++`, CMake 4.4.3, Ninja 1.13.2.

## Root cause

### The two frames in Qt

The ASan report names `libQt6Core.so.6+0x241dc1` (allocation) and `+0x241f16` (release). Arch's
debuginfod has no debug file for this Qt build, so the frames were read from the disassembly:

```text
241dbc: call *...(%rip)   # _ZnwmRKSt9nothrow_t@GLIBCXX_3.4   operator new(size_t, std::nothrow_t const&)
241f11: call *...(%rip)   # _ZdlPvm@CXXABI_1.3.9              operator delete(void*, size_t)
```

The allocating loop is libstdc++'s `std::_Temporary_buffer` (the `std::stable_sort` buffer):
it requests half the range in 32-byte elements (a pair of `QtCbor::Element`), caps the count at
`PTRDIFF_MAX / 32`, and halves it when the nothrow `new` returns null. Qt's JSON parser sorts an
object's members by key, so any object with a member, such as `{"":2}`, reaches it. A nothrow
`new` released by a sized `delete` is a valid pair in C++; this code is compiled into Qt with
GCC, so nothing in a SketchyUp header or compile flag takes part. The libc++ vs libstdc++ and
`-fsized-deallocation` angles therefore do not apply: our targets compile none of this code.

### Which runtime each operator binds to

`clang++ -fsanitize=fuzzer,address -###` shows the driver links
`--whole-archive libclang_rt.fuzzer-x86_64.a` **before** `libclang_rt.asan_cxx-x86_64.a`.
Arch's fuzzer archive embeds a private libc++ (namespace `std::__Fuzzer`). Its `nm` shows:

| Operator | In `libclang_rt.fuzzer-x86_64.a` | In `libclang_rt.asan_cxx-x86_64.a` | Bound in the target |
| --- | --- | --- | --- |
| `new`, `new[]`, aligned `new` | `W` (weak, global) | `W` | libFuzzer's (first weak definition wins) |
| every non-nothrow `delete` (plain, sized, aligned) | `W` | `W` | libFuzzer's |
| nothrow `new`, nothrow `delete` | `t` (local) | `W` | ASan's |

In the linked minimal program, `_ZdlPvm` tail-calls `_ZdlPv`, which jumps to
`__interceptor_trampoline_free`, and `_Znwm` (in the `__lcxx_override` section) calls
`__interceptor_trampoline_malloc`, while `_ZnwmRKSt9nothrow_t` is ASan's interceptor. So
ASan records Qt's buffer as `operator new` and then sees it released by `free`. A program linked
with `-fsanitize=address` alone has no fuzzer archive, every operator is ASan's, and nothing is
reported; that is why `parser_fuzz_seeds` and the sanitized test suites never showed it.

A control confirms the scope: `int *p = new int[4]; free(p);` is reported as `operator new []
vs free` in a plain ASan build and is silent in a libFuzzer build. In these targets ASan's
new/delete-family check was already blind for all code except the nothrow forms.

### Shared libraries (PR #350)

PR #350 builds shared libraries only under `SKETCHYUP_SANITIZE`. The fuzz configuration sets
`SKETCHYUP_BUILD_FUZZERS` alone, so its libraries stay static and the change does not reach it.
Symbol binding would not change either way: definitions in the executable take precedence over
those in its shared libraries.

## Resolution

Option (a), a build-side fix. `tests/fuzz/fuzzer_allocation.cpp` is linked into each libFuzzer
target (not into `parser_fuzz_seeds`). It defines the four nothrow `new` forms (plain, array,
aligned, aligned array) as calls to the ordinary `new` inside `try`/`catch` returning null,
and the four nothrow `delete` forms as calls to the ordinary `delete`. Its strong definitions
take precedence over ASan's weak ones, so every operator in the target is the libFuzzer
runtime's, and every allocation and release goes through ASan's `malloc`/`free`.

Swapping the link order instead (ASan's operators first, libFuzzer linked by hand afterwards)
was tried and rejected: libFuzzer's own libc++ aborts at start-up ("`operator new(size_t)` has
been overridden, but `operator new(size_t, nothrow_t)` has not") because it requires its own
`new` to stay in place.

**What it gives up.** Nothing that the targets had. In a libFuzzer link ASan already could not
detect a new/delete-family mismatch (the control above), because every form but the nothrow
ones was already `malloc`/`free`; the fix brings the nothrow forms into line, and
`alloc_dealloc_mismatch` stays at its default. Unlike `alloc_dealloc_mismatch=0`, it needs no
option in the campaign command, so a future runtime that does bind ASan's operators gets the
full check without a documentation change. Checking `new`/`delete` pairing remains the job of the `sanitize`
preset suites, which do not link libFuzzer and are unchanged. No ASan option is disabled
anywhere.

## Commands

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -gline-tables-only' \
  -DBUILD_TESTING=OFF -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=OFF \
  -DSKETCHYUP_BUILD_FUZZERS=ON
cmake --build build/fuzz --target parser_fuzzers --parallel 4
build/fuzz/parser_fuzz_seeds "$CAMPAIGN/seeds" > "$CAMPAIGN/seeds.json"
cp -r "$CAMPAIGN/seeds" "$CAMPAIGN/corpus"
# per format, two at a time:
TMPDIR="$CAMPAIGN/scratch" ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/fuzz/parser_fuzz_<format> "$CAMPAIGN/corpus/<format>" \
  -max_len=1048576 -max_total_time=60 -timeout=10 -rss_limit_mb=2048 \
  -detect_leaks=1 -print_final_stats=1 -artifact_prefix="$CAMPAIGN/artifacts/<format>/"
```

This is the [documented command](../PARSER_FUZZING.md) with `-max_total_time=60`, and, as in
R083.e and R083.h on this shared host, `-rss_limit_mb=2048` and two targets at a time. Debug
information was reduced to line tables to save disk. The build took 10 min 8 s at
`--parallel 4` and used 990 MB.

## Results

`parser_fuzz_seeds` exited 0: 11 formats, 32 valid seeds that passed the accepted-result
invariants, and 2 rejection seeds (native JSON) that the parser refused as expected.

**Before and after, same build.** `parser_fuzz_native_json` relinked from the same objects
without `fuzzer_allocation.cpp.o` still aborts with exit 134 and `alloc-dealloc-mismatch
(operator new vs free)` on R083.e's `{"":2}` and on the `empty-document` seed. The target with
the file runs `{"":2}` with exit 0.

**Seeds.** Each target run on its seed files (no mutation) exited 0 with no sanitizer report:
native-json 7/7, native-container 5/5, glb 1/1, stl-binary 1/1, stl-ascii 1/1, obj 1/1,
dxf 1/1, template 2/2, component 2/2, extension 1/1, parameter-expression 12/12.

**60 s campaign.** libFuzzer final statistics; cov and ft are from the `DONE` line. Every target
ran to time with exit 0, no crash, leak, timeout, sanitizer report, invariant failure or
RSS-limit breach, and no artifact. The whole campaign took 6 min 16 s of wall time.

| Target | Seeds | Executions | Exec/s | cov | ft | Peak RSS | Corpus | Findings |
| --- | --: | --: | --: | --: | --: | --: | --: | --- |
| native-json | 7 | 124,762 | 2,045 | 17,543 | 59,413 | 540 MB | 443 | none |
| native-container | 5 | 233,460 | 3,827 | 16,526 | 42,227 | 555 MB | 92 | none |
| glb | 1 | 1,408 | 23 | 13,997 | 15,949 | 136 MB | 40 | none |
| stl-binary | 1 | 331,628 | 5,436 | 326 | 747 | 529 MB | 74 | none |
| stl-ascii | 1 | 147,810 | 2,423 | 483 | 1,396 | 507 MB | 72 | none |
| obj | 1 | 201,515 | 3,303 | 818 | 3,186 | 518 MB | 287 | none |
| dxf | 1 | 196,869 | 3,227 | 1,285 | 3,768 | 485 MB | 239 | none |
| template | 2 | 80,839 | 1,325 | 14,293 | 19,011 | 533 MB | 36 | none |
| component | 2 | 80,177 | 1,314 | 16,487 | 22,723 | 533 MB | 34 | none |
| extension | 1 | 13,778 | 225 | 3,823 | 4,553 | 532 MB | 52 | none |
| parameter-expression | 12 | 133,464 | 2,187 | 2,096 | 5,876 | 494 MB | 427 | none |

The cov and ft values are not comparable with R083.e and R083.h: the targets now link more
code (native schema 28, the parameter-expression parser) and the runs are 60 s, not 300 s.

Seed-set SHA-256 (over `sha256sum ./*` in each format directory, as in R083.e):

| Format | Seed-set SHA-256 |
| --- | --- |
| native-json | `b9840d34d4f6a81bdded4df436e7e555b0588b4f628f021261ddb83f6e7cf8b2` |
| native-container | `174325cce78fc4738fe98bd262420c5faba57a33846dff38953e3c8ff1ee619b` |
| glb | `672d8b3a4e72ac0d52fb91bbfc8ff6f0154748570ff885da749c5b60d0fe0380` |
| stl-binary | `6b30f10cd9b74297b7aa51a1be9f9ff88c44de274cc8e97001096855dbef58fd` |
| stl-ascii | `0930c147e524735246f6a13ea6b78553f606f0f764843cadf19cbc8a97702a6c` |
| obj | `ce660cddf1ee4efffdbfee8cb8d7442d54703369687fe342cce3c68ae74543b6` |
| dxf | `77b3a7667050a722ea74eddc7d6b4e595e2b0f9413529ba23da2a0d18fc42fe5` |
| template | `a983e1253af6cd5105b5619e04c5d37ce765148893e375f0688e8ed92db4767e` |
| component | `63b39132c833352347ef39ec6d827f21ea404bb34b205d6c045596cfdda68cf8` |
| extension | `ec13f723b3e23da6c06ae8e23928fa9a376288db767ada5033ef88c3d118a7db` |
| parameter-expression | `22bcbbbccf7c7a5b028fc31817d0aef32cd9ef662ca8ef3571bf840f29cd8d0a` |

Corpora, logs and binaries stayed outside the repository and were deleted afterwards.

## Continuous fuzzing recommendation

Not added; this is for the owner to decide.

- **Job.** One Linux job on `main` pushes (or nightly), not on pull requests: configure as
  above, build `parser_fuzzers` (about 10 min at four cores locally; the build is the largest
  cost), generate seeds, then run the 11 targets two or more at a time for 120 s each with the
  documented options. About 12 min of fuzzing at two lanes, about 25 min in all, inside a
  30 min job limit. Fail the job on any artifact, and upload the artifacts and logs.
- **Corpus caching.** Cache `$CAMPAIGN/corpus` with `actions/cache`, keyed on the seed
  generator's hash and a run number and restored from the latest key with a prefix match, so
  each run continues the last corpus. Merge the regenerated seeds into the restored corpus
  first, and run `-merge=1` at the end to keep the cache small (the 60 s corpora above are
  34–443 files per target, about 11 MB on disk together).
- **Pull requests.** A cheaper per-PR check is to run each target on the seeds and the cached
  corpus only (`-runs=0`), which takes seconds once built; it catches regressions on known
  inputs without fuzzing.
- **Toolchain.** The fix depends on how Clang's libFuzzer runtime is linked; pin the CI clang
  version with the image, and keep the minimal `{"":2}` run as the first step so a toolchain
  change that reintroduces D-022 fails clearly.

## Limits

A 60 s campaign is a smoke run under the documented options, not the 300 s campaign of R083.e
and R083.h. The Qt frames were identified from the disassembly because no debug file was
available for this Qt build. The fix is verified with clang 22.1.8 and Arch's
compiler-rt; another compiler-rt build that localizes all its operators would not need it and
is unaffected by it.
