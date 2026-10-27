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
One more covers the R096.a terrain edit list decoder (`terrain-edits`;
[ADR 0169 §7](decisions/0169-terrain-editing-drape-stamp.md#7-persistence)): its input is the
JSON `edits` array of a terrain record. The harness decodes the production kinds (R096.c:
`editSource`, `addSource`, `removeSource`; R096.b: `sculpt`, `smooth`; R096.d: `stamp`; R096.e:
`drape`) and registers the
test-only kinds of `tests/terrain_edit_test_kinds.hpp` (`test.lift`, `test.split`,
`test.refine`). An accepted list has 1 to 4,096 entries with unique ids within the 8 MiB bound
and re-encodes to a list that decodes to equal entries. Its seeds are a lift and a split as
applied to the schema-29 seed terrain, the three source edits, a brush sculpt and a region
smooth, a resolved daylight pad and an unresolved fixed-width pad, a resolved drape (open and
closed lines, a paint region with a hole, its runs and a split contour segment) and an unresolved
one, a rejected list one entry over the bound, a rejected contour at two elevations, a rejected
smooth of nine iterations, a rejected pad whose rays run backwards and a rejected drape whose
split names its segment's larger end first; `parser_fault_corpus_tests` mutates the valid list
(with two source edits, a sculpt, a smooth, a pad and a drape) 512 times.
One more covers the R099.a presentation records (`presentation`; [ADR 0173 section 3](decisions/0173-camera-paths-batch-output.md#3-the-render-preset-record)):
the `presentation` object of a schema-30 document, with its camera path table (at most 64
records of 2 to 32 keyframes and at most 240 planned frames) and its render preset table (at
most 16). The harness decodes each input twice and requires equal results; an accepted object
must be within every bound, with allocators above its identities, and must re-encode to a
canonical object that decodes to equal tables and encodes to the same bytes. Its seeds are a
tour with a broken path and two presets, an empty table, and three rejected inputs (a path of
one keyframe, a keyframe of zero and a preset 63 pixels wide).
One more covers the R100.a edge stroke (`edge-style`; [ADR 0174 section 2](decisions/0174-custom-styles-asset-authoring.md#2-the-extended-style-record-and-schema-decision-2)):
the `edgeStroke` object of a schema-31 style, with its width (1 to 8 logical pixels), opacity
(0.01 to 1), named dash and canonical texture asset id (zero for none). The harness decodes
each input twice and requires equal results; an accepted stroke must be within its bounds and
re-encode to a canonical object. Its seeds are four accepted strokes (the defaults, both bounds
with a texture, a dashed and a dotted stroke with the largest id) and eight rejected inputs (a
width of 0.5 and of 8.5, an opacity of zero and of 1.5, an unknown dash, a non-canonical id, a
negative id and an unknown field).
One more covers the R107.a geographic site record (`geographic-site`; [ADR 0181 section 2.1](decisions/0181-maps-geolocation-feasibility.md#21-stored-form)): the site origin's `geographic`
object, with its latitude and longitude, datum label, UTM zone and hemisphere, convergence, vertical
reference and provenance. The harness decodes each input twice and requires equal results; an accepted
record must be within its zone and latitude bounds and re-encode to a canonical object. Its seeds are
four accepted records (London typed, the 84°N bound with NAD83, the 80°S bound and a southern imported
record with a digest) and eleven rejected inputs (OSGB36 and ETRS89 datums, a latitude above 84°N, zone
61, a Norway exception position, an unknown vertical reference, a digest that does not match the
provenance kind, a hemisphere that does not match the latitude, a convergence that drifts and an unknown
field).
One more covers the R100.b2 material texture size (`material-texture-size`; [ADR 0174 section 3](decisions/0174-custom-styles-asset-authoring.md#3-expanded-material-presets-decision-3)):
the `textureSize` object of a schema-33 material, with its width and height in metres (0.01 to 100).
The harness decodes each input twice and requires equal results; an accepted size must be within its
bounds and re-encode to a canonical object. Its seeds are three accepted sizes (the two bounds
together, a brick at 0.90 m, and a non-square wool tile at 0.27 by 0.28 m) and five rejected inputs (a width
below 0.01, a height above 100, a string height, a missing height and an unknown field).
One more covers the R097.b IFC conversion package decoder (`ifc-package`;
[ADR 0171 section 1.6](decisions/0171-ifc-exchange-subset.md#16-process-isolation-decision-19)): the bounded,
versioned container the isolated IFC helper writes and the application decodes (a manifest of at most 32 MiB,
a mesh chunk of at most 128 MiB with its SHA-256). The harness decodes with the digest check off, so mutated
meshes reach the structural checks, and again with it on; an accepted package must be within every bound
(1,000,000 triangles, 100,000 per mesh, node and mesh references in range, parents before children, a tree at
most 128 deep), must re-encode to a package that decodes to an equal one, and the two decodes must agree.
Its seeds are `tests/ifc_package_fixtures.hpp`'s sample package, an empty one, the three packages the helper
wrote from buildingSMART samples (`tests/fixtures/ifc/packages`) and three rejected ones (truncated, bad magic,
a parent after its child); `parser_fault_corpus_tests` mutates the sample and a real package 512 times each.
The helper itself (IfcOpenShell's parser and OpenCASCADE's geometry) is not in a libFuzzer target: its
input is the IFC file, it runs in a child process, and `tests/ifc_worker_fault_corpus.py` mutates the ten
buildingSMART files 512 times each through its dry run (J2); the upstream `ifcparse_fuzzer` and a `worker`
target are the J3 campaigns of the acceptance record.
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

A `terrain-cavity` input (R096.c; [ADR 0169 §3](decisions/0169-terrain-editing-drape-stamp.md#local-re-triangulation-cavity-api))
describes one call of the cavity API, in the layout documented in
`tests/fuzz/terrain_cavity_harness.hpp`: a base point set on the same sixteenth-of-a-metre grid,
a disc choosing the cavity (the base triangles whose centroid lies in it), a stride choosing kept
constraints among its interior edges, and new contours, breaklines and points. Its targets build
with the triangulation's (`terrain_triangulation_fuzzers`): `terrain_cavity_fuzz_seeds DIR`
writes the seeds (re-triangulated cavities and four refusals of three kinds) and
`parser_fuzz_terrain_cavity` runs with the same options. Every byte string is a valid input. The
harness triangulates the base, cuts the cavity out and re-triangulates it three times: twice as
given, requiring identical bits, and once with the kept vertices in reverse order, requiring the
same triangles by position (the input is canonicalised). An accepted result must keep the kept
vertices bit for bit, have sorted counter-clockwise triangles with no directed edge twice, keep
every boundary edge in its direction with no other open edge, keep the cavity's area, contain
every kept constraint and new segment as an edge and use every vertex; a refusal returns no mesh
and lists bounded counts, locations and messages, and never `triangulation-failed`.
`terrain_triangulation_tests` run the same harness over 3,000 generated cavities and seed
mutations.

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
