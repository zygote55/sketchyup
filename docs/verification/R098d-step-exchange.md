# R098.d — STEP exchange subset: acceptance record

2026-10-25. Branch `feat/r098d-step-publish`, based on `main` at `c898a9e3`. The product code and the tests
ran at `33e175fa`; the commits after it change documentation only. Every check below ran at `33e175fa` unless
its row says otherwise, in the pinned image `sketchyup-ci-pinned:r097a-ifc` (GCC 16.2.1, OpenCASCADE 7.9.3,
Qt 6.11.2, Python 3.14), 22 CPUs and a 10 GB container. The checks are those of
[ADR 0172 §7](../decisions/0172-step-exchange-subset.md#7-verification-the-r098-verify-line) (E1 to E8),
§6 (the budgets) and §8 (packaging and CI). [Summary](R098d-step-exchange.json).

**Status.** The Verify line is **partly met**. Three of the eight checks pass in full (E3, E5 and E7), and
five are partial, each with its gap named in its row: E1 (the cylinder's bound is looser than §7 says), E2
(the non-planar and degenerate counts are not asserted), E4 (no fixture with a bad face or a PMI-only AP242
file exists in the corpus), E6 (the STEP import has no restart protocol, so ADR §5 and E6's
restart-and-exclude clause are not implemented) and E8 (the message bound is not asserted for STEP).
Budgets, measured on the Release build: the 64 MiB and 128 MiB parse rows and the helper's tessellation are
met; three rows are missed and recorded as **missed: owner decision pending**: the end-to-end import of the
100-sphere model (13.8 to 15.8 s against 10 s), the export of the 90,954-face imported model (16.6 to 16.8 s
against 5 s), and the export of the 1,000,000-triangle model (R098.c, not re-run). No budget was changed.

## 1. Check map

Each row names the test or command that proves the check, the result of the run made today, and the commit it
ran at. "Not run" always comes with the reason.

| Check | Clause (ADR 0172) | Proof: CTest suite or command | Result today | Commit |
| --- | --- | --- | --- | --- |
| E1 | §7, dimensional: analytic box exact; cylinder and assembly within the bound | `ifc_step` (box volume and area to 1e-9 absolute; cylinder to 5 % relative; assembly two solids); `ifc_step_worker` (the same in Python) | pass. **Partial:** the cylinder is asserted to 5 %, not "within the reported deflection" as §7 says. Measured in R098.a2: 0.989 x volume, 0.995 x area | 33e175fa |
| E2 | §7, tessellation: planar faces merge; non-planar stays triangles and is counted; collapsed triangles dropped and counted; caps enforced | `step_import` C3 (the box comes out as six faces); `ifc_step` §6 (a total cap of 50 omits the cylinder and reports it; a per-element cap of 50 refuses) | pass for the merge and the caps. **Partial:** `nonPlanarFaces` and `degenerateTriangles` are reported but no test asserts them. Measured today on one 10,108-triangle sphere: 2,021 non-planar faces, 2 degenerate triangles dropped. The document holds 100,000 unique faces (§7, finding 3), so the 1,000,000-triangle total cannot be imported into one document | 33e175fa |
| E3 | §7, units: a declared unit converts and is reported; no unit refuses; a mismatched option does not rescale | `ifc_step` §4 (inch fixture comes out 254 x 508 x 762 mm, factor reported; a mismatching `--step-length-unit` does not win); §5 (no header and no unit refused, exit 2); `ifc_step_worker` (the same, Python) | pass | 33e175fa |
| E4 | §7, invalid or unsupported: reported with locations and counts; the rest converts; the accounting identity holds | `ifc_step` (accounting: 2 entities = 1 converted + 1 supporting + 0 omitted); `step_import` C2 (AP242-only and unsupported formats refused with the helper's codes) | **Partial.** The accounting identity and the refusals pass. **Not run:** a fixture with a bad face, an entity the translator refuses, or a PMI-only AP242 file: none is in the corpus. `mirror-box.step` was malformed, but it was not a designed E4 fixture, and it is now fixed (see the corpus record) | 33e175fa |
| E5 | §7, round trip: AP214 and AP203 export re-imports with geometry, names, colours, `step.Layer`, hidden state and the assembly/instance structure | `ifc_step_export` (`roundTrip` for `ap214` and `ap203`, through the real helper: volumes and bounds of each top-level body, names, colours, layer, hidden, one definition with three instances at three placements; E3 export half: the file declares `.MILLI.`) | pass | 33e175fa |
| E6 | §7, limits and refusal: a file over 128 MiB refuses with the count and the limit named; a worker over its limits or killed is restarted and the element excluded, or fails with its log tail; the document is unchanged | Refusal: `tests/step_budgets.py` (`refuse-129m`, a run today). Stop and cancel: `step_import` C5 (deadline) and C6 (cancel), which assert the model is untouched. **Not run:** the 128 MiB + 1-byte case as a CTest; SIGSEGV for STEP; RLIMIT_AS and RLIMIT_CPU for STEP | **Partial.** Refusal: pass (today: `LIMIT_EXCEEDED`, `tooLarge`, 134,217,729 bytes, 0.01 s, "larger than 128 MiB"). Stop and cancel: pass with a stand-in helper. **Not met:** the restart-and-exclude clause. `src/io/step_worker.cpp` states that the STEP import "has no per-element restart protocol: a stop is a failure"; ADR §5 and E6 require up to three restarts and then a partial import naming the excluded elements | 33e175fa |
| E7 | §7, native: the document validates (`decodeDocument(encodeDocument())`); save and reopen bit-exact; one undo removes the import; provenance resolves | `ifc_step_export` E7 checks, added in R098.d, run on each round-trip import through the real helper: encode, decode and re-encode bit-exact; one undo empties the document; every geometry body's `step.Source` equals the file's digest in the report | pass (both schemas) | 33e175fa |
| E8 | §7, report: messages ≤ 256 bytes; report ≤ 64 KiB; counts complete past the 32-per-kind bound | Report size: `tests/step_budgets.py` (the 100-sphere import's report, 6,166 bytes). Messages: **not run for STEP**; the shared report code is asserted by the IFC suites (`ifc_import`, `ifc_convert`) | **Partial:** report size pass; message bound not asserted for STEP | 33e175fa |
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

- **E4 fixtures.** A bad face, an entity the translator refuses, and a PMI-only AP242 file are not in the corpus.
  Writing them is a corpus change for the owner to approve; no such fixture was added here.
- **E6 restart.** Not run: there is no restart protocol for STEP to test (§7, finding 1).
- **E6 SIGSEGV and RLIMIT for STEP.** The IFC suite `ifc_worker_run` covers these for IFC. A STEP twin would need a
  STEP fixture that crashes the helper, which the corpus does not have.
- **E8 messages for STEP.** Not asserted; the shared report code is asserted by the IFC suites.
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
| Tessellate the model (≤ 1,000,000 triangles) | ≤ 10 s, with progress; cancel returns in ≤ 100 ms and the helper ends within 1 s | The helper alone on the 100-sphere model (990,584 triangles): 4.04, 3.98, 4.08, 4.99 s; progress: 101 `P at` lines. The command-line import of the same file, end to end, with a new output file: 13.84, 15.76, 14.27, 13.99 s, keeping 9 bodies (`documentLimit`). Cancel on the real helper: **not measured** (the command line has no cancel) | the helper's tessellation **met**; the end-to-end import **missed: owner decision pending**; cancel **not run** |
| Export of the 1,000,000-triangle model | ≤ 5 s, with progress and cancel; output ≤ 256 MiB | **Not re-run for the 1,000,000-triangle model**: a document cannot hold it (the 100,000-face limit), and a helper-only run needs a package writer the repository does not have. Re-run today, Release: 20,000 triangles, 1.04 s, 18,121,042 bytes; 50,000 triangles, 2.72 s, 45,951,294 bytes (`ifc_step_export --budget 2 100` and `5 100`, one run each). The imported 90,954-face model: 16.75, 16.64, 16.61 s, 223,950,326 bytes (213.6 MiB). The R098.c measurement for 1,000,000 triangles, cited and not re-run: helper alone 73.9 s and 974.9 MB with no address-space limit; with the 4 GiB limit the helper segfaults after 26 s; with the 256 MiB output limit it is stopped at 60.7 s | the 1,000,000-triangle row **missed: owner decision pending** (R098.c). The 90,954-face export **missed on time** (16.6 to 16.8 s against 5 s, **owner decision pending**), and met on size |

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
4. **The export size note comes too late for triangle faces.** The export dialog warns above 250,000 triangles.
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

## 8. Commits

- `7e46236b` — packaging closure check, `STEP-SOURCE.txt` install, path map.
- `9e9d66f7` — the corpus record and the regenerated mirror fixture.
- `33e175fa` — the loss wording in the dialog and report, the E7 checks, the budget tools. **The commit the tests ran at.**
- The commit after `33e175fa` adds this record, the user guide, the support matrix, the roadmap row and the ADR notes.
