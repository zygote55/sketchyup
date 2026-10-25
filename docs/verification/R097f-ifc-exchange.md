# R097.f — IFC exchange subset: acceptance record

2026-10-25. Branch `docs/r097f-ifc-acceptance`, based on `main` at `78058393` (R098.c merged as
PR #438). Product code is unchanged by this record; the one file it adds is the J3 `worker` harness,
`scripts/ifc-fuzz/worker_fuzzer.cpp`. Every check below ran at `78058393` unless its row says otherwise. The checks are those of
[ADR 0171 §6.3](../decisions/0171-ifc-exchange-subset.md#63-checks-for-the-verify-line).

[Summary](R097f-ifc-exchange.json).

**Status.** Every check that a run can prove has been run and passes, with the exceptions named in its row. Two
items are not complete: the buildingSMART hosted validation service, which needs a person (owner, section G1; the export
claim is conditional on it), and the native matrix, which runs on the pull request with the `full-ci` label (section
Native matrix). Budgets: seven of the eight rows measured in §6.4 are met (the cancel row only for the control's return, not the helper's exit); the 100,000-face, 10,000-body row is missed for its 10,000-body case.

The budgets and the claims are in their sections below.

## Check map

Each row names the test or command that proves the check, the result of the run made today, and the
commit it ran at. "Not run" always comes with the reason.

| Check | Clause (ADR 0171 §6.3) | Proof: CTest suite or command | Result today | Commit |
| --- | --- | --- | --- | --- |
| A1 | Units: mm, m and ft fixtures to 1e-9 m | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| A2 | Quantity values equal the raw-text scan's (7 area, 7 volume, 11 length) | `ifc_import_worker` | pass | 78058393 |
| A3 | Missing length unit refused; accepted with the override, reported | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| A4 | Non-SI, non-inch, non-foot length unit refused (furlong) | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| A5 | Property or quantity with its own `Unit` converted or listed | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| B1 | Group tree equals the raw-text scan's by `GlobalId` | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| B2 | Counts per class equal the scan's | `ifc_import_worker` | pass | 78058393 |
| B3 | Element order within a parent follows the file | `ifc_import_worker` | pass | 78058393 |
| B4 | `ifc.Groups` equals the file's group, system and zone membership | `ifc_import_worker` | pass | 78058393 |
| B5 | Tags per class | `ifc_import`, `ifc_convert` | pass | 78058393 |
| B6 | Duplicate `GlobalId`, two parents, containment cycle noticed or reported | `ifc_import_worker` | pass | 78058393 |
| B7 | A layer with `LayerOn` false imports its bodies hidden | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| C1 | Every `Pset.Property` and `Qto.Quantity` equals the scan's | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| C2 | `\X2\` and `\S\` escapes decode | `ifc_import_worker` | pass | 78058393 |
| C3 | 128-per-body cap, 8 MiB aggregate cap, chunk-remainder cap; over-chunk geometry refused | `ifc_convert` | pass | 78058393 |
| C4 | `ifc.GlobalId`, `ifc.Class`, `ifc.Step`, `ifc.Representation` on every body | `ifc_import` | pass | 78058393 |
| C5 | Classification reference retained with its URI | `ifc_import_worker` | pass | 78058393 |
| C6 | Per-face colours equal the file's `IfcIndexedColourMap` | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| D1 | Accounting identity holds on every fixture | `ifc_import_worker`, `ifc_georeference`, `ifc_import` | pass | 78058393 |
| D2 | `unsupported-all.ifc` lists each class, ≤ 32 locations, complete counts | `ifc_import_worker` | pass | 78058393 |
| D3 | Unsupported placement, cycle or no geometry: not imported, located, nothing at the origin | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| D4 | Reference to an undefined ID is a notice, not a crash | `ifc_import_worker` | pass | 78058393 |
| E1 | Native validation (`decodeDocument(encodeDocument())`); save and reopen bit-exact | `ifc_convert`, `ifc_import` | pass | 78058393 |
| E2 | Tessellated and polygonal-profile areas and volumes to 1e-6 relative | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| E3 | Reference View wall volume with the opening subtracted; the uncut file is larger | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| E4 | Coplanar triangles merge into polygon faces; non-planar counted | `ifc_convert`, `ifc_import` | pass | 78058393 |
| E5 | One undo step for the whole import | `ifc_import`, `ifc_import_dialog` (S9) | pass for the import's one undo (`ifc_import`); the desktop suite S9 is not run here (native matrix) | 78058393 |
| E6 | Provenance properties resolve to the right source entity | `ifc_import` | pass | 78058393 |
| E7 | Mesh volumes and areas agree with `NetVolume` and `GrossArea` within 1e-3 | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| F1 | θ = 0 and matching scale set the site origin to E, N, H within 1e-9 m | `ifc_georeference`, `ifc_import` | pass | 78058393 |
| F2 | 60° fixtures: `keepAxes` sets no origin; grid mode maps each vertex | `ifc_georeference`, `ifc_import` | pass | 78058393 |
| F3 | Model beyond ±1e6 m refused, naming the extent | `ifc_georeference` | pass | 78058393 |
| F4 | Survey foot, WCS, two conversions and the other F4 cases handled as specified | `ifc_georeference` | pass | 78058393 |
| G1 | Goldens pass the raw-text scan, `--check`, `ifcopenshell.validate`; hosted service by a person | `ifc_export`, `ifc_export_worker`; `ifcopenshell.validate` by hand (below); hosted service pending: owner | partial: `ifcopenshell.validate` 0 statements on the six goldens, with a positive control (1 statement); the buildingSMART hosted service is **pending: owner** | 78058393 |
| G2 | Export then re-import reproduces bodies, vertices, hierarchy and properties | `ifc_export` | pass | 78058393 |
| G3 | Two exports with a fixed time are byte-identical | `ifc_export`, `ifc_export_worker` | pass | 78058393 |
| G4 | Every non-exported kind is counted; the identity holds | `ifc_export`, `ifc_export_worker` | pass | 78058393 |
| G5 | CRS label of authority-code form required for `IfcMapConversion` | `ifc_export` | pass | 78058393 |
| G6 | Golden exports checked in; CI compares bytes | `ifc_export` | pass | 78058393 |
| G7 | Duplicate and invalid `GlobalId` values get derived identifiers | `ifc_export`, `ifc_export_worker` | pass | 78058393 |
| H1 | Two imports of every fixture give identical encodings | `ifc_import`, `ifc_georeference` | pass | 78058393 |
| H2 | Shuffled instances give the same document except `ifc.Step` and file order | `ifc_import_worker` | pass | 78058393 |
| I1 | 128 MiB limit and over-limit entity count refuse with the limit named | `ifc_worker_run` | pass | 78058393 |
| I2 | Messages ≤ 256 bytes; report ≤ 64 KiB | `ifc_import`, `ifc_convert` | pass | 78058393 |
| I3 | Worker killed, over limit or past deadline: element left out, app keeps running | `ifc_worker_run` | pass | 78058393 |
| J1 | Package decoder: bounds, re-encode; `parser_fuzz_ifc_package` | `ifc_package`, `parser_fault_corpus`; fuzz run (below) | pass (`ifc_package`, `parser_fault_corpus`); the 300-second run of `parser_fuzz_ifc_package`: 1,246,993 runs, no finding | 78058393 |
| J2 | 512 mutations of each certification file through the dry run, in a child | `ifc_worker_fault_corpus_full` (label `full`) | pass (64 and 512 mutations per file); direct 512 run: 2,514 ok, 2,605 refused, 1 crash (`rv-wall-with-opening-and-window.ifc`, index 453) | 78058393 |
| J3 | Upstream `ifcparse_fuzzer` and a `worker` target, ≥ 5 min each, sanitizers | manual campaigns (below) | pass for five minutes per campaign: `ifcparse_fuzzer` 32,923 runs, `worker` 27,463 runs, no finding; the library is not coverage-instrumented (see the fuzz caveats) | 78058393 |
| J4 | Harness runs every input twice and compares | `tests/fuzz/parser_harness.hpp` (`ifc-package`), exercised by the J1 run | pass as the harness is written (the ifc-package decode runs twice and compares; the seed generator and the J1 run exercised it); the two runs differ in one setting | 78058393 |
| K1 | Support matrix row, user guide section, exchange-boundaries entry state exactly the claims | manual, against the report's kinds (below) | pending (claims section below) | 78058393 |
| K2 | Fixture `NOTICE.md` carries CC BY 4.0 and the IFC trademark credit; fixtures unmodified | `ifc_import_worker` | pass | 78058393 |
| L1 | No dynamic dependency on IfcOpenShell or OCCT outside the worker | `ifc_library_install` | pass | 78058393 |
| L2 | `LD_LIBRARY_PATH` replacement makes the worker use the replaced libraries | `ifc_library_install` | pass | 78058393 |
| L3 | Package carries the notices; vendored tree equals the pinned commit | `ifc_library_source`, `scripts/vendor-ifcopenshell.py check` | pass (`ifc_library_source`; the vendored tree equals the pinned archive, 326 files, `scripts/vendor-ifcopenshell.py check`) | 78058393 |
| L4 | `THIRD_PARTY_NOTICES`, PKGBUILD licence array and `dependencies.json` updated | `ifc_library_source` | pass | 78058393 |
| L5 | No GPL-3.0-or-later library linked | `ifc_library_install` (`verify-ifc-library.py`) | pass | 78058393 |
| L6 | Kernel removal reports "kernel unavailable", no abort | `ifc_library_install` | pass | 78058393 |

## Test runs

All run on 2026-10-25 in `sketchyup-ci-pinned:r097a-ifc` (GCC 16.2.1, OpenCASCADE 7.9.3, Boost 1.92.0,
Clang 23.1.1, Python 3.14), four CPUs and 10 GB, the IfcOpenShell prefix from the cache (stamp `7ba70197`,
not rebuilt). Product code at `78058393`. Logs are kept in the build cache and are not committed.

| Build (CMake preset) | Command | Result |
| --- | --- | --- |
| `ifc` (helper only, CI fast tier) | `cmake --preset ifc -DSKETCHYUP_IFC_PREFIX=/ifc-cache/ifc-prefix`, then `cmake --build --preset ifc` | exit 0 |
| `ifc-app` (helper, CLI, IFC tests) | `cmake --preset ifc-app -DCMAKE_LINKER_TYPE=BFD`, then `cmake --build --preset ifc-app --parallel 4` | exit 0, 263 steps, 4 min |

**Helper suite, CI fast-tier selection** (`ctest --test-dir build/ifc -R '^ifc_' -LE full --parallel 2`):
8 of 8 passed, 100 %. `ifc_worker_probe` 2.31 s, `ifc_import_worker` 14.28 s, `ifc_georeference` 6.92 s,
`ifc_export_worker` 7.51 s, `ifc_step_worker` 0.67 s, `ifc_worker_fault_corpus` (64 per file) 30.69 s,
`ifc_library_install` 13.20 s, `ifc_library_source` 0.14 s.

**Application suites, CI selection** (`ctest --test-dir build/ifc-app -R '^(ifc_package|ifc_convert|ifc_import|ifc_worker_run|ifc_export|ifc_step|ifc_step_export|step_import)$'`):
8 of 8 passed. `ifc_package` 0.16 s, `step_import` 1.19 s, `ifc_convert` 7.29 s, `ifc_worker_run` 9.87 s,
`ifc_import` 16.37 s, `ifc_step` 1.49 s, `ifc_step_export` 0.30 s, `ifc_export` 12.06 s.

**`parser_fault_corpus`** is not a target of the `ifc-app` preset, so the first selection reported it
"Not Run". It was built with `cmake --build build/ifc-app --target parser_fault_corpus_tests` (the fast
tier builds it with the application) and passed: 8.48 s.

**The damaged-file corpus at 512 mutations per file** (`ifc_worker_fault_corpus_full`, label `full`):
passed, 278.18 s. The direct run's outcome counts are in the J2 notes below.

**Sanitizers, helper** (`cmake --preset ifc-sanitize`, the full tier's sanitizer step, `ASAN_OPTIONS=detect_leaks=0`): 5 of 5 passed. `ifc_worker_probe` 28.99 s, `ifc_georeference` 93.88 s, `ifc_export_worker` 31.19 s, `ifc_import_worker` 162.96 s, `ifc_worker_fault_corpus` (64 per file) 167.95 s. The library itself is not instrumented in this build; the instrumented library is the J3 build below.

## Budgets (ADR 0171 §6.4)

Measured on this machine, 2026-10-25, in the pinned image with a Release build of the helper and the command line
(`build/release`, `-DCMAKE_BUILD_TYPE=Release`, `BUILD_TESTING=OFF`), four CPUs and 10 GB. Each figure is the
spread of three runs. The helper's time is the `--dry-run` (parse, index and the accounting traversal). Memory is
the peak resident size of the command and its waited-for children, so the import figures include the helper; the
budget is for the application, so these are upper bounds.

| 6.4 budget | Measured | Verdict |
| --- | --- | --- |
| Parse and index a 64 MiB, 1,000,000-instance IFC4 file ≤ 3 s | 0.737 to 0.758 s on a padded 67,108,864-byte file of 1,000,000 points; 274 to 275 MiB resident | **Met** |
| Parse and index a 128 MiB, 2,000,000-instance file ≤ 6 s | 1.461 to 1.469 s on a padded 134,217,728-byte file; 525 MiB resident | **Met** |
| At one instance below the limit, the file is refused with the count named | 1.476 s; `entityLimit`, "The file has 2000000 instances; the limit is 1999999" | **Met** (the budget's 3 s; the text's 1.4 s is 1.48 s) |
| Refuse a file over 128 MiB ≤ 1 s | 0.008 s for a sparse 134,217,729-byte file; `tooLarge`, "larger than 128 MiB (134217729 bytes)" | **Met** |
| Interpret, convert and commit the 100,000-face, 10,000-body model ≤ 10 s | Two measurements, not one model: 100,000 faces and 5,000 bodies (`ico-5k`): 9.01, 9.27 and 9.66 s total, 326 MiB; 9,996 bodies and 59,976 faces (`walls-20k`, 10,004 bodies over the body limit): 11.14, 11.44 and 11.76 s total | **Missed** for the 10,000-body case. The 100,000-face case is met, at 0.3 to 1.0 s of margin |
| Cancel returns control in ≤ 100 ms; the helper ends within 1 s | 1 ms (`ifc_worker_run`, the cancel test). The time the helper takes to exit was not measured separately | **Met** for the return; the exit time is **not measured** |
| Export of the 100,000-face model ≤ 5 s; output ≤ 256 MiB | `ico-5k` as an IFC export through the command line: 1.770, 1.789 and 1.792 s; output 5,166,408 bytes (5.2 MB) | **Met** |
| Application memory during import ≤ 1 GiB | 325 to 326 MiB (`ico-5k`); 404 MiB (`walls-20k`) | **Met** |

**Frozen values.** Only the met rows are frozen, at the measured value: parse 64 MiB at 0.76 s, parse 128 MiB at
1.47 s, the size refusal at 0.01 s, the instance refusal at 1.48 s, the export at 1.79 s, and the application's
memory at 326 MiB. The 100,000-face import row and its 10,000-body case are not frozen: the row is missed as written.

**The 10,000-body case is an ADR problem, not only a speed problem.** The document limits (100,000 unique vertices
and 10,000 bodies) cannot hold a 100,000-face, 10,000-body model at once: a box-like model reaches about 6,000
elements before the vertex limit. The row describes a model the limits do not allow. The owner decides whether to
relax the row to what the limits allow (the 100,000-face model at 5,000 bodies, met), or to change the limits. The
partial import of the 20,000-wall file is the other case the limits produce, and it takes 11.1 to 11.8 s.

**Progress and cancel of the export.** The export's cancel is covered by `ifc_export` (the process runner is cancelled
while it runs, and the scratch directory is removed). Its progress display is in the desktop runner, whose suites
(`ifc_export_dialog`, `ifc_import_dialog`) were not run in this record; see the native matrix below.

**Size of the budget files.** The 6.4 rows name a 64 MiB and a 128 MiB file. The generator in `tests/ifc_budgets.py`
writes 43 MB and 86 MB files, so the padded files above were made for the two parse rows: the instances are the
same points, and the padding is trailing zeros in the first coordinate, a valid STEP number. The generated files
measure 0.69 s and 1.31 s, in the same range.

## J2: the damaged-file corpus, direct run

`tests/ifc_worker_fault_corpus.py` mutates each of the ten buildingSMART files and runs the helper's dry run on each
mutation in a child process. The test passes when every run ends in a defined outcome and the crashes and timeouts stay
under 2 %. The 512-per-file run of the full label passed under CTest (278.18 s). Its outcome counts are printed only
when the script runs by itself, so the same run was repeated directly:

| Mutations (10 files × 512) | `ok` | `refused` | `crash` | `timeout` | `error` |
| --- | --- | --- | --- | --- | --- |
| 5,120 | 2,514 | 2,605 | 1 | 0 | 0 |

- **The crash** is on `rv-wall-with-opening-and-window.ifc`, mutation index 453, seed `208281516466520` (the seed
  is a hash of the file name and the index, so the script reproduces it; no input file is committed, because the
  mutated input is derived from a third-party file). It is a child that died on a signal or a limit (the script's `crash`
  outcome; the script does not record which signal, and the child printed nothing on stderr). The process
  boundary contained it, and the test counts it. It is a finding to report upstream if it is the
  library's, and is not fixed here (the ADR says not to fix library internals). Whether it is the same defect as
  R097.b's finding 9 (`IfcMaterialLayerSetUsage` with a wrongly typed `ForLayerSet`) was not checked.
- **The split differs from R097.b's record** (2,548 `ok`, 2,571 `refused`, 1 `crash` there). The total and the crash
  count agree and the mutations are the same (seeded per file and index). The shift is in the helper's refusals.
  It was not traced to a slice. Later slices added refusals (R097.c's world-coordinate-system refusal and the
  georeference checks), which is a plausible cause, but that is not verified.

## J1 and J3: the fuzz campaigns

**Builds.** Three instrumented builds, each in the pinned image with Clang 23.1.1:

- **The library (J3).** The vendored tree (`third_party/ifcopenshell/cmake`) configured with Clang, ASan and
  UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=all -g -O1 -fno-omit-frame-pointer`), upstream's
  `BUILD_FUZZERS=ON`, the three schemas, IfcGeom and the OpenCASCADE kernel (OCCT 7.9.3); no CGAL, Manifold,
  converter or Python. 340 steps, 28 minutes (15:27 to 15:55 UTC). Its `ifcparse_fuzzer` is upstream's harness
  (`src/ifcfuzz`).
- **The `worker` target (J3).** `scripts/ifc-fuzz/worker_fuzzer.cpp`, committed with this record. It parses the
  input from memory with IfcParse, then iterates its geometry with the OpenCASCADE kernel the way the helper's
  import does (welded vertices, the import's deflections, one thread, the Model context's Body representation),
  and reads every triangulation. It is built with `clang++ -fsanitize=fuzzer,address,undefined` against the
  instrumented library. Upstream's harness stops at the parser, and upstream does not fuzz geometry.
- **The application's package decoder (J1).** `cmake -DSKETCHYUP_BUILD_FUZZERS=ON` with Clang, RelWithDebInfo,
  no desktop and no CLI, and the target `parser_fuzz_ifc_package` (`docs/PARSER_FUZZING.md`). 224 steps, 6 minutes.

**Runs.** Each campaign ran 300 seconds (libFuzzer `-max_total_time=300`) from the seeds. The seeds are the 60
IFC files of `tests/fixtures/ifc/` (for the two library targets) and the eight package seeds of
`parser_fuzz_seeds` (for J1). Options: `-max_len=1048576 -timeout=10` (30 for the worker) `-rss_limit_mb=3072`.
Sanitizer options: `ASAN_OPTIONS=detect_leaks=0` for the two library targets, as ADR F12 requires (IfcParse leaks
the instances of every file it parses); `detect_leaks=1` for J1, whose decoder is ours.

| Campaign | Target | Runs | Exec/s | Peak RSS | Findings |
| --- | --- | --- | --- | --- | --- |
| J3 | `ifcparse_fuzzer` (upstream, IfcParse instrumented) | 32,923 | 109 | 798 MiB | none; no artifact, no sanitizer report |
| J3 | `worker` (parse plus geometry iteration, OCCT kernel) | 27,463 | 91 | 518 MiB | none; no artifact, no sanitizer report |
| J1 | `parser_fuzz_ifc_package` (the application's decoder) | 1,246,993 | 4,142 | 533 MiB | none; no artifact, no sanitizer report |

Exit status 0 for all three. The artifact directories are empty. No crash, timeout or out-of-memory input was
found, so `tests/fixtures/ifc/fuzz-findings/` has nothing to hold, and no upstream report is needed.

**Caveats, which bound what these runs show.**

- **Coverage.** The upstream build adds `-fsanitize=fuzzer` only to its harness target, so libFuzzer's coverage
  feedback covers the harness code, not the library. The campaigns are mutation-driven against the library: the
  `ifcparse_fuzzer` run reached 37 coverage edges and the `worker` run 72, both in the harness. J1's coverage
  (1,248 edges) is real, since its code is ours and instrumented. A library-wide coverage build (the library
  compiled with `-fsanitize=fuzzer-no-link`) would be the stronger campaign. It is not done here; it is a follow-up
  that needs about 30 minutes of build.
- **Length.** Five minutes each, the ADR's minimum per release; the library campaigns have run only that long.
- **J4.** The ADR asks that the harness run every input twice and compare. The `ifc-package` branch of
  `tests/fuzz/parser_harness.hpp` decodes each input twice, once with the digest check off and once with it on, and
  requires the two packages to agree when the digest matches. This satisfies the wording as written, but the two
  runs differ in one setting; a same-setting repeat is not in the harness. The worker's own determinism is H1
  (two imports of each fixture), which passes.

## G1: external validators

ADR 0171 §6.3 G1 asks for two validators on the golden exports, run by hand at acceptance; neither is a CI
gate. The raw-text scan, the helper's own schema check and the other checks of G1 are in the test table above.

### `ifcopenshell.validate` (run here)

The PyPI package `ifcopenshell==0.9.0.post1` (the same engine as the helper, so a consistency check, not an
independent reader) is installed in a throwaway venv inside the pinned image. It runs
`scripts/ifc-external-validators/validate_ifcos.py` with `python -I` on the six goldens in
`tests/fixtures/ifc/export/`. Result: **0 statements on each of the six goldens** (`bsi-ifc4-arch.ifc`,
`gltf-rig.ifc`, `ifc-structure.ifc`, `native-model.ifc`, `obj-concave-paint.ifc`, `rv-individual-colors.ifc`).

A zero count is only meaningful if the rules run, so a positive control was run with the same venv: a copy of
`gltf-rig.ifc` whose owner history sets `ChangeAction` to `.ADDED.` with no `LastModifiedDate` (a violation of the
`CorrectChangeAction` rule, R097.d finding 3). The validator reports **one error statement** on it, so the rules
run. The control is not committed; it is a copy with one changed line.

This is the same engine as the helper, so it is a consistency check, not an independent reader. The second
reader (web-ifc) and the local buildingSMART rule run are R097.d's results and were not repeated here.

### buildingSMART hosted validation service: owner

The hosted service (<https://validate.buildingsmart.org>) needs a signed-in account and a person, so it was
**not run**. The owner runs it on the six files below and fills in the table.

Files to upload, byte for byte as the record's commit has them (`tests/fixtures/ifc/export/`, unchanged since
`78058393`):

| File | Bytes | SHA-256 |
| --- | --- | --- |
| `bsi-ifc4-arch.ifc` | 49,657 | `3bfc4ba100324da7fd6b2edbe715fbd24aa83d032962e14064fb37eed81da581` |
| `gltf-rig.ifc` | 7,054 | `a16c5ca0d023f048b39abf70c70562ba4e4b7de8784a32a21e66b77ae411b233` |
| `ifc-structure.ifc` | 7,367 | `4d7f7f1f53003ab3ebc2829e5884c4e92c6123e30b8a9b2c2f7eb6e2edbaa1c7` |
| `native-model.ifc` | 16,522 | `3e8112d71ee4903a18f11ebd254bb960af1e614524a52b6f30951f0e384656ad` |
| `obj-concave-paint.ifc` | 5,195 | `9300c4daf3dd7d6ec42c3a8d318bac39a83e6167c05946b3b433510747d8c6fa` |
| `rv-individual-colors.ifc` | 2,506 | `2317e150d188cf0cbfea98af2a2dd1980e4a829c990e850a44e84e8290efb495` |

**Where to find them:** the repository at `main` (`78058393`) or this branch, under `tests/fixtures/ifc/export/`.
Check the SHA-256 with `sha256sum` before uploading.

**What to check:** for each file, the service's schema (IFC4), its error and warning counts, and the rule
identifiers of each finding. The export claim (ADR 0171 §2) is not final until this table has a result for
every file. Notes for reading the result:

- The files declare no `ViewDefinition` on purpose (no view claim, ADR 0171 §2). A finding that says the file
  is not a view-conformant file is expected and is not a failure of the export claim.
- The local buildingSMART rule run (R097.d) showed no error and no warning on the six goldens. The
  certified Architecture file reports one warning, `GRF005`, under the same harness; that is the file's own
  issue, not an export issue.
- Record each error or warning with its rule identifier. An error that the local rules did not show is a
  finding: report it, do not edit the goldens.

| File | Errors | Warnings | Rule identifiers or service report link | Date | Result | Reviewer |
| --- | --- | --- | --- | --- | --- | --- |
| `bsi-ifc4-arch.ifc` | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner |
| `gltf-rig.ifc` | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner |
| `ifc-structure.ifc` | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner |
| `native-model.ifc` | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner |
| `obj-concave-paint.ifc` | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner |
| `rv-individual-colors.ifc` | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner | pending: owner |

## Native matrix

The full tier (ADR 0170) runs X11 and Wayland at 1× and 2× (the IFC dialog suites `ifc_import_dialog` and
`ifc_export_dialog`, which include S9, the dirty-model undo, and T7, the cancelled export), the sanitizer step, and
the damaged-file corpus at 512 mutations (label `full`). It runs when the pull request carries the `full-ci` label.
**Pending: the label is added to the pull request of this branch, and its CI run is not finished at the time of this
record.** The desktop suites were not run in this record, so E5's S9 and the export's T7 are not claimed here.

## Published claims (K1)

The claims are published in three places, and each states the same schemas and directions:

- the **support matrix** row (`docs/SUPPORT_MATRIX.md`, Exchange boundaries, "IFC (F04, not in release 1.3.0)"),
  which marks the row as not part of release 1.3.0;
- the **user guide** section "IFC import and export" (`docs/USER_GUIDE.md`);
- the **exchange-boundaries entry** for F04, which is the same support-matrix row.

What is claimed, and no more:

- **Import:** the schemas `IFC2X3`, `IFC4` and `IFC4X3_ADD2`, into a new unsaved model. The code accepts exactly these
  three (`src/io/ifc_worker.cpp` and `src/ifc/import.cpp`); any other schema is refused, with the schema named.
- **Export:** `IFC4` only, tessellated, with no view definition. The code writes exactly that (`src/ifc/export.cpp`,
  `FILE_SCHEMA` and `FILE_DESCRIPTION`). **The export claim is conditional** on the owner's buildingSMART hosted run
  (G1 above); the support-matrix row says so.
- **Not claimed:** IFC2X3 and IFC4X3 export, `.ifczip`, `.ifcXML`, `.ifcJSON` and IFC5 (refused with the format named,
  tested by `ifc_worker_run`), IFC4X1 and IFC4X2, the IFC 4.3 release candidates and ADD1 (refused with the schema
  named; `ifc_worker_run` tests IFC4X1 and a file that declares two schemas, the others by the same rule), advanced breps, NURBS, CSG, swept solids,
  revolutions and parametric profiles, and the 4.3 infrastructure entities.
- **Unsupported classes as the report names them:** the tests of `ifc_import_worker` check that `IfcGrid` and
  `IfcRelNests` are counted as omitted (`omittedEntities`), and that `IfcAnnotation` and `IfcVirtualElement` are not
  imported with the reason `unsupportedClass` (D2). The user guide names those classes; the support matrix names the
  kinds (4.3 infrastructure, the advanced geometry classes and the other file formats).
- **Not written on export:** the kinds the export dialog and report list (terrain, terrain tiles, reference images,
  guides, loose edges, curves, annotations, sections, scenes, sheets, component parameters, textures, empty groups and
  bodies without faces), counted by G4.

**K1 result: pass as written, on the text published here.** The schemas and directions above match the code paths cited;
the unsupported classes match D2's assertions. The desktop screens' wording of the report is counts and notices; the
per-class lists are in the command line's report, which the user guide says.

## Commands

Every command runs in the pinned image `sketchyup-ci-pinned:r097a-ifc`, with the IfcOpenShell prefix
cache mounted at `/ifc-cache` (its stamp matches the pin `7ba70197`), four CPUs and 10 GB.

### Commands run for this record

Each command ran inside the pinned image with `--cpus=4 --memory=10g --memory-swap=10g`, the worktree mounted at
`/src` and the IfcOpenShell prefix cache at `/ifc-cache`, one job at a time on the machine.

- Build: `cmake --preset ifc -DSKETCHYUP_IFC_PREFIX=/ifc-cache/ifc-prefix`, `cmake --build --preset ifc`, then
  `cmake --preset ifc-app -DCMAKE_LINKER_TYPE=BFD` and `cmake --build --preset ifc-app --parallel 4`.
- Tests: `ctest --test-dir build/ifc -R '^ifc_' -LE full --parallel 2`;
  `ctest --test-dir build/ifc-app -R '^(ifc_package|ifc_convert|ifc_import|ifc_worker_run|ifc_export|ifc_step|ifc_step_export|step_import)$' --parallel 2`;
  `cmake --build build/ifc-app --target parser_fault_corpus_tests`, then `ctest -R '^parser_fault_corpus$'`;
  `ctest --test-dir build/ifc-app -R '^ifc_worker_fault_corpus_full$'`.
- Sanitizers: `cmake --preset ifc-sanitize`, `cmake --build --preset ifc-sanitize`, then
  `ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/ifc-sanitize -R '^(ifc_worker_probe|ifc_import_worker|ifc_georeference|ifc_export_worker|ifc_worker_fault_corpus)$'`.
- Direct runs for the printed measurements: `build/ifc-app/ifc_worker_run_tests`, `build/ifc-app/ifc_import_tests`,
  `python3 -I tests/ifc_import_worker_tests.py WORKER tests/fixtures/ifc`, and
  `python3 -I tests/ifc_worker_fault_corpus.py WORKER tests/fixtures/ifc 512 2`.
- Budgets: `cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=ON -DSKETCHYUP_BUILD_IFC=ON -DSKETCHYUP_IFC_PREFIX=/ifc-cache/ifc-prefix -DCMAKE_LINKER_TYPE=BFD`,
  then `python3 -I tests/ifc_budgets.py WORKER CLI DIR` three times; the padded parse files, the export of the
  generated 100,000-face model (`sketchyup-cli --input ... --export-ifc ...`) and the 128 MiB + 1 byte refusal were
  measured with a small wrapper that reads `wait4` for the peak resident size.
- Fuzzing: `cmake -S third_party/ifcopenshell/cmake -B DIR -G Ninja` with `CC`/`CXX` Clang, `-fsanitize=address,undefined
  -fno-sanitize-recover=all -g -O1 -fno-omit-frame-pointer`, `BUILD_FUZZERS=ON`, IfcGeom and the OpenCASCADE kernel, then
  `ifcparse_fuzzer` from `ifcfuzz/`; the `worker` target is `scripts/ifc-fuzz/worker_fuzzer.cpp` compiled with
  `clang++ -fsanitize=fuzzer,address,undefined` against the built libraries; `parser_fuzz_ifc_package` from
  `cmake -DSKETCHYUP_BUILD_FUZZERS=ON` with Clang. The campaigns: `-max_total_time=300`, `ASAN_OPTIONS=detect_leaks=0`
  for the two library targets and `detect_leaks=1` for J1.
- Validator: `python3 -m venv` and `pip install ifcopenshell==0.9.0.post1 pytest` in a new directory inside the
  container; `python -I scripts/ifc-external-validators/validate_ifcos.py FILE 20` on each golden.
- Vendored tree: `python3 scripts/vendor-ifcopenshell.py fetch DIR`, then `check DIR/ifcopenshell-v0.9.0.tar.gz`.
