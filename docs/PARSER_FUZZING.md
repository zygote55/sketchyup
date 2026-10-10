# Coverage-guided parser checks

The opt-in Clang/libFuzzer targets instrument the linked SketchyUp and bundled
parser code with coverage, AddressSanitizer and UndefinedBehaviorSanitizer.
There is one target per native JSON/container, GLB, binary/ASCII STL, OBJ, DXF,
template, component and extension format. Existing deterministic parser-fault
and transaction tests remain required.

Configure a separate build and generate valid, deterministic seeds:

```sh
cmake -S . -B build/fuzz -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS_RELWITHDEBINFO='-O1 -g' \
  -DBUILD_TESTING=OFF -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=OFF \
  -DSKETCHYUP_BUILD_FUZZERS=ON
cmake --build build/fuzz --target parser_fuzzers --parallel 2
build/fuzz/parser_fuzz_seeds build/fuzz-seeds > build/fuzz-seeds.json
```

Run each target against its corresponding corpus in a resource-limited process.
Keep scratch, output corpora, logs and failure artifacts on a disk with adequate
space. For example, after copying the original seeds into a writable campaign
directory and creating its artifact directory:

```sh
TMPDIR="$PWD/build/fuzz-scratch" ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  build/fuzz/parser_fuzz_native_json build/fuzz-corpus/native-json \
  -max_len=1048576 -max_total_time=300 -timeout=10 -rss_limit_mb=3072 \
  -detect_leaks=1 -print_final_stats=1 -artifact_prefix=build/fuzz-artifacts/
```

With Qt 6.11.2, the six JSON-based targets (native JSON/container, GLB, template,
component and extension) stop on their first valid seed with an ASan
alloc-dealloc mismatch reported entirely inside `QJsonDocument::fromJson`. A
libFuzzer program that links only Qt reproduces it; plain ASan does not. Run those
targets with `alloc_dealloc_mismatch=0` added to `ASAN_OPTIONS` and record that
option with the results. See [R083.e](verification/R083e-parser-fuzzing.md).

The harness caps generated inputs at 1 MiB. This is a campaign bound; the
production file limits remain covered by separate boundary tests. Expected
validation exceptions count as parser rejection. Allocation failure, crashes,
sanitizer findings and exceptions after a parser accepted a result are failures.
Accepted native/imported documents must survive canonical container validation;
surface-source parsers retain coordinate and reference invariants. GLB parsing
uses one private scratch file and the production sidecar confinement checks.

Preserve the source revision, compiler/library versions, seed hashes, exact
command, elapsed time, executions, coverage/features, retained corpus and failure
artifacts. A finite successful campaign provides measured coverage, not proof
that every malformed file is safe. Minimize and reproduce every finding before
adding a regression. The fuzz targets are disabled by default and are not
installed in the application package.

The instrumentation and corpus workflow follows the
[LLVM libFuzzer documentation](https://llvm.org/docs/LibFuzzer.html).
