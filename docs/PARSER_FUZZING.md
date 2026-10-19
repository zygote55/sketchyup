# Coverage-guided parser checks

The opt-in Clang/libFuzzer targets instrument the linked SketchyUp and bundled
parser code with coverage, AddressSanitizer and UndefinedBehaviorSanitizer.
There is one target per native JSON/container, GLB, binary/ASCII STL, OBJ, DXF,
template, component and extension format, and one for component parameter
expressions (`parameter-expression`, R093.a; [ADR 0165 §7](decisions/0165-constrained-component-parameters.md)),
and one for the schema-29 `terrain-mesh` chunk decoder (`terrain-mesh`, R095.a;
[ADR 0168 §2](decisions/0168-terrain-import-and-surfaces.md)), whose accepted input must
stay within the tile and document limits and re-encode to exactly its bytes.
Two more cover the R095.c terrain sources: `terrain-dxf` (the DXF terrain mode, metres) and
`terrain-points` (CSV/XYZ/PNEZD point files, metres, delimiter, header and columns detected).
An accepted source must have finite coordinates within the 100,000 km site bound, valid layer
or code indices and polylines of at least two vertices; the harness then runs the whole import
dry run (preparation and triangulation), which must not throw for bad data, must keep its
report within 64 KiB with at most 64 kinds, 32 locations a kind and 256-byte messages, and,
when it imports, must give a non-empty mesh within the triangle limit with in-range indices.
Their seeds are the generated fixtures of `tests/terrain_import_fixtures.hpp` (the metre and
UTM contour DXFs, the invalid DXF, the UTM XYZ file, a PNEZD file with coded breakline points
and one with bad values) plus a rejected DXF version and a rejected two-column point file;
`parser_fault_corpus_tests` mutates a slice of the contour DXF and a PNEZD file 512 times each.
Existing deterministic parser-fault and transaction tests remain required.

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

Run every target, including the six JSON-based ones, with these options; no ASan check
is disabled. The targets link `tests/fuzz/fuzzer_allocation.cpp`, which gives the nothrow
`operator new` and `delete` the same definitions as the rest of the target. Without it, the
libFuzzer runtime's private libc++ supplies every other form while ASan supplies the nothrow
ones, and Qt's JSON parser (a nothrow `new` released by a sized `delete` in libstdc++'s
`std::stable_sort` buffer) stopped on its first seed with a false alloc-dealloc mismatch
(D-022). See [R083.i](verification/R083i-fuzz-asan.md).

A `parameter-expression` input is one formula. It is parsed, then checked and
evaluated against a fixed table with one parameter of every type. Its invariants:
an accepted formula's canonical text re-parses to the same tree, checks the same
way and evaluates to the same bits; a rejected one gives a code from the closed
`PARAMETER_*`/`LIMIT_EXCEEDED` set with an offset inside the input; the token,
node, depth, value-stack and node-visit counters stay within their limits. The
in-ctest `parameter_expression_fuzz` runs the same harness over 20,000 generated
formulas and seed mutations in the `dev` and `sanitize` presets.

A `terrain-triangulation` input (R095.b; [ADR 0168 section 3](decisions/0168-terrain-import-and-surfaces.md))
is a compact binary description of one adapter call, in the layout documented in
`tests/fuzz/terrain_triangulation_harness.hpp`: contours, breaklines, points, an optional
boundary and holes on a sixteenth-of-a-metre grid, and the options. It needs neither Qt nor
the parsers, so its targets build with the rest of `parser_fuzzers`:
`terrain_triangulation_fuzz_seeds DIR` writes the seeds (accepted terrains and the three
refusal kinds), and `parser_fuzz_terrain_triangulation` runs with the options above and the
same `fuzzer_allocation.cpp`. Every byte string is a valid input. The harness runs each input
twice and requires identical bits, then checks that an accepted mesh has in-range, sorted,
counter-clockwise, edge-manifold triangles with no unused vertex and within the triangle
budget, and that a refusal returns no mesh and lists bounded counts, locations and messages.
The in-ctest `terrain_triangulation_tests` run the same harness over generated inputs and
seed mutations in the `dev` and `sanitize` presets.

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
