# R098.d — STEP exchange subset: acceptance record

2026-10-25. Branch `feat/r098d-step-publish`, based on `main` at `c898a9e3`. The product code and the tests
ran at `33e175fa`; the commits after it change documentation only. Every check below ran at `33e175fa` unless
its row says otherwise, in the pinned image `sketchyup-ci-pinned:r097a-ifc` (GCC 16.2.1, OpenCASCADE 7.9.3,
Qt 6.11.2, Python 3.14), 22 CPUs and a 10 GB container. The checks are those of
[ADR 0172 §7](../decisions/0172-step-exchange-subset.md#7-verification-the-r098-verify-line) (E1 to E8),
§6 (the budgets) and §8 (packaging and CI). [Summary](R098d-step-exchange.json).

**Status (after R098.i, 2026-10-29).** E6 now passes in full: the restart-and-exclude that owner decision 7 deferred to 1.5 is built and tested (section 13). Nothing else in this record changes, and the verdict stays as R098.h left it.

**Status (after R098.h, 2026-10-28): Accepted (R098.h)**, with the documented limits of section 12.7: the owner's decisions 7, 8 and 13 are applied (section 12). E1 to E5, E7 and E8 pass; E6 passes for the refusals and the stops, and the restart-and-exclude is deferred to 1.5 (decision 7). The 1,000,000-triangle export budget is replaced by the largest exportable model a document holds, measured under the 4 GiB cap (23 s).

**Status (after R098.f, 2026-10-28).** The Verify line is still **partly met**. R098.f fixed the three STEP import
defects of the R098.e close (findings 11, 12 and 13; section 10): E4 now passes in its tests, and E1 passes by the
parent's decision on ADR 0172 section 7 (the reported deflection governs). E2, E3, E5, E7 and E8 pass as before, and
E6 is partial (no restart). The owner has not accepted this record. The restart-and-exclude and the budgets wait for
the owner. The R098.e status below is kept as the record of that close.

**Status (after R098.e, 2026-10-28).** The Verify line was **partly met**. Five of the eight checks pass in
full (E2, E3, E5, E7 and E8), two are partial (E1, which waits for the owner's choice between two clauses of ADR 0172
section 7, and E6, which lacks the restart), and one is **not met** (E4: the designed bad-face file does not report
its solid as an element and its faces join the good box's body; the PMI-only AP242 file is refused without counts).
R098.e changed E1, E2, E6 and E8 (section 9.1 gives each row's before and after). E8 passes on a stand-in
helper's message, not on a real one.

Budgets, measured on the Release build (section 4), are not all met, and no budget was changed. The helper's
tessellation of the 100-sphere model (4.0 s, budget 10 s) and the parse rows are met. Three rows are missed and
recorded as **missed: owner decision pending**: the end-to-end import of the 100-sphere model (14.0 s with a new
native file, 10.2 s without one, against 10 s), the export of the 90,954-face imported model (16.8 s against 5 s),
and the export of the 1,000,000-triangle model (61 s for the helper alone, 975 MB of output, and a crash under the
4 GiB address-space limit; the model cannot be built in one document at all, section 4).

## 1. Check map

Each row names the test or command that proves the check, the result of the run made today, and the commit it
ran at. "Not run" always comes with the reason.

| Check | Clause (ADR 0172) | Proof: CTest suite or command | Result today | Commit |
| --- | --- | --- | --- | --- |
| E1 | §7, dimensional: analytic box exact; cylinder and assembly within the bound | `ifc_step` (box volume and area to 1e-9 absolute; cylinder to 5 % relative at the default 1 mm; assembly two solids; **R098.e: the bound of §7, "within the reported deflection", at 0.02 mm** for the cylinder and the assembly); `ifc_step_worker` (the 5 % check, in Python) | **R098.f: pass.** The parent decided (R098.f) that the reported deflection governs, and the clause "1e-3 relative for analytic primitives" is dropped (ADR 0172 §7, amended in R098.f). The bound holds: at 0.02 mm the volume is 0.997368 of the analytic value (bound 0.992016) and the area 0.998948 (bound 0.995203), and the assembly likewise. The bound is derived from the deflection passed to the helper: at the default 1 mm it is 0.64 for the volume, looser than the 5 % check, which stays. (R098.e measured the dropped clause at 2.6e-3 at 0.02 mm, so it did not hold.) | df9cc436 (the test); the amendment in the R098.f record commit |
| E2 | §7, tessellation: planar faces merge; non-planar stays triangles and is counted; collapsed triangles dropped and counted; caps enforced | `step_import` C3 (the box comes out as six faces); `ifc_step` §6 (a total cap of 50 omits the cylinder and reports it; a per-element cap of 50 refuses); **R098.e: `ifc_import` E2 (app-level counts from the real helper)** | **R098.e: pass.** Box: 12 triangles, 6 merged faces, `nonPlanarFaces` 0, `degenerateTriangles` 0. Cylinder: 92 triangles, 26 merged faces (24 lateral strips and two caps), 0 and 0. Sphere of radius 5 mm (`sketchyup-step-sphere.step`): 304 triangles, `degenerateTriangles` 2 (one per pole, dropped and counted), 302 faces (304 less 2), `nonPlanarFaces` 3. The sphere's non-planar count depends on the tessellation, so it is pinned at this commit, not derived. The 100-sphere model (9 bodies in the document): 18 collapsed triangles, two per sphere | df9cc436 |
| E3 | §7, units: a declared unit converts and is reported; no unit refuses; a mismatched option does not rescale | `ifc_step` §4 (inch fixture comes out 254 x 508 x 762 mm, factor reported; a mismatching `--step-length-unit` does not win); §5 (no header and no unit refused, exit 2); `ifc_step_worker` (the same, Python) | pass | 33e175fa |
| E4 | §7, invalid or unsupported: reported with locations and counts; the rest converts; the accounting identity holds | `ifc_step` (accounting: 2 entities = 1 converted + 1 supporting + 0 omitted); `step_import` C2 (an AP230 file refused as `unsupportedSchema`; the unsupported formats refused with the helper's codes); **R098.e: `ifc_import` E4 on two designed fixtures** (`e4-bad-face.step`, `e4-pmi-ap242.step`, written by `tests/fixtures/step/make_designed.py`) | **R098.f: met in its tests (the owner has not accepted it).** Bad face: the six faces that no solid bounds are counted
(`elements.notImported`: `face`, `free-surface`, 6; `freeFaces` 6), located (six locations, each with the part's entity
label), and not imported, as ADR 0172 section 3 says. The good box is the body: one body of 12 triangles, and no face of
the bad box is in it. The accounting is 8 = 1 converted + 1 supporting + 6 omitted. PMI-only AP242: still refused with
`nothingToImport`, and the refusal names and counts the five PMI kinds, one entity each, with five locations.
(R098.e found the bad face counted but unlocated, not an element, and in the good body; the PMI file refused with no
counts. Section 10 gives the change.) Correction: the AP242 refusal this row cited is not in C2. C2 refuses AP230 (`unsupportedSchema`). The helper reads AP242 (best effort, ADR 0172 section 2) and refuses only the AP242 export. See sections 9 and 10 | df9cc436 (R098.e); df0f218e (R098.f code and tests) |
| E5 | §7, round trip: AP214 and AP203 export re-imports with geometry, names, colours, `step.Layer`, hidden state and the assembly/instance structure | `ifc_step_export` (`roundTrip` for `ap214` and `ap203`, through the real helper: volumes and bounds of each top-level body, names, colours, layer, hidden, one definition with three instances at three placements; E3 export half: the file declares `.MILLI.`) | pass | 33e175fa |
| E6 | §7, limits and refusal: a file over 128 MiB refuses with the count and the limit named; a worker over its limits or killed is restarted and the element excluded, or fails with its log tail; the document is unchanged | Refusal: `tests/step_budgets.py` (`refuse-129m`, a run today); **R098.e: `step_import` C8 (the 128 MiB + 1-byte refusal as a CTest)**. Stop and cancel: `step_import` C5 (deadline) and C6 (cancel). **R098.e: `step_import` C9 (SIGSEGV), C10 (RLIMIT_AS) and C11 (RLIMIT_CPU), each with a stand-in helper** | **R098.e: partial; R098.i: pass (section 13).** Pass: the 128 MiB + 1-byte file is refused as `tooLarge` before any helper starts (the stand-in hangs, so a helper that started would time out); exactly 128 MiB passes the application's check. A helper killed by SIGSEGV fails with `crash (tessellate)`; over RLIMIT_AS (the stand-in applies the limit and allocates past it) with `memory` and `bad_alloc` in the log tail; over RLIMIT_CPU (SIGXCPU) with `crash`. Each import throws before anything is created. Stop and cancel: pass with a stand-in helper. **The restart-and-exclude clause, not met at R098.e, is built and tested in R098.i** (section 13; owner decision 7): a stop in the tessellation is restarted with its base left out, at most three times, and gives a partial import that names it, or a failed import with its log tail. Section 9 has the estimate that R098.i built | df9cc436 |
| E7 | §7, native: the document validates (`decodeDocument(encodeDocument())`); save and reopen bit-exact; one undo removes the import; provenance resolves | `ifc_step_export` E7 checks, added in R098.d, run on each round-trip import through the real helper: encode, decode and re-encode bit-exact; one undo empties the document; every geometry body's `step.Source` equals the file's digest in the report | pass (both schemas) | 33e175fa |
| E8 | §7, report: messages ≤ 256 bytes; report ≤ 64 KiB; counts complete past the 32-per-kind location bound | Report size: `tests/step_budgets.py` (the 100-sphere import's report, 6,166 bytes). **R098.e: `step_import` C12, a refusal whose helper message is 5,000 bytes** | **R098.e: pass (on a stand-in).** The error's message is kept to 240 bytes (it measured 240; the bound is 256), and the report is within 64 KiB. The real helper's texts are clipped to the same 240 bytes (`step_import.cpp`, kernel messages). The counts past 32 locations are not asserted for STEP, because the STEP report keeps no locations (E4) | df9cc436 |
| Packaging L1 to L6 | §8: the STEP closure is in the private set, `RUNPATH $ORIGIN`, the notices installed | `ifc_library_install` (runs `verify-ifc-library.py install`; the new L3 check requires `STEP-SOURCE.txt` and its toolkit names and tag; the new L6 check requires the worker to record and install `libTKDESTEP` and `libTKXCAF`) | pass | 33e175fa |
| Packaging L3 to L4 (source) | §8: the notices and licence lines | `ifc_library_source` | pass | 33e175fa |
| Package acceptance | §8: the installed package carries `share/doc/sketchyup/STEP-SOURCE.txt`; the base package does not | `packaging/arch/verify-package.sh` (not run: no `makepkg` package build in this session). The install step it drives is covered by `ifc_library_install` | **Not run** (no package build). The base-package grep was extended and is unexecuted | — |
| Path map | §8: the STEP sources, fixtures and tests select the IFC job | `python3 scripts/ci-plan.py check`; `scripts/ci-plan.py areas` on each STEP file (below) | pass. `tests/fixtures/step/mirror-box.step`, `src/io/step_export.cpp`, `src/ifc/step_export.cpp`, `tests/ifc_step_tests.cpp`, `tests/step_export_dialog_tests.cpp`: `build job:ifc native`. `docs/STEP-SOURCE.txt` and `tests/step_budgets.py` added to the IFC rule: `job:ifc` | 33e175fa (path map in 7e46236b) |
| Corpus | §7 item 1 and 3: each file's origin, licence and SHA-256; self-generated and hand-written marked | `sha256sum` against `tests/fixtures/step/STEP-SOURCE.txt` (all 14 files match the record) | pass. No third-party file: §7 item 3 is not met, and owner decision 10 allows the corpus to stand without one | 9e9d66f7 |
| User guide, support matrix, exchange boundaries | §9 item 4: the loss-of-editability wording, the limits and the schemas | Read against the code and the measured numbers (below); the dialog and report wording is asserted by `step_import_dialog` | pass for the wording, which now appears in the import dialog and the import report; the claims in the user guide match §4 of this record | 33e175fa (documents in the commit after) |

## 2. Test runs

All run in the pinned image on 2026-10-25 at `33e175fa`. CTest ran with `-j2` and with the address space capped at
8,000,000 KiB (`ulimit -v`); the helper keeps its own 4 GiB limit.

| Build (CMake preset) | Command | Result |
| --- | --- | --- |
| `ifc` (helper) | `cmake --build --preset ifc` after reconfiguring for the installed notice | exit 0 |
| `ifc-app` (helper, CLI, application IFC and STEP tests) | `cmake --build --preset ifc-app --parallel 4`, then `--target parser_fault_corpus_tests` | exit 0 both times |
| `native` (desktop, the four dialog suites) | `cmake -S /src -B build/native` with `-DSKETCHYUP_BUILD_DESKTOP=ON -DSKETCHYUP_BUILD_IFC=ON`, then the four test targets | exit 0, 397 steps |
| `release` (the timed binaries: `sketchyup-cli`, `sketchyup-ifc-worker`, `ifc_step_export_tests`) | `cmake -S /src -B build/release -DCMAKE_BUILD_TYPE=Release -DSKETCHYUP_BUILD_DESKTOP=OFF -DSKETCHYUP_BUILD_CLI=ON -DSKETCHYUP_BUILD_IFC=ON` | exit 0, 249 steps. **All timings in §4 come from this build.** The `ifc-app` preset is a Debug build (`CMAKE_BUILD_TYPE=Debug`), and a first set of timings from it was discarded: it was about 1.6 times slower |

**Parse, refusal and export budget files** were run in the Release build, one run and then repeats (§4); the raw
JSON and logs are kept in the build cache and are not committed.

**Helper suites, fast tier** (`ctest --test-dir build/ifc -R '^ifc_' -LE full -j2`): 8 of 8 passed, 134.31 s.
`ifc_worker_probe` 9.53 s, `ifc_import_worker` 48.08 s, `ifc_georeference` 27.50 s, `ifc_export_worker` 26.04 s,
`ifc_step_worker` 2.85 s, `ifc_worker_fault_corpus` 83.38 s, `ifc_library_install` 36.24 s, `ifc_library_source` 0.31 s.

**Application suites** (`ctest --test-dir build/ifc-app -R '^(ifc_package|ifc_convert|ifc_import|ifc_worker_run|ifc_export|ifc_step|ifc_step_export|step_import|parser_fault_corpus)$' -j2`):
8 of 9 passed on the first run. `parser_fault_corpus` was "Not Run" because the `ifc-app` preset does not build
it; it was built with `--target parser_fault_corpus_tests` and passed (2.14 s). `ifc_convert` 16.77 s,
`ifc_import` 38.34 s, `ifc_worker_run` 14.39 s, `ifc_export` 20.99 s, `ifc_step` 2.98 s, `ifc_step_export` 0.72 s,
`step_import` 1.39 s.

**After the mirror fixture was replaced:** `ifc_step`, `ifc_step_export` and `step_import` 3 of 3 passed; `ifc_step_worker` passed.

**Desktop dialog suites** (`ctest --test-dir build/native -R '^(step_import_dialog|step_export_dialog|ifc_import_dialog|ifc_export_dialog)$' -j2`, offscreen): 4 of 4 passed. `step_import_dialog` 2.77 s (with the
new assertions for the loss wording in the dialog and the report), `step_export_dialog` 1.07 s, `ifc_import_dialog` 7.20 s,
`ifc_export_dialog` 1.77 s.

**Path map:** `python3 scripts/ci-plan.py check` — "Path map covers 3562 tracked files with 27 rules; 19 examples hold." Run on the working tree before the commit, with the new files untracked; the check covers tracked files, so it is run again at the PR.

## 3. E-checks not yet run, and why

- **E4 fixtures.** R098.e added two designed fixtures (a bad face and a PMI-only AP242 file) and ran them. The
  second is refused; the first shows the defects of section 9. No entity the translator refuses is in the corpus
  as its own fixture: a surface model beside a solid gives the same free-face notice as the bad face (checked by
  hand, not committed).
- **E6 restart.** Not run: there is no restart protocol for STEP to test (§7, finding 1).
- **E6 SIGSEGV and RLIMIT for STEP.** Run in R098.e with stand-in helpers (`step_import` C9 to C11), the pattern
  `step_import` already uses. They test the application's handling of each stop, not the helper's own limits.
- **E8 messages for STEP.** Run in R098.e with a stand-in helper's 5,000-byte message (`step_import` C12).
- **Package acceptance (`verify-package.sh`).** Not run: no `makepkg` package build in this session.
- **Cancel on the real helper.** Not measured: the command line has no cancel. The stop path is asserted with a stand-in helper.
- **Sanitizers.** Not run for R098.d: the STEP path is unchanged in its sanitizer behaviour, and the R098.b record ran the sanitize set.
- **Desktop by hand.** Not run: no interactive session.

## 4. Budgets (ADR 0172 §6)

Measured today on the Release build (§2), in the pinned image. Each row names its runs. The machine is
shared with another worker, so the timings carry noise: the spread of the repeats is given. Memory is the
`wait4` maximum RSS of the command and its children (`tests/step_budgets.py`). The generated files are the
budget files of `tests/step_budgets.py` (the parse files are the corpus box plus unreferenced points) and the
OCCT sphere model of `tests/step_budget_model.cpp` (100 spheres of radius 1 m, 2.5 m apart).

| §6 row | Budget | Measured today (Release) | Verdict |
| --- | --- | --- | --- |
| Input file | 128 MiB, checked before the helper starts (regular file, ISO header, FILE_SCHEMA) | 134,217,729 bytes refused in 0.01 s: `LIMIT_EXCEEDED`, `tooLarge`, "larger than 128 MiB (134217729 bytes)", no helper started. 134,217,728 bytes imports (status ok, row 7's file) | **met** (size). Header and schema: `step_import` C1 and C2 |
| Helper resource limits | 4 GiB address space; 120 s deadline; CPU 480 s; output 256 MiB; four geometry threads; 3 restarts; 2,000,000 entities | In the code: address space 4 GiB (`step_worker.hpp`), deadline 120 s, `--rlimit-cpu` 4 x deadline = 480 s, `--rlimit-fsize` 256 MiB (`step_worker.cpp`). **Not as §6 says:** the engine reports `threads: 1` (BRepMesh is single-threaded in this build); there are no restarts; the STEP mode has no entity cap (`--max-entities` is read only by the IFC import) | **partly met**: the four values are set; the thread count, restarts and entity cap are not. **missed: owner decision pending** |
| Triangles | 1,000,000 in total; 100,000 per element | The helper on the 100-sphere model: 990,584 triangles converted to 99 bodies; the 2 solids that would pass 1,000,000 are omitted with the `triangleBudgetApplied` notice. Per element: `ifc_step` §6 refuses at a cap of 50 | **met** at the helper. The application's document holds 100,000 unique faces (`documentLimit:faces`): 89 of 100 bodies were left out, so a curved model stops at about 100,000 triangles (§7, finding 3) |
| Package | manifest ≤ 32 MiB; mesh chunk ≤ 128 MiB | The decoder check in `ifc_step` (`decodePackageCheck`) and the shared decoder (`ifc_package`) pass. The 100-sphere package: 27,906,882 bytes | **met** (asserted by the decoder tests) |
| Report and messages | report ≤ 64 KiB; message ≤ 256 bytes | The 100-sphere import's report: 6,166 bytes. Messages not asserted for STEP | report **met**; messages **not run** |
| Application memory during import | ≤ 1 GiB | Command-line import of the 100-sphere model: peak 722 to 723 MiB over three runs (the helper's peak is inside that figure). The helper alone on the 128 MiB file: 964 to 965 MiB. The 128 MiB input through the command line was not run | **met** for the measured inputs |
| Parse and transfer 64 MiB, 1,000,000 instances | ≤ 3 s | Helper `--import-step`, 67,108,864 bytes, 1,000,350 entities, status ok; four runs: 2.72, 2.59, 2.69, 2.74 s; peak 487 to 529 MiB. (The R098.a2 figure of 13.89 s was for a different file, 79.8 MiB and 1,690,012 entities, and its build type is not recorded) | **met**: 2.59 to 2.74 s; the margin is about 0.3 s |
| Parse and transfer 128 MiB, 2,000,000 instances | ≤ 6 s | Helper `--import-step`, 134,217,728 bytes, 1,999,350 entities (under the row's 2,000,000 by 650), status ok; four runs: 5.39, 5.66, 5.51, 5.50 s; peak 964 to 965 MiB | **met**: 5.39 to 5.66 s |
| Tessellate the model (≤ 1,000,000 triangles) | ≤ 10 s, with progress; cancel returns in ≤ 100 ms and the helper ends within 1 s | The helper alone on the 100-sphere model (990,584 triangles): 4.04, 3.98, 4.08, 4.99 s (R098.d); **R098.e, Release, 4 runs: 3.99, 4.00, 4.00, 4.03 s**; progress: 101 `P at` lines. The command-line import of the same file, end to end, with a new output file: 13.84, 15.76, 14.27, 13.99 s (R098.d); **R098.e, Release, 4 runs: 14.02, 14.09, 14.06, 14.11 s**. The same import without an output file (R098.e, 3 runs): 10.15, 10.18, 10.25 s. Cancel on the real helper: **not measured** (the command line has no cancel) | the helper's tessellation **met**; the end-to-end import **missed: owner decision pending** (14.1 s with a native file, 10.2 s without); cancel **not run**. Where the time goes, in section 9 |
| Export of the 1,000,000-triangle model | ≤ 5 s, with progress and cancel; output ≤ 256 MiB | **R098.e, Release, the helper alone on a package of the budget model (20 bodies of 500 102-gons, 1,000,000 triangles, 1,020,000 vertices), with no address-space limit: 61.1 s, peak 8,446 MiB, output 975,418,866 bytes (exit 0).** With the application's 4 GiB address-space limit: **SIGSEGV at 21.6 s** (the address space is exhausted; the OCCT writer reports `malloc failed` and then faults in `Interface_Graph::Evaluate`, gdb backtrace in section 9). With the output limit only (257 MiB): SIGXFSZ at 54 s, 269 MB written. Through the application: **the model cannot be built in one document**: `ifc_step_export --budget 20 500` runs for 204 s and then refuses the edit ("vertices would be 1,020,000 of 600,000"). R098.c's figures (73.9 s helper alone, 974.9 MB) are the same order and are consistent with these. The 90,954-face imported model: **R098.e, Release, 4 runs of the command line: 16.80, 16.83, 16.82, 16.83 s**; helper 15.6 s (the writer 13.1 s), application 1.1 s; 223,950,330 bytes | the 1,000,000-triangle row **missed** (61 s against 5 s; 975 MB against 256 MiB; crash at the 4 GiB limit): owner decision pending. The 90,954-face export **missed on time** (16.8 s against 5 s), met on size: owner decision pending |

The helper figures are four runs each (the text gives the spread). The two export budget figures (20,000 and
50,000 triangles) are single runs. Every timed figure in the record is from the Release build.

## 5. Publication

- **User guide.** "STEP import and export" is a new section (after "Exchange and present a model"). It states
  the menus and the command line, the schemas (AP203 and AP214 read and write; AP242 read best effort and never
  written), the unit policy, what comes across both ways and what does not, the limits (128 MiB in; 1,000,000
  triangles and 100,000 per element on import; a 100,000-face document; the export's 256 MiB), and the loss of
  editability in the words of ADR 0172 §3.
- **Import dialog and report.** Both say the loss in the same words. The dialog text is asserted by `step_import_dialog`.
- **Support matrix.** One row in "Exchange boundaries": "STEP (F04, not in the 1.3.0 declaration)". It names the
  M10 gate.
- **Roadmap.** The R098 row now records R098.d and states that F04 needs the M10 gate.

## 6. Installed notice and packaging

`docs/STEP-SOURCE.txt` is installed as `share/doc/sketchyup/STEP-SOURCE.txt` by the `ifc` component, beside
`IFC-SOURCE.txt`. It names the OCCT toolkits the STEP mode uses, their source (tag V7_9_3 and the Arch package),
the licence, and where the SketchyUp STEP code is in the source archive. `verify-ifc-library.py` checks it
(L3) and checks the STEP closure (L6). The check ran in `ifc_library_install` and passed.

## 7. Findings for the owner

1. **E6 restart is not implemented for STEP.** ADR §5 and E6 say a worker that is over its limits, or killed,
   is restarted with the element it was on excluded, at most three times, and then gives a partial import that
   names the excluded elements, or a failed import with its log tail. The STEP import gives only the failed
   import with its log tail (`src/io/step_worker.cpp`, the comment above the `workerFailed` throw, and the header
   of `step_worker.hpp`). The ADR and the code disagree; the owner decides whether to build the restart or to
   amend ADR §5 and E6.
2. **The STEP mode has no entity cap, and runs on one thread.** ADR §6 says the STEP mode takes "the same limits"
   as the IFC helper, which include 3 restarts and 2,000,000 entities. The STEP mode has no entity cap, no
   restart, and reports `threads: 1` (BRepMesh is single-threaded in this build). Decision 5's four threads
   do not apply to STEP.
3. **A document holds 100,000 unique faces, so a curved import stops at about 100,000 triangles.** Each triangle
   of a curved face is one unique face. The import of 100 spheres (990,584 triangles in the helper) converted 9
   bodies and reported 89 as `documentLimit:faces` (the limit is `DocumentLimits::unique.faces` = 100,000). ADR 0172
   assumed the 1,000,000-triangle total would hold; in a document it holds about one tenth of that. The owner
   decides whether the curved import should merge or sample, or whether the 1,000,000-triangle figure is a
   helper figure only.
4. **The export size note came too late for triangle faces** (fixed in the review: the note now appears above
   100,000 triangles). It warned above 250,000 triangles.
   The measured output of a triangle-faced model is about 2.4 KB per face (223,950,326 bytes for 90,954 faces),
   so the 256 MiB limit is reached at about 110,000 faces; the warning does not appear before the refusal. The
   user guide and the support matrix now state the measured figures. The threshold constant
   (`StepExportDialog::kTriangleWarningLimit`) is not changed here; the owner decides it. **Owner decision pending.**
5. **E1's cylinder bound.** The test asserts 5 % relative; §7 says "within the reported deflection". The deflection
   bound would be tighter. Not changed; the owner decides whether §7 or the test is the target.
6. **The corpus has no E4 fixtures, and no third-party file.** See §3. NIST's CAD interoperability test cases state
   unrestricted use with acknowledgement requested (the NIST download page); none was downloaded.
7. **The mirror fixture was malformed, and is replaced.** The R098.a2 `mirror-box.step` lost its commas and
   same-sense attributes, and integer literals stood where reals were required. A reader tolerates it, so the
   tests passed. The regenerated file is OCCT's output for a true mirror. This changes a checked-in input; the
   old one is in the history (`cb2f4bf3`).
8. **The corpus lives in `tests/fixtures/step/`**, not `tests/resources/step/` as ADR §7 says. Recorded here; the
   ADR notes name the actual path.

9. **Two budget misses are timing, not size.** The end-to-end import of the 100-sphere model takes 13.8 to 15.8 s
   (budget 10 s), and the export of the 90,954-face imported model takes 16.6 to 16.8 s (budget 5 s). The helper's
   tessellation of the same model takes about 4 s, so about 10 s of the import is outside the helper: the application's
   conversion, the document and the save. That split was not measured further. The export's own phase timings were not
   recorded for this model. Both figures are from the Release build. The 1,000,000-triangle export is R098.c's figure
   and is not re-run. **Owner decision pending** for each.
10. **The R098.a2 parse miss does not hold for the Release build.** The 64 MiB parse row, which R098.a2 recorded at
   13.89 s for a different, larger file, is 2.59 to 2.74 s today on the Release build. The R098.a2 build type is not
   recorded in its notes, so the two figures cannot be compared directly. The 128 MiB row is 5.39 to 5.66 s.
   Both rows are met; the margins are about 0.3 s each.

11. **A bad solid's faces are reported as not imported and imported.** The helper's package says `face`, `free-surface`,
    6 (`src/ifc/step_import.cpp`, `report.notImported`), and the application's notice says `freeFaces` 6 "imported as a
    body of its own faces". The faces are tessellated with the base shape, so they come back in the good box's body:
    one body, 12 faces, 24 triangles, and `elements.notImported` is empty. Steps: import `tests/fixtures/step/e4-bad-face.step`
    (the CLI's `--import-step`), read `importReport`. **Fixed in R098.f** (df0f218e; section 10): the surfaces are not
    tessellated, they are counted and located, and `elements.notImported` names them.
12. **The STEP report has no locations.** ADR 0172 §5 says a location is the entity label, the parent path, the transfer
    status and a message, at most 32 per kind. `src/ifc/step_import.cpp` never calls `Report::locate`; the report keeps
    counts by class and reason only. **Fixed in R098.f** (df0f218e; section 10): every outcome that leaves a part out is located.
13. **A PMI-only AP242 file is refused.** `nothingToImport` ("The reader could not transfer the file into the document"):
    the file has no shape representation, and the refusal names no PMI entity and counts none (E4 asks for both).
    **Fixed in R098.f** (df0f218e; section 10): the refusal stays, and it names and counts the five kinds, one each.
14. **The 1e-3 clause of §7 E1 does not hold for a tessellated cylinder.** The volume error is 2.6e-3 at 0.02 mm; the
    clause needs a deflection of about 0.0025 mm. The §7 clause "within the reported deflection" holds at 0.02 mm.
    **Parent decision (R098.f):** the "1e-3" clause is dropped and the reported deflection governs; ADR 0172 §7 is amended
    in place, and E1 passes (section 10).
15. **The 1,000,000-triangle export does not run through the application.** The budget model needs 1,020,000 vertices and
    a document holds 600,000 (the placement limit). The helper alone needs 8.4 GiB of address space at its peak, so the
    4 GiB limit of ADR 0172 §6 ends it with a SIGSEGV (an OCCT `malloc failed` in the writer, then a fault in
    `Interface_Graph::Evaluate`), and the output of 975 MB is four times the 256 MiB limit. The ADR's 4 GiB limit and its
    1,000,000-triangle export budget do not fit together; the owner decides which one moves.
16. **Where the 100-sphere import's 14 s goes.** Helper 4.0 s; the application's conversion after the helper 6.2 s (the
    planar merge `buildMeshGeometry` is about 40 % of the sampled application time, the conversion 60 %); the native save
    about 4 s (the save is about 37 % of the sampled application time). Sampling: 35 gdb stacks, one every ~0.4 s, on the
    application's side of the run (section 9).
17. **The export of 90,954 faces is the writer.** Of the helper's 15.6 s, the OCCT writer takes 13.1 s, the read-back
    check 1.0 s, the build 1.5 s, the read 0.02 s (a standalone run on the staged package, section 9).

## 8. Commits

- `7e46236b` — packaging closure check, `STEP-SOURCE.txt` install, path map.
- `9e9d66f7` — the corpus record and the regenerated mirror fixture.
- `33e175fa` — the loss wording in the dialog and report, the E7 checks, the budget tools. **The commit the tests ran at.**
- The commit after `33e175fa` adds this record, the user guide, the support matrix, the roadmap row and the ADR notes.
- `df9cc436` — R098.e: the E1 bound, the E2 counts, the E4 fixtures, the E6 CTests and stand-ins, the E8 bound, the
  sandbox count of 17. **The commit the R098.e tests ran at** (the tree of `eb85cffa` differs only in a comment).
- `eb85cffa` — R098.e: a comment (the E1 area bound at 0.02 mm is 0.995, not 0.994).
- The commit that adds section 9 and this update to the record and the ADR notes (R098.e).
- `df0f218e` — R098.f: the STEP surfaces kept out of bodies, the locations, the PMI kinds, the E4 tests. **The commit the
  R098.f tests ran at.** The commit that adds section 10 and the R098.f ADR notes and E1 amendment follows it (see the PR).
## 9. R098.e close (2026-10-28)

The F04 close brief (items 1 to 9). The product code is unchanged: `git diff 86070665 -- src/` is empty. The
tests are in `df9cc436`, with a comment correction in `eb85cffa`, and this section is in the commit after them.

### 9.1 Each row: before, after, test, commit

| Row | Before (R098.d) | After (R098.e) | Test | Commit |
| --- | --- | --- | --- | --- |
| E1 | cylinder to 5 % only | At 0.02 mm: the cylinder's volume is 0.997368 of the analytic value (bound 0.992016) and its area 0.998948 (bound 0.995203); the assembly is inside its bounds too. The default 1 mm keeps its 5 % check, because its bound (0.64 for the volume) is looser. The §7 clause "1e-3 relative for analytic primitives" is not met at any tested deflection (2.6e-3 at 0.02 mm) | `ifc_step` §6 | df9cc436 |
| E2 | counts reported, not asserted | Box: 12 triangles, 6 faces, 0 non-planar, 0 collapsed. Cylinder: 92 triangles, 26 faces, 0 and 0. Sphere of radius 5 mm: 304 triangles, 2 collapsed (one per pole, dropped), 302 faces, 3 non-planar (pinned) | `ifc_import` E2 | df9cc436 |
| E4 | no designed fixtures | Two fixtures added. Bad face: six faces counted (`freeFaces` 6), the good box converts, but no location, no element and the faces join the good box's body (section 7, item 11). PMI-only AP242: refused as `nothingToImport` with no counts | `ifc_import` E4 | df9cc436 |
| E6 | refusal and stop with a stand-in | The 128 MiB + 1-byte refusal is a CTest. SIGSEGV, RLIMIT_AS and RLIMIT_CPU each fail with their log tail and create nothing. Restart-and-exclude: not implemented (9.2) | `step_import` C8 to C11 | df9cc436 |
| E8 | report size only | A 5,000-byte helper message is kept to 240 bytes; the report is within 64 KiB | `step_import` C12 | df9cc436 |

The E4 `nothingToImport` refusal and the bad-face result are pinned as they are. They are not passing E4 checks.

**Correction to the R098.d record.** The E4 row there cited "`step_import` C2 (AP242-only ... refused)". C2 refuses
an AP230 file (`unsupportedSchema`). The helper reads AP242 (best effort, ADR 0172 section 2) and refuses only the
AP242 export (`ifc_step_export` tests the export's refusal). No AP242 import is refused by C2 or anywhere else.

### 9.2 Restart-and-exclude: what IFC does, and what STEP would take (brief item 5)

**What the IFC import does** (ADR 0171 section 1.6 and decision 19; ADR 0171 notes "Restart attribution", lines
1477 to 1479):

- `src/io/ifc_worker.cpp` `runOnce` (lines 133 to 150) passes `--threads`, `--exclude-step N` for each excluded product,
  and `--diagnose-from TASK`. It reads the progress lines `P at TASK` (the task it is on), `P next` (the elements to
  leave out) and `P excluded` (the products the helper left out itself) (lines 190 to 260).
- The restart loop (lines 395 to 500): a stop on several threads reruns on one thread; a stop on one thread at a known
  task (`P at TASK`) reruns with `--diagnose-from TASK`, where each task is tried in a child process (ADR 0183); a stop
  with elements named by `P next` leaves those out and reruns. At most `maxRestarts`, 3 (`src/io/ifc_worker.hpp` line
  21). Then `workerFailed`, with `engine.stops`, `excluded` and `logTail`. A partial import carries the `excluded` list
  and the notice `restarted`.
- The helper side is `--exclude-step` (`src/ifc/worker_main.cpp` lines 146 and 211), `iteratePass` and `diagnoseTasks`
  (`src/ifc/geometry.cpp`).

**What can be shared.**

- The process supervisor. `runIfcWorker` (`src/io/ifc_worker_run.cpp`, 113 lines) is already shared by the IFC export
  and the STEP export. The STEP import's own `runOnce` (`src/io/step_worker.cpp`, about 135 lines) is a near copy that
  also parses the progress lines (`P phase`, `P tasks`, `P at`), guards against flooding and classifies SIGSYS. Folding
  it into `runIfcWorker` needs a progress hook of about 15 lines.
- The restart bookkeeping: the excluded list, the `maxRestarts` cap, the `workerFailed` report and the `restarted`
  notice. These carry over unchanged.
- Not shareable: the one-thread and `--diagnose-from` steps. They rest on the IFC geometry's tasks, child processes
  and its four threads. STEP has none of them: BRepMesh is single-threaded in this build, and the helper reports the
  base it is tessellating as `P at` already (`src/ifc/step_import.cpp`, the `baseLabels` loop).
- A stop in the read or build phase cannot be attributed to an element, in either importer. Those stay failures.

**What STEP would take (rough size).**

- Helper (`src/ifc/worker_main.cpp`: an `--exclude-step` option, about 10 lines; `src/ifc/step_import.cpp`: skip the
  excluded bases, record them, and report `P excluded`, about 30 lines): about 40 lines.
- Application (`src/io/step_worker.cpp`: the restart loop and its report, about 100 lines; `src/io/step_worker.hpp`: the
  `maxRestarts` option, 1 line; the progress hook in `runIfcWorker` if the STEP runner is folded in, about 15 lines):
  about 115 lines added and about 130 removed if the STEP runner goes.
- Tests (a stand-in that stops at base k and a helper test with `--exclude-step`): about 120 lines.
- Net: roughly 150 to 260 lines. The protocol changes are one new option and one progress line on the helper, and the
  restart rule in the application. Owner decision: whether 1.4.0 needs the restart (ADR 0172 section 5 and E6).

### 9.3 Budget measurements, and where the time goes (brief item 7)

Release tree (`build/release`: `CMAKE_BUILD_TYPE=Release`, desktop off, CLI on, IFC on, linker BFD; targets
`sketchyup-cli`, `sketchyup-ifc-worker` and `ifc_step_export_tests`) in `sketchyup-ci-pinned:r097a-ifc`, 2026-10-28,
on the shared machine. The 100-sphere model is `step_budget_model OUT 100 1000` (100 spheres of radius 1 m, 2.5 m
apart, 3,520 entities); it is not committed, and the tool builds it.

| Measurement | Runs | Result | Budget | Verdict |
| --- | --- | --- | --- | --- |
| Helper alone, 100 spheres (parse, tessellate, package) | 4 | 3.99 to 4.03 s; peak 184 to 189 MiB | 10 s | met |
| Command line, end-to-end import, a new native file | 4 | 14.02 to 14.11 s; peak 722 to 723 MiB | 10 s | missed |
| Command line, the same import, no native file | 3 | 10.15 to 10.25 s: before the helper 0.01 s, the helper 3.94 to 4.00 s, after it 6.18 to 6.30 s | 10 s | missed (by 0.2 s) |
| Command line, export of the imported model (9 bodies, 90,954 faces) | 4 | 16.80 to 16.83 s; peak 1,925 to 1,927 MiB; 223,950,330 bytes. Before the helper 0.71 s, the helper 15.6 to 15.7 s, after it 0.4 s | 5 s | missed |
| Helper alone, the same export, phase times (one staged package) | 1 | read 0.02 s, build 1.46 s, write 13.06 s, check 1.00 s; total 15.55 s | (part of the 5 s) | |
| Helper alone, 1,000,000 triangles (20 bodies of 500 102-gons; 1,020,000 vertices) | 1 each | No limits: 61.1 s, peak 8,446 MiB, 975,418,866 bytes, exit 0. The application's 4 GiB address-space limit: SIGSEGV at 21.6 s (the address space reached its cap at 4,022 MiB). The output limit only (257 MiB): SIGXFSZ at 54.0 s, 269 MB written | 5 s; 256 MiB | missed |
| Application, 1,000,000-triangle export (`ifc_step_export --budget 20 500`) | 1 | Not measurable: the edit is refused after 204 s ("vertices would be 1,020,000 of 600,000"); a document holds 600,000 vertices | | not run |

**Where the 100-sphere import's time goes** (14.1 s with a new native file). The helper is 4.0 s (28 %). After the
helper, the application's conversion is 6.2 s (44 %, decode, merge and document build), and the native save is about
4 s (28 %, the difference between the runs with and without an output file). A poor man's profile of the application's
side (gdb `bt`, 35 samples, about 0.4 s apart, the process stopped in each) puts the conversion (`Converter::run`) in
21 of 35 samples, and of those the planar merge (`buildMeshGeometry`, `src/io/ifc_import.cpp`) in 14. The save
(`createNativeFile`, `encodeDocument`) is in 13 of 35. These are sample counts, not a profile, and the sampler ran
with ptrace enabled for that run only (`--cap-add SYS_PTRACE`).

**Where the 90,954-face export's time goes** (16.8 s). The helper is 15.6 s, of which the OCCT writer
(`STEPCAFControl_Writer::Transfer` and `Write`) is 13.1 s (84 % of the helper), the read-back and text check of the
224 MB output is 1.0 s, and the build from the package is 1.5 s. The application's own package build and staging is
1.1 s. The writer is the cost; the application is not.

**The 1,000,000-triangle export.** The helper's phases: read 0.03 s, build 0.25 s, then the writer from 5.7 s. Under
the 4 GiB limit, the writer's first `malloc` failure is reported by OCCT (`TransferWriteShape, EXCEPTION:
Standard_MMgrRaw::Allocate(): malloc failed`), and then the process faults. The gdb backtrace: SIGSEGV in
`Interface_Graph::Evaluate()`, from `Interface_Graph::Interface_Graph`, `IFSelect_WorkSession::ComputeGraph`,
`STEPCAFControl_Writer::transfer` and `sketchyup::ifc::runExportStep`. The helper cannot catch a fault inside OCCT's
writer after a failed allocation, so the application sees a crash. The peak memory is 8.4 GiB for 1,000,000 triangles,
which the 4 GiB limit of ADR 0172 section 6 cannot hold.

The helper-only runs used the same export package format the application writes (`SKSTPX01`, `src/ifc/step_export_package.cpp`),
written by a generator of the budget model's geometry (not the application's own export path, which cannot build the model).

### 9.4 Contradictions and corrections

- **The brief's premise about C2** (section 9.1): C2 refuses an AP230 file; no AP242 import is refused.
- **#495.** The brief describes it as a four-thread race and a use-after-free. The commit (`461e2de8`) and ADR 0171's notes
  describe a data race: unsynchronised reads of the library's log while worker threads append. The diff has no
  lifetime change beyond reading the log after the iterator is destroyed, which joins the threads. This record makes
  no use-after-free claim.
- **ADR 0172 section 3** says surfaces without volume are "counted and reported as not imported". The bad solid's faces
  are reported as not imported by the helper and as imported by the application's notice, and they are in the output.
- **ADR 0172 section 5** says a location is the entity label, the parent path, the status and a message. The STEP
  report has none.
- **ADR 0172 section 6** gives the 4 GiB address-space limit. The 1,000,000-triangle export needs about 8.4 GiB, so the
  limit and that budget cannot both hold.
- **ADR 0172 section 7, E1** says "1e-3 relative for analytic primitives" and also "within the reported deflection".
  Only the second holds for a tessellated cylinder.
- **ADR 0172 sections 6, 8 and 9**: the STEP helper reuses the IFC helper's restarts; it does not (R098.d section 7).
- **The R098.d record's E4 row** cites a C2 refusal of an AP242-only file (9.1, corrected).
- **The worker rules** ask for the roadmap row's status to be updated; the brief says not to change `docs/PR_ROADMAP.md`,
  `docs/SUPPORT_MATRIX.md`, `docs/RELEASE_NOTES.md` or `CHANGELOG.md`. The brief governs, and none of them is changed.

### 9.5 What was not done

- **E4 bad face (stopped).** The defect of section 7, item 11 is a product defect, and fixing it is out of scope. It is
  reported here with its steps, and E4 stays **not met**. I did not change the product code.
- **E4 locations and PMI counts.** Both are product changes (section 7, items 12 and 13). Not done.
- **E6 restart-and-exclude.** Not implemented, as the brief asks. The estimate is in 9.2.
- **E1's "1e-3" clause.** The owner decides which clause governs (section 7, item 14).
- **Budgets.** Measured, not fixed. The 1,000,000-triangle export is measured helper-only, not through the application,
  which cannot build the model.
- **E8 on a real helper message.** Not produced; the stand-in gives the long message.
- **Not run:** sanitizers; package acceptance (`verify-package.sh`); cancel latency on the real helper; the desktop
  dialog suites (the native matrix, which needs the `full-ci` label); the full tier (`full-ci`).
- **Mutation check.** Three deliberate failures (E1's volume bound at 0.99, E2's non-planar count at 4, E8's bound at
  200 bytes) each failed their test; the sources were restored and the suites passed again. The mutants are not committed.

### 9.6 Test runs (R098.e)

- The IFC job's application selection, in `sketchyup-ci-pinned:r097a-ifc`, the `ifc-app` tree (Debug), CTest with the
  address space capped at 8,000,000 KiB: `ifc_package`, `ifc_convert`, `ifc_import`, `ifc_worker_run`, `ifc_export`,
  `ifc_step`, `ifc_step_export`, `step_import`, `import_refusal` and `ifc_sandbox`: **10 of 10 pass** at the tree of
  `df9cc436` (the suites ran on the final sources before that commit). `eb85cffa` changes one comment in
  `ifc_step_tests.cpp` and was not re-run.
- `ifc_sandbox` counts the 17 STEP fixtures (it counted 14 before; the test was updated with the fixtures).
- Not run: the helper's Python suites (`ifc_step_worker` and the rest of `build/ifc`); they are untouched.

## 10. R098.f: the three STEP import defects (2026-10-28)

The fix of findings 11, 12 and 13, and the parent's decision on E1 (ADR 0172 section 7). The code and tests are in
`df0f218e`; the ADR notes are "Implementation notes (R098.f)" in ADR 0172. The IFC import is not changed.

### 10.1 Each defect: cause, fix, test

| Finding | Cause | Fix | Test |
| --- | --- | --- | --- |
| 11: a bad solid's faces join the good body | `src/ifc/step_import.cpp` tessellated a part's whole shape, and the six free faces of `e4-bad-face.step` are in it. The helper wrote `notImported` where the application does not read it (`elements`), so the list was empty | A part with surfaces is tessellated from its solids alone (`solidsOf`). Its surfaces are counted (`freeFaces` 6), located and not imported, as ADR 0172 section 3 says. The helper writes `elements.notImported` as IFC does | `ifc_import` E4 (bad face): six in `elements.notImported` (`face`, `free-surface`); the body is one body of 12 triangles; the identity 8 = 1 + 1 + 6 |
| 12: the report has no locations | `src/ifc/step_import.cpp` never called `Report::locate` | The shared `Report::location` (32 per kind, totals complete) for each outcome that leaves a part out: no shape, no solid, a surface, a tessellation failure, no triangles, the two triangle caps, a mirror variant left out. A location's `step` is the part's entity label; the message gives the reason and the assembly path | `ifc_import` E4 (bad face): six locations, each with `step` > 0 and the reason |
| 13: a PMI-only AP242 file is refused with no counts | The refusal named no entity | The refusal stays (`nothingToImport`). Its report names and counts, by STEP kind, the entities the import does not read, and locates each by its `#` label. The kinds the import reads: the product structure, the representation contexts and the units | `ifc_import` E4 (PMI): exactly DIMENSIONAL_SIZE, SHAPE_ASPECT, MEASURE_REPRESENTATION_ITEM, SHAPE_DIMENSION_REPRESENTATION and DIMENSIONAL_CHARACTERISTIC_REPRESENTATION, one each; five locations |

The accounting for STEP is now `entities` = the package's nodes + the walk's entries that no body was made for + the
surfaces; `omitted` is the last two. For the good box nothing changes (2 = 1 + 1 + 0). For the bad face it is 8 = 1 + 1 + 6.

### 10.2 E1 (parent decision, 2026-10-28)

The parent keeps "within the reported deflection" and drops "1e-3 relative for analytic primitives". ADR 0172 section 7
is amended in place ("amended in R098.f, parent decision"). E1 is **pass** on the numbers of section 1 (R098.e): at
0.02 mm the cylinder's volume is 0.997368 of the analytic value (bound 0.992016) and its area 0.998948 (bound 0.995203);
the assembly is inside its bounds. The test is `ifc_step` (E1 at 0.02 mm), unchanged.

### 10.3 Test runs (R098.f)

All in `sketchyup-ci-pinned:r097a-ifc` with the `ifc-app` tree (Debug, as in section 2), the worktree at `/src`, four
CPUs, 10 GB, the IfcOpenShell prefix from the cache, and `ulimit -v 8000000` for CTest with `-j2`. Builds were run one at
a time, with `-j4`.

| Run | Result |
| --- | --- |
| Baseline at `f51b59ec`, before the change: `ifc_step`, `step_import`, `ifc_step_export`, `ifc_import`, `ifc_sandbox`, `ifc_package`, `ifc_convert`, `ifc_worker_run`, `ifc_export`, `import_refusal` | 10 of 10 pass (37.6 s) |
| At `df0f218e` (the code of these runs; the record and ADR commits change no code), the same 10 suites | 10 of 10 pass (33.5 s) |
| The five touched suites (`ifc_step`, `step_import`, `ifc_step_export`, `ifc_import`, `ifc_sandbox`), five runs | 5 of 5 pass in each run |
| `ifc_step_worker` (the helper's STEP Python suite, `build/ifc-app`) | pass |
| The helper on `e4-bad-face.step`: `freeFaces` 6, `triangles` 12, `nodes` 2, `converted` 1 | as stated |
| The helper on `e4-pmi-ap242.step`: refused as `nothingToImport`, five kinds, one each | as stated |
| The helper on `sketchyup-step-box.step`: unchanged (12 triangles, `freeFaces` 0) | as before |

Not run: the desktop dialog suites (`step_import_dialog` and the rest of the native matrix; they need the `full-ci`
label); the full tier; the sanitizers; the helper-only IFC suites in `build/ifc` (the IFC import's code is unchanged, and
the application's IFC suites ran in 10.3); `parser_fault_corpus`; the package acceptance (`verify-package.sh`).

### 10.4 What was not done

- **Restart-and-exclude (E6), the budgets, the entity cap, and the 4 GiB limit against the 1,000,000-triangle export:**
  not changed, as the brief says. They wait for the owner.
- **PMI in a file that converts.** The PMI kinds are counted and located only in the refusal (the brief's case). A file
  that converts geometry and also holds PMI reports nothing for its PMI, as before. ADR 0172 section 5 would ask for it.
- **Not accepted.** E4 is met in its tests; the owner has not accepted the record. The roadmap, the support matrix, the
  release notes and the changelog are not changed (the parent does that).

### 10.5 Contradictions found

- **`treeTooDeep`.** The notice says the parts deeper than 128 levels "were attached to the deepest kept assembly". The
  code (`src/ifc/step_import.cpp`, the walk) does not add them: they are not imported, and they are neither counted nor
  located. This is outside the three defects and is not changed. The notice and the code disagree.
- **ADR 0172 section 7, E1.** Resolved by the parent's decision (10.2).

## 11. R098.g: the two STEP reporting defects (2026-10-28)

The fix of the two defects that section 10 left open: the PMI of a file that converts (10.4) and the `treeTooDeep`
notice (10.5). The code and tests are in `76d408bf`. ADR 0172 has the "Implementation notes (R098.g)" with the PMI kind
list and its source. The IFC import is not changed.

| Defect | Cause | Fix | Test | Commit |
| --- | --- | --- | --- | --- |
| PMI and GD&T of a converting file not reported (ADR 0172 sections 2 and 5) | The R098.f count ran only on the refusal path (`nothingToImport`) | `tallyPmi` counts the 66 AP242 PMI kinds of `kPmiKinds` by kind (reason `pmi`), locates each by its `#` label, and imports none; notice `pmiNotImported`. The PMI is outside the section 3 identity | `ifc_import` R098.g (PMI) on `e4-pmi-solid.step`: 8 kinds, 41 entities, 32 located, total 41, accounting 2 = 1 + 1 + 0 | `76d408bf` |
| `treeTooDeep` drops parts without a count | The walk returned at its limit without recording the entry, and the notice said the parts were attached. The limit was also one level too deep for the document: a body at depth 127 has 129 records, and the document refuses more than 128 (`src/core/model.cpp`) | `kMaxTreeDepth` is 126. Each entry below it (part or assembly) is counted and located with its label and path, reason `tree-too-deep`, and added to `omitted` (`droppedEntries`). The notice text is corrected | `ifc_import` R098.g (depth) on `deep-tree.step` (128 nested assemblies, a box at depth 1, a box at depth 128): one part and one assembly counted and located, notice 2, omitted 2 | `76d408bf` |

**Runs.** In `sketchyup-ci-pinned:r097a-ifc`, the `ifc-app` tree (Debug), CTest with `-j2` and `ulimit -v 8000000`, at the
final code: 11 of 11 pass (`ifc_step`, `step_import`, `ifc_step_export`, `ifc_import`, `ifc_sandbox`, `ifc_package`,
`ifc_convert`, `ifc_worker_run`, `ifc_export`, `import_refusal`, `ifc_step_worker`). The five touched suites ran five times
each (`--repeat until-fail:5`): 25 of 25 pass. Before the fix, the first `ifc_import` run failed on `deep-tree.step`: the
document refused the chain ("Cyclic or excessively deep hierarchy"). The limit was then corrected and the suites re-run.
`scripts/ci-plan.py check` passed on the tree before the commit: 3835 tracked files, 43 rules, 20 examples hold.

**Not run.** The Release budgets: the PMI pass adds one kind lookup per entity of a converting file, and the 128 MiB parse
and transfer (5.39 to 5.66 s against 6 s) is not re-measured, so its margin is unknown. The sanitizers. The desktop dialog
suites and the native matrix (they need the `full-ci` label). The full tier. The package acceptance. The helper-only IFC
suites in `build/ifc`.

**Contradictions with this record.** Section 10.4 said a converting file reports nothing for its PMI; that is fixed. Section
10.5's `treeTooDeep` is resolved. The 32-per-kind bound is per location kind, as the IFC import has it (ADR 0172, R098.g
notes). The location rule of ADR 0172 section 5 now holds for the PMI and the dropped entries.

**Owner decisions still open.** As in section 9.5 and section 10.4: the restart-and-exclude, the budgets, the entity cap, and
the 1,000,000-triangle export against the 4 GiB limit. The refusal path counts every kind the import does not read (R098.f),
which can include non-PMI kinds; that is not changed here.

## 12. R098.h: the F04 STEP close (2026-10-28)

The owner's decisions 7, 8 and 13 (2026-10-28) are applied to [ADR 0172](../decisions/0172-step-exchange-subset.md) (sections 5,
6 and 7, and the R098.h notes) and to this record. The product code is unchanged (`git diff 2326adfe -- src/` is empty). The
one code change is the measurement generator, `tests/ifc_step_export_tests.cpp`: the modes `--largest`, `--largest-grid`,
`--largest-placements` and `--largest-solids`. The default checks and the R098.e `--budget` mode are unchanged, and the default
checks pass on the final binary (`ifc_step_export: all checks passed`).

### 12.1 The document limits, and what they mean for the largest export

Read in `src/core/document_limits.hpp` (the benchmark scale is 1). A **unique** count is the geometry outside placements,
with each definition's members counted once; a **materialized** count is every copy, placed or not.

| Quantity | Unique | Materialized | Other |
| --- | --- | --- | --- |
| Vertices | 100,000 | 600,000 | 100,000 per body (`Surface::validate`) |
| Faces | 100,000 | 600,000 | 100,000 per body |
| Edges | 300,000 | 900,000 | |
| Wires | 100,000 | 600,000 | |
| Bodies (groups included) | 10,000 | 40,000 | |
| Placements | none | 20,000 | |
| Curves, guides | 10,000 each | 40,000 each | |

The export (`src/io/step_export.cpp`) caps an element at 100,000 triangles and the file at 1,000,000 triangles. It counts
the total once per **distinct** mesh: identical placed copies share one mesh by digest. The helper's address space is 4 GiB,
its deadline 120 s, and the output 256 MiB. The edit history allows 64 MiB per edit (`src/core/model.cpp`).

Two facts change the premise of decision 13, which says a document holds 600,000 vertices:

- The 600,000 is the **materialized** limit, reachable only through placements of a component definition. A plain body is
  limited to 100,000 unique vertices. Measured: two plain bodies of 100,000 vertices each are refused at the second body
  ("vertices would be 200,000 of 100,000").
- The export writes each distinct mesh once, so a placement adds an instance node, not triangles. The export's cost follows
  the **unique** geometry, which holds at most 100,000 faces and triangles. The 1,000,000-triangle total cannot be reached by
  any document, so the old budget model (R098.e, 1,020,000 vertices) was never a document-reachable model.

So the largest exportable model is measured on the unique limits (the export's work) and on the materialized limits (the
document's size). Four models were built and exported; all four completed under the 4 GiB address-space limit.

### 12.2 The four models

Built in `tests/ifc_step_export_tests.cpp` in the pinned image, Release, `build/release` (the `sketchyup-cli`, the helper and the
test binary, the IfcOpenShell prefix built with them). Each is exported by `exportStepFile`, the function the application and
`sketchyup-cli --export-step` both call, with the default `IfcWorkerOptions` (4 GiB, 120 s, four threads). The test calls it
in memory, not through the CLI binary, because the model is built in memory. The native save of each model is 9.7 to 13.4 MB
(the file limit is 128 MiB); the saves were not reloaded through the CLI.

| Model | Materialized (vertices, faces, edges, triangles, bodies, placements) | Distinct (faces, triangles, meshes) | Output | Helper peak (RSS) |
| --- | --- | --- | --- | --- |
| **M2: five placements of a 200 × 250 grid of triangle faces** (the largest export, the row's model) | 252,255; 500,000; 752,250; 500,000; 10; 5 | 100,000 faces, 100,000 triangles (one element, at the element cap), 50,451 vertices; 1 mesh | 216,390,153 B | 2,044 to 2,045 MiB |
| M4: 10,000 separate closed pentagonal bipyramids (7 vertices, 10 faces each) | 70,000; 100,000; 150,000; 100,000; 10,000; 0 | 100,000 faces, 100,000 triangles, 10,000 solids | 255,583,213 B (243.7 MiB) | 2,473 to 2,474 MiB (the largest peak) |
| M1: six placements of a 980 × 102-gon + one 40-gon definition (600,000 vertices, the materialized vertex limit) | 600,000; 5,886; 600,000; 588,228; 12; 6 | 100,000 vertices, 981 faces, 98,038 triangles; 1 mesh | 91,176,661 B | 850 to 851 MiB |
| M3: 20,000 placements of one triangle under a group (40,000 bodies, 20,000 placements) | 60,000; 20,000; 60,000; 20,000; 40,000; 20,000 | 3 vertices, 1 face, 1 triangle; 1 mesh; 20,000 elements | 33,990,861 B | 365 to 366 MiB |

### 12.3 The runs

Each figure is the wall time of one `exportStepFile` call. The machine is shared with another worker; the gate (no `ninja` or
`cmake` running) was checked before each build, not before each run. The spread within a batch is under 0.5 s for M1, M3
and M4. For M2 the second and final batches span under 0.3 s; the first batch spans 18.6 to 22.3 s.

| Model | Batch | Runs (ms) | p50 | Max |
| --- | --- | --- | --- | --- |
| M2 | final | 18,602; 18,772; 18,659; 18,567; 18,587 | 18,602 ms | 18,772 ms |
| M2 | second (an earlier build of the same test) | 18,533; 18,531; 18,652; 18,645; 18,470 | 18,533 ms | 18,652 ms |
| M2 | first (an earlier build of the same test) | 19,621; **22,317**; 18,565; 18,629; 18,682 | 18,629 ms | 22,317 ms |
| M4 | final | 17,680; 17,722; 17,575; 17,561; 17,463 | 17,575 ms | 17,722 ms |
| M4 | first | 17,036; 17,283; 17,392; 17,444; 17,508 | 17,392 ms | 17,508 ms |
| M1 | final | 6,485; 6,468; 6,514; 6,552; 6,503 | 6,503 ms | 6,552 ms |
| M1 | first | 6,664; 6,568; 6,571; 6,583; 6,664 | 6,583 ms | 6,664 ms |
| M3 | final | 2,677; 2,719; 2,713; 2,699; 2,693 | 2,699 ms | 2,719 ms |
| M3 | first | 2,595; 2,651; 2,625; 2,631; 2,632 | 2,631 ms | 2,651 ms |

Across the fifteen M2 runs the p50 is 18,629 ms, and the maximum is 22,317 ms (one run of the first batch; the other fourteen are
18,470 to 19,621 ms). The earlier batches used builds of the same test that differed only in the model's labels and in how the
builder was written; the export path is the same. The helper's peak is the largest child's resident size
(`getrusage(RUSAGE_CHILDREN)`, the helper is the only child), taken as the maximum over the runs so far. It is resident
memory, not the address space, and every run completed under the 4 GiB limit, so the address-space peak stayed below it.

For the record: the R098.e 1,000,000-triangle model (20 bodies of 500 102-gons, 1,020,000 vertices, a single document) could
not be built in one document (its edit was refused), and its helper alone used 8,446 MiB with no limit and crashed at the
4 GiB limit (section 9.3).

### 12.4 The budget row (decision 13)

The row is the largest exportable model's export: **M2**, the model with the largest export time (the most distinct
triangles, at the element cap). Its measured maximum is 22.3 s, so the budget is **23 s**, rounded up to the next whole
second. The 4 GiB address-space cap is kept and is not raised. M4 uses more helper memory (2,474 MiB) and more output (255.6 MB,
under 256 MiB), and it also completes under the cap. The new row is in ADR 0172 section 6, marked "(replaced in R098.h, owner
decision 13)".

If the owner wants the typical figure instead of the outlier, the M2 median is 18.6 s and the batch maxima are 18.8 s (final)
and 22.3 s (first); the budget follows the rule as written, the measured maximum.

### 12.5 The owner's decisions applied (2026-10-28)

- **Decision 7 (restart-and-exclude):** not in 1.4.0; planned for 1.5. A STEP file that crashes, or hits a limit, is refused
  whole with its log tail, and the document is unchanged. ADR 0172 section 5 and E6 amended in place.
- **Decision 8 (speed):** two budgets are missed and accepted as documented limits for 1.4.0. The import of the 100-sphere
  model takes 14.02 to 14.11 s with a new native file, against 10 s (R098.e). The export of the 90,954-face imported model
  takes 16.80 to 16.83 s, against 5 s, and 13.1 s of that is in the OCCT writer (R098.e). Speeding up the application's
  conversion step is for 1.5. ADR 0172 section 6 amended in place.
- **Decision 13 (STEP export):** the 4 GiB cap stays. The 1,000,000-triangle budget is replaced by M2's row (section 12.4).

### 12.6 Final verdicts (E-checks, ADR 0172 section 7)

| Check | Final verdict | Basis |
| --- | --- | --- |
| E1 dimensional | pass | R098.f (the parent's decision: the reported deflection governs) |
| E2 tessellation | pass | R098.e |
| E3 units | pass | R098.d and the R098.e to R098.g suites |
| E4 invalid or unsupported | pass | R098.e to R098.f: the bad face and the PMI are reported with locations; the R098.g PMI and depth defects are fixed |
| E5 round trip | pass | `ifc_step_export` (AP214 and AP203) |
| E6 limits and refusal | **pass for the refusals and the stops; the restart-and-exclude is deferred to 1.5 (decision 7)** | `step_import` C8 to C11 (R098.e), the deadline and cancel (C5, C6) |
| E7 native | pass | `ifc_step_export` E7 |
| E8 report | pass (R098.e: on a stand-in helper's message; the real helper's texts are clipped to the same 240 bytes) | `step_import` C12 |

### 12.7 Budget verdicts (ADR 0172 section 6), and the documented limits for 1.4.0

| Row | Verdict |
| --- | --- |
| Input file 128 MiB | met |
| Helper resource limits | 4 GiB, 120 s, CPU 480 s and output 256 MiB are set. The engine reports one thread (BRepMesh is single-threaded in this build), there is no entity cap, and there are no restarts (decision 7). The thread count and the entity cap were not covered by the owner's decisions. **Parent decision (2026-10-28):** ADR 0172 §6 now states one thread and no entity cap, bounded by the 128 MiB input limit and the helper's 4 GiB, 120 s and 480 s limits; an explicit entity cap is a 1.5 follow-up. **Met, as amended** |
| Triangles, 1,000,000 total and 100,000 per element | met at the helper; the 1,000,000 total is not reachable by a document (section 12.1) |
| Package; report; application memory | met (as R098.d recorded) |
| Parse 64 MiB; parse 128 MiB | met (R098.d) |
| Tessellate and import of the 100-sphere model, 10 s | the helper alone, 4.0 s: met. The end-to-end import, 14 s: **missed, accepted as a documented limit (owner decision 8)** |
| Export of the 1,000,000-triangle model, 5 s | **replaced in R098.h (owner decision 13)** by the row of M2: **23 s** |
| Export of the 90,954-face imported model, 5 s | **missed, accepted as a documented limit (owner decision 8)**: 16.8 s, 13.1 s in the writer |
| Export of the largest exportable model, 23 s | **M2 measured: 18.6 s p50, 22.3 s max** (fifteen runs); met at the budget |

**Documented limits for 1.4.0** (the "Accepted (R098.h)" status at the top of this record): the 100-sphere import at 14 s; the 90,954-face
export at 16.8 s; no restart-and-exclude for STEP; the thread count and entity cap of the helper (the row above, for the
parent). The 4 GiB cap holds for every model the document can hold (section 12.2): no measured model needed it raised.

### 12.8 Contradictions and findings for the parent

- **The 600,000-vertex premise.** A plain document holds 100,000 unique vertices. The 600,000 needs placements, and the
  export counts each distinct mesh once, so placements do not raise the export's work (section 12.1).
- **The 1,000,000-triangle export is not reachable by a document.** The unique limits (100,000 faces and triangles) allow at
  most about 100,000 triangles to reach the export. ADR 0172 section 6's 1,000,000 total is a cap that no document reaches,
  and the R098.d and R098.e triangle figures (975 MB, 8.4 GiB) describe a model the document cannot hold.
- **The 64 MiB edit history.** A model's creation in one edit is refused when its history is over 64 MiB (`src/core/model.cpp`,
  "Edit exceeds the 64 MiB history budget"). The first attempt at M1 failed this way (one edit for six bodies). M1 and M2 are
  built with one placement per edit, and M3 and M4 in batches of 1,000 and 500. This bears on any import or edit that creates
  a large model in one step; it is not tested for STEP or IFC imports here.
- **`Surface::addFace` copies the surface and `Surface::vertex` searches every vertex** (`src/geometry/surface.cpp`). The
  generator avoids both for the large bodies. The cost is quadratic in the number of faces and vertices. This is an
  observation, not a fault in any of the checks above.
- **Decision 8's figure.** The decision text says the 90,954-face export takes "about 17 s". R098.e measured 16.80 to 16.83 s,
  and this record uses the measured figure.
- **Not run.** The cancel and progress of the export on the real helper (the command line has no cancel); the package
  acceptance; the sanitizers; the desktop suites, which run in the full tier with the `full-ci` label. The record's R098.d
  verdict on the helper's thread count and entity cap is not re-measured.

## 13. R098.i: restart-and-exclude for STEP (2026-10-29)

Owner decision 7 (2026-10-28) deferred the restart-and-exclude to 1.5. This section records its build. Branch
`feat/r098i-step-restart`, [PR #522](https://github.com/zygote55/sketchyup/pull/522). ADR 0172 section 5 and the E6 row
are amended in place (R098.i), and the design is the one section 9.2 estimated (about 150 to 260 lines; the diff is near
that).

### 13.1 What was built

- **The helper** (`src/ifc/worker_main.cpp`, `src/ifc/step_import.cpp`, `step_import.hpp`). `--import-step --output PKG`
  takes `--exclude-step N` (repeatable). In the tessellation, a base whose index is N is left out before any work on it:
  the check comes before the base's shape is fetched, so the work that stopped the run is not repeated. The base is
  located as `excluded` with its part's name and its path, counted as omitted, and the helper prints `P excluded N`. A
  group (an assembly) is never left out, since it has no mesh.
- **The application** (`src/io/step_worker.cpp`, `step_worker.hpp`). The restart loop in `importStepFile`. A stop is
  restarted when the helper stopped in the tessellation (`P phase tessellate` with a `P at` base) and its reason is a
  deadline (`timeout`), a signal other than SIGSYS (`crash`), or a memory failure (`memory`). The base is added to the
  excluded list and the helper runs again. `StepWorkerOptions::maxRestarts` is 3, as in IFC.
- **The partial import.** It carries the report's `excluded` list (each entry `step` = the base's index, and `reason`),
  the `restarted` notice (count = restarts, the IFC text), `engine.restarts`, and the `excluded` locations, which name the
  elements. A failed import carries `engine` (completed, restarts, stops), `excluded` and `logTail`.
- **The desktop** (`src/app/ifc_sheets.cpp`, `stepImportReportFace`). A group, "Left out after the helper stopped", lists
  each excluded location's message (the part's name, its entity label and its path). The group appears only for a partial
  import. The banner's restart count and the restarted notice were already the IFC face's; the STEP face inherits them.

### 13.2 The protocol change

- One new option on `--import-step`: `--exclude-step N`. N is a base index, the value of the helper's `P at` line (the
  order of the unique bases in the tessellation), not an entity label.
- One progress line from the helper: `P excluded N...`, the line IFC uses. For STEP the helper prints it only for a base the
  application passed, so the application's merge adds nothing new; the line is there so the stop is visible in the log.
- Unchanged: the package format, the `P at`, `P phase`, `P tasks` and `P done` lines, the R line, the refusals and the
  limits (4 GiB, 120 s, 480 s CPU, 256 MiB output, 128 MiB input).

### 13.3 The restart rule

| The helper stopped with | Restarted? | Result |
| --- | --- | --- |
| a `P at` base in the tessellation, reason `timeout`, `crash` or `memory` | yes, up to three times | the base is left out; a partial import, or a failed import once the restarts are used up |
| a `P at` base, reason `crash` by SIGSYS (`sandboxViolation`) | never (ADR 0183 section 4.6) | refused whole as `sandboxViolation` |
| no `P at`, or the read phase, or the write phase | no | refused whole, as before (`workerFailed`, the stop's reason) |
| a helper refusal (`R` with status `refused`) | not a stop | refused with the helper's words, as before |

The number of runs is at most four: the first run and three restarts. The 120 s deadline applies to each run.

### 13.4 Tests and runs

All run at the tree of the R098.i commits, on the pinned image `sketchyup-ci-pinned:r097a-ifc` (GCC 16.2.1, OCCT 7.9.3,
Qt 6.11.2), in the worktree mounted at `/src`, `--user` the build's user, `--memory 10g --memory-swap 10g`, `-j4` for builds.

| Suite | What it checks | Result |
| --- | --- | --- |
| `step_import` (application, a stand-in helper; `ifc-app` tree, Debug) | the existing checks unchanged; R098.i: SIGSEGV at base 1 gives a partial import naming 1, two runs; a hang past a 5 s deadline at base 1 gives a partial import with reason `timeout`, two runs; a SIGSYS stand-in is refused as `sandboxViolation`, one run; a read-phase stop is refused whole, one run, no base left out; a write-phase stop is refused whole, one run; a helper that stops on every run gives three restarts (four runs), a failed import whose message names the three restarts, with the stops (four), the excluded bases 0, 1 and 2 and the log tail | pass (9.04 s) |
| `ifc_step_worker` (helper, Python, the committed fixture `sketchyup-step-assembly.step`) | R098.i: `--exclude-step 1` prints `P excluded 1`; one body and one mesh fewer; one base omitted; the meshes that remain are the run without the option's, byte for byte and in order; the root is still first; the left-out base is located as `excluded`; a base the file does not have (`--exclude-step 99`) gives the same package bytes as no option | pass (the direct run in the image, and under ASan, 2.05 s) |
| `step_import_dialog` (desktop, the `native` tree, default build type) | the existing STEP dialog flow unchanged; a whole import names no element left out; R098.i: a partial import's report shows one restart in the banner, the "Left out after the helper stopped" group with the named element, and the restarted notice | pass on X11 at 1x (`xvfb-run`, `QT_QPA_PLATFORM=xcb`) and on Wayland at 2x (`scripts/test-wayland.sh`, `SKETCHYUP_TEST_SCALE=2`) |

**The CI "IFC library and helper" job's lines, run locally** (from `.github/workflows/native.yml`):

- `ctest --test-dir build/ifc -R '^ifc_' -LE full --output-on-failure --no-tests=error --parallel 2`: 8 of 8 pass (38 s) on
  the final tree, with the new exclusion checks in `ifc_step_worker`.
- `ctest --test-dir build/ifc-app -R '^(ifc_package|ifc_convert|ifc_import|ifc_worker_run|ifc_export|ifc_step|ifc_step_export|step_import|import_refusal|ifc_sandbox)$' --output-on-failure --no-tests=error --parallel 2`:
  10 of 10 pass (38 s) on the final tree.
- The sanitized helper suites, `ctest --test-dir build/ifc-sanitize -R '^(ifc_worker_probe|ifc_import_worker|ifc_georeference|ifc_export_worker|ifc_worker_fault_corpus|ifc_step_worker)$' --output-on-failure --no-tests=error`, with `ASAN_OPTIONS=detect_leaks=0`: 6 of 6 pass (235 s), with the helper built under ASan and UBSan (`build/ifc-sanitize`, the uninstrumented library).

Not run: `ifc_worker_fault_corpus_full` (the 512-mutation corpus, label `full`), which is in the full tier only.

### 13.5 What was not done

- **The STEP runner is not folded into `runIfcWorker`.** The brief made the fold optional. It needs a progress hook in
  `runIfcWorker`, and this change is the restart; the fold is left for a later change, and the STEP `runOnce` keeps its own
  progress parsing (it now parses `P excluded` too).
- **The refusal sheet** (`showStepRefusal`) is unchanged. A failed restart's details (the stops, the excluded list and the
  log tail) ride its report and its copy-details. The sheet's own words say the helper stopped, not which base it was on.
- **A real crashing file.** The restart is tested with stand-in helpers, as the brief asks; no committed STEP fixture
  makes the pinned helper crash, so the path is not exercised against OCCT's real failure.
- **Mutation checks** were not run for this slice.

### 13.6 Findings for the parent

- **The `excluded` entries name a base index, not an entity label.** IFC's `excluded` entries carry the product's `#id`.
  STEP's carry the base's index in the tessellation (the value of `P at`), because that is what the helper reports and
  what `--exclude-step` takes. The entity label is in the `excluded` location, and the desktop names the element from that.
- **ADR 0172 section 6's row for the helper's limits** said STEP "has no restarts" (owner decision 7). That row is amended
  to match (R098.i).
- No contradiction with ADR 0171: decision 19 and the IFC restart are unchanged, and the STEP restart copies their rule.
