# R097.f — IFC exchange subset: acceptance record

2026-10-25. Branch `docs/r097f-ifc-acceptance`, based on `main` at `78058393` (R098.c merged as
PR #438). Product code is unchanged by this record; every check below runs at `78058393` unless the
row says otherwise. Checks are those of [ADR 0171 §6.3](../decisions/0171-ifc-exchange-subset.md#63-checks-for-the-verify-line).

[Summary](R097f-ifc-exchange.json).

**Status: in progress.** This skeleton holds the check map. The result column is filled in as each
check runs; a check is marked passed only when a run of today proves it.

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
| E5 | One undo step for the whole import | `ifc_import`, `ifc_import_dialog` (S9) | pass (desktop S9 not run here: native matrix, see below) | 78058393 |
| E6 | Provenance properties resolve to the right source entity | `ifc_import` | pass | 78058393 |
| E7 | Mesh volumes and areas agree with `NetVolume` and `GrossArea` within 1e-3 | `ifc_import_worker`, `ifc_import` | pass | 78058393 |
| F1 | θ = 0 and matching scale set the site origin to E, N, H within 1e-9 m | `ifc_georeference`, `ifc_import` | pass | 78058393 |
| F2 | 60° fixtures: `keepAxes` sets no origin; grid mode maps each vertex | `ifc_georeference`, `ifc_import` | pass | 78058393 |
| F3 | Model beyond ±1e6 m refused, naming the extent | `ifc_georeference` | pass | 78058393 |
| F4 | Survey foot, WCS, two conversions and the other F4 cases handled as specified | `ifc_georeference` | pass | 78058393 |
| G1 | Goldens pass the raw-text scan, `--check`, `ifcopenshell.validate`; hosted service by a person | `ifc_export`, `ifc_export_worker`; `ifcopenshell.validate` by hand (below); hosted service pending: owner | partial: pass for the scan, --check and the helper's own tests; ifcopenshell.validate pending its run; hosted service pending owner | 78058393 |
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
| J1 | Package decoder: bounds, re-encode; `parser_fuzz_ifc_package` | `ifc_package`, `parser_fault_corpus`; fuzz run (below) | partial: pass for ifc_package and parser_fault_corpus; the 5-minute libFuzzer run pending | 78058393 |
| J2 | 512 mutations of each certification file through the dry run, in a child | `ifc_worker_fault_corpus_full` (label `full`) | pass (64 and 512 mutations per file) | 78058393 |
| J3 | Upstream `ifcparse_fuzzer` and a `worker` target, ≥ 5 min each, sanitizers | manual campaigns (below) | pending (fuzz campaigns) | 78058393 |
| J4 | Harness runs every input twice and compares | `tests/fuzz/parser_harness.hpp` (`ifc-package`), exercised by the J1 run | pending (the twice-run harness is exercised by the seed generator and the J1 run) | 78058393 |
| K1 | Support matrix row, user guide section, exchange-boundaries entry state exactly the claims | manual, against the report's kinds (below) | pending (docs) | 78058393 |
| K2 | Fixture `NOTICE.md` carries CC BY 4.0 and the IFC trademark credit; fixtures unmodified | `ifc_import_worker` | pass | 78058393 |
| L1 | No dynamic dependency on IfcOpenShell or OCCT outside the worker | `ifc_library_install` | pass | 78058393 |
| L2 | `LD_LIBRARY_PATH` replacement makes the worker use the replaced libraries | `ifc_library_install` | pass | 78058393 |
| L3 | Package carries the notices; vendored tree equals the pinned commit | `ifc_library_source`, `scripts/vendor-ifcopenshell.py check` | pass (the tree-equals-archive check is run separately, see below) | 78058393 |
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

## G1: external validators

ADR 0171 §6.3 G1 asks for two validators on the golden exports, run by hand at acceptance; neither is a CI
gate. The raw-text scan, the helper's own schema check and the other checks of G1 are in the test table above.

### `ifcopenshell.validate` (run here)

The PyPI package `ifcopenshell==0.9.0.post1` (the same engine as the helper, so a consistency check, not an
independent reader) is installed in a throwaway venv inside the pinned image. It runs
`scripts/ifc-external-validators/validate_ifcos.py` with `python -I` on the six goldens in
`tests/fixtures/ifc/export/`. Results: pending (filled in below when the run finishes).

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

## Commands

Every command runs in the pinned image `sketchyup-ci-pinned:r097a-ifc`, with the IfcOpenShell prefix
cache mounted at `/ifc-cache` (its stamp matches the pin `7ba70197`), four CPUs and 10 GB.
