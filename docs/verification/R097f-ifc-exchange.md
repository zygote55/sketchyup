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
| A1 | Units: mm, m and ft fixtures to 1e-9 m | `ifc_import_worker`, `ifc_import` | pending | — |
| A2 | Quantity values equal the raw-text scan's (7 area, 7 volume, 11 length) | `ifc_import_worker` | pending | — |
| A3 | Missing length unit refused; accepted with the override, reported | `ifc_import_worker`, `ifc_import` | pending | — |
| A4 | Non-SI, non-inch, non-foot length unit refused (furlong) | `ifc_import_worker`, `ifc_import` | pending | — |
| A5 | Property or quantity with its own `Unit` converted or listed | `ifc_import_worker`, `ifc_import` | pending | — |
| B1 | Group tree equals the raw-text scan's by `GlobalId` | `ifc_import_worker`, `ifc_import` | pending | — |
| B2 | Counts per class equal the scan's | `ifc_import_worker` | pending | — |
| B3 | Element order within a parent follows the file | `ifc_import_worker` | pending | — |
| B4 | `ifc.Groups` equals the file's group, system and zone membership | `ifc_import_worker` | pending | — |
| B5 | Tags per class | `ifc_import`, `ifc_convert` | pending | — |
| B6 | Duplicate `GlobalId`, two parents, containment cycle noticed or reported | `ifc_import_worker` | pending | — |
| B7 | A layer with `LayerOn` false imports its bodies hidden | `ifc_import_worker`, `ifc_import` | pending | — |
| C1 | Every `Pset.Property` and `Qto.Quantity` equals the scan's | `ifc_import_worker`, `ifc_import` | pending | — |
| C2 | `\X2\` and `\S\` escapes decode | `ifc_import_worker` | pending | — |
| C3 | 128-per-body cap, 8 MiB aggregate cap, chunk-remainder cap; over-chunk geometry refused | `ifc_convert` | pending | — |
| C4 | `ifc.GlobalId`, `ifc.Class`, `ifc.Step`, `ifc.Representation` on every body | `ifc_import` | pending | — |
| C5 | Classification reference retained with its URI | `ifc_import_worker` | pending | — |
| C6 | Per-face colours equal the file's `IfcIndexedColourMap` | `ifc_import_worker`, `ifc_import` | pending | — |
| D1 | Accounting identity holds on every fixture | `ifc_import_worker`, `ifc_georeference`, `ifc_import` | pending | — |
| D2 | `unsupported-all.ifc` lists each class, ≤ 32 locations, complete counts | `ifc_import_worker` | pending | — |
| D3 | Unsupported placement, cycle or no geometry: not imported, located, nothing at the origin | `ifc_import_worker`, `ifc_import` | pending | — |
| D4 | Reference to an undefined ID is a notice, not a crash | `ifc_import_worker` | pending | — |
| E1 | Native validation (`decodeDocument(encodeDocument())`); save and reopen bit-exact | `ifc_convert`, `ifc_import` | pending | — |
| E2 | Tessellated and polygonal-profile areas and volumes to 1e-6 relative | `ifc_import_worker`, `ifc_import` | pending | — |
| E3 | Reference View wall volume with the opening subtracted; the uncut file is larger | `ifc_import_worker`, `ifc_import` | pending | — |
| E4 | Coplanar triangles merge into polygon faces; non-planar counted | `ifc_convert`, `ifc_import` | pending | — |
| E5 | One undo step for the whole import | `ifc_import`, `ifc_import_dialog` (S9) | pending | — |
| E6 | Provenance properties resolve to the right source entity | `ifc_import` | pending | — |
| E7 | Mesh volumes and areas agree with `NetVolume` and `GrossArea` within 1e-3 | `ifc_import_worker`, `ifc_import` | pending | — |
| F1 | θ = 0 and matching scale set the site origin to E, N, H within 1e-9 m | `ifc_georeference`, `ifc_import` | pending | — |
| F2 | 60° fixtures: `keepAxes` sets no origin; grid mode maps each vertex | `ifc_georeference`, `ifc_import` | pending | — |
| F3 | Model beyond ±1e6 m refused, naming the extent | `ifc_georeference` | pending | — |
| F4 | Survey foot, WCS, two conversions and the other F4 cases handled as specified | `ifc_georeference` | pending | — |
| G1 | Goldens pass the raw-text scan, `--check`, `ifcopenshell.validate`; hosted service by a person | `ifc_export`, `ifc_export_worker`; `ifcopenshell.validate` by hand (below); hosted service pending: owner | pending | — |
| G2 | Export then re-import reproduces bodies, vertices, hierarchy and properties | `ifc_export` | pending | — |
| G3 | Two exports with a fixed time are byte-identical | `ifc_export`, `ifc_export_worker` | pending | — |
| G4 | Every non-exported kind is counted; the identity holds | `ifc_export`, `ifc_export_worker` | pending | — |
| G5 | CRS label of authority-code form required for `IfcMapConversion` | `ifc_export` | pending | — |
| G6 | Golden exports checked in; CI compares bytes | `ifc_export` | pending | — |
| G7 | Duplicate and invalid `GlobalId` values get derived identifiers | `ifc_export`, `ifc_export_worker` | pending | — |
| H1 | Two imports of every fixture give identical encodings | `ifc_import`, `ifc_georeference` | pending | — |
| H2 | Shuffled instances give the same document except `ifc.Step` and file order | `ifc_import_worker` | pending | — |
| I1 | 128 MiB limit and over-limit entity count refuse with the limit named | `ifc_worker_run` | pending | — |
| I2 | Messages ≤ 256 bytes; report ≤ 64 KiB | `ifc_import`, `ifc_convert` | pending | — |
| I3 | Worker killed, over limit or past deadline: element left out, app keeps running | `ifc_worker_run` | pending | — |
| J1 | Package decoder: bounds, re-encode; `parser_fuzz_ifc_package` | `ifc_package`, `parser_fault_corpus`; fuzz run (below) | pending | — |
| J2 | 512 mutations of each certification file through the dry run, in a child | `ifc_worker_fault_corpus_full` (label `full`) | pending | — |
| J3 | Upstream `ifcparse_fuzzer` and a `worker` target, ≥ 5 min each, sanitizers | manual campaigns (below) | pending | — |
| J4 | Harness runs every input twice and compares | `tests/fuzz/parser_harness.hpp` (`ifc-package`), exercised by the J1 run | pending | — |
| K1 | Support matrix row, user guide section, exchange-boundaries entry state exactly the claims | manual, against the report's kinds (below) | pending | — |
| K2 | Fixture `NOTICE.md` carries CC BY 4.0 and the IFC trademark credit; fixtures unmodified | `ifc_import_worker` | pending | — |
| L1 | No dynamic dependency on IfcOpenShell or OCCT outside the worker | `ifc_library_install` | pending | — |
| L2 | `LD_LIBRARY_PATH` replacement makes the worker use the replaced libraries | `ifc_library_install` | pending | — |
| L3 | Package carries the notices; vendored tree equals the pinned commit | `ifc_library_source`, `scripts/vendor-ifcopenshell.py check` | pending | — |
| L4 | `THIRD_PARTY_NOTICES`, PKGBUILD licence array and `dependencies.json` updated | `ifc_library_source` | pending | — |
| L5 | No GPL-3.0-or-later library linked | `ifc_library_install` (`verify-ifc-library.py`) | pending | — |
| L6 | Kernel removal reports "kernel unavailable", no abort | `ifc_library_install` | pending | — |

## Commands

Every command runs in the pinned image `sketchyup-ci-pinned:r097a-ifc`, with the IfcOpenShell prefix
cache mounted at `/ifc-cache` (its stamp matches the pin `7ba70197`), four CPUs and 10 GB.
