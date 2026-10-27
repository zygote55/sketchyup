# R101.f — Quantities, reports and schedules: acceptance record

2026-10-27. Branch `docs/r101f-quantities-acceptance`, based on `main` at `6dcea44a` (the merge of PR #480, R101.e2).
The product code is unchanged by this record. Its local checks ran at `6dcea44a`. The full-tier CI evidence for the
Quantities panel comes from PR #480's own full-tier run at its head `6c371a5e`, because no full-tier run on `main` has
included the panel yet (see §2). The R101 product files are identical at `6c371a5e` and `6dcea44a`; the 14 commits
between them change no file under `src/core/report*`, `src/automation/report*`, `src/app/quantities*`, `src/io/report*`
or `tests/report*`, `tests/quantities*` or `tests/fixtures/reports` (checked with `git diff`).

The checks are those of [ADR 0175 §11](../decisions/0175-quantities-reports-schedules.md#11-verification-plan-fixtures-with-hand-computed-expected-totals)
(V1 to V12), §12 (the budgets, with the owner's 2026-10-27 change of the panel re-run to p95 ≤ 1 s off the UI thread),
and the Implementation split. [Summary](R101f-quantities-reports.json).

**Status.** The Verify line is **partly met**. Nine of the twelve clauses pass: V1, V2, V3, V4, V5, V8, V9, V10 and V11.
Three are partial, each with its gap named in §1: V6 (the `degenerate` status cannot be built through the public API,
so one of the three contexts the ADR names is not tested), V7 (the UI check runs on `quantities-a`, not on a large model,
and the 100 ms cancel budget is measured here, not asserted in CI) and V12 (the exported JSON omits the session-only body
IDs, so it is not byte-identical to the session result).

Budgets: six of the seven §12 rows are met on the Release build. **The panel re-run row is missed**: p95 1,097 ms in one of
four series, against 1 s (the other three series: 965, 569 and 800 ms). It is frozen as a **fail** with that number. No
budget was changed. The §12 table in ADR 0175 carries the figures.

The roadmap row's status is therefore "Delivered, with open findings", not "Accepted". §8 lists what the owner must decide.

## 1. Check map

Each row names the test and assertion that prove the clause, the CI job from the most recent green full-tier or fast-tier
run that covers it, and the scripted run made for this record. "Not run" always comes with the reason. Test line numbers
are in the file named.

Run identifiers: **M** is the most recent green `main` run, `40580430398` (fast tier, `6dcea44a`, CI verdict job
`122461448626`, success). **N** is the nightly full tier on `main`, `40503622529` (`61b2018b`, 2026-10-27 12:25 UTC,
success). **P** is PR #480's full-tier run, `40563748036` (head `6c371a5e`, label `full-ci`, CI verdict success).

| Clause (ADR 0175 §11) | Proof: test and assertion | CI job (run) | Scripted run (this record) | Result | Gap |
| --- | --- | --- | --- | --- | --- |
| **V1** Shared components: definitions counted once; `Table` filter gives 2 placements and their 10 owned records; `Boxes` row 3 / 12 m³ / 48 m² | `report` (`tests/report_tests.cpp`, `components()`, V1 asserts at lines 157 to 175) | M: `Fast tier: tests 4/5` (job `122459106252`); N: `Development build and checks` (`122187330427`); P: `Development build and checks` (`122400019678`) | `sketchyup-cli --group-by definition` on `quantities-a`: BoxP 3 instances, 6 contexts, 12 m³; Table 12 placements, 22 contexts, 4.4 m³; 4 definitions. `--group-by tag`: Boxes 3 instances, 6 contexts, 48 m², 12 m³ | **Pass** | none |
| **V2** Nested instances: `Table` yields 8 `Post` and 2 `Top` placements; a `definition=Post` filter counts exactly 8; members under the outer definition | `report` (`components()`, V2 asserts at lines 183 to 187) | as V1 | `--definition 2` (Post): 8 instances, 16 contexts, all under Table. `--definition 3` (Top): 2 instances, 4 contexts | **Pass** | none |
| **V3** Hidden content: the default totals include `Stash` and `Off` (82.4 m³, 40,259.2 m²); `visible` excludes them (79.4 m³, 40,241.2 m²); the mode is stated | `report` (`quantitiesA()`, lines 235 to 256); `report_output` (`metadataMatrix()`, the `#filters.visibility` line) | as V1; `report_output`: M shard 4/5 (`122459106252`) | Default: 82.4 m³, areas 40,259.20 m², 120 contexts. `--visible`: 79.4 m³, areas 40,241.20 m², 117 contexts, `#filters.visibility=visible` | **Pass** | none |
| **V4** Transforms: the mirrored copy equals the unscaled box (4 m³, 16 m²); a 2× scale is ×8 volume and ×4 area; the three-level chain is 27 m³ and 54 m² | `report` (`transforms()`, lines 110 to 146) | as V1 | The default CSV (which includes the copies) equals its golden byte for byte (§3) | **Pass**, with the ADR's chain area corrected from 9 m² to 54 m² (R101.a, §8 item 4) | owner decision 4 |
| **V5** Geometry edits update totals: scaling `Stash` ×2 adds 7 m³ and 18 m²; the revision advances; undo restores the totals under a higher revision | `report` (`quantitiesA()`, lines 261 to 282; the test labels these checks V9) | as V1 | Not scripted: the CLI has no edit command | **Pass**. The ADR's "lower revision" on undo is corrected, as §4 requires | none |
| **V6** Invalid solids: the three invalid contexts appear under their exact statuses; none is in a volume total; `analysis_limit` is listed | `report` (`invalidSolids()`, lines 191 to 224: `open_boundary`, `non_manifold`, and 65 disjoint cubes over the 64-shell limit as `analysis_limit`); `quantitiesA()` line 259 | as V1 | Default run: `invalid,open_boundary` 1 context; `invalid,non_manifold` 1 context; `volumes,unvalidated` 2; the volume total is 82.4 m³ with no invalid context in it | **Partial.** The `degenerate` status cannot be built through the public API (R101.a, point 6), so two of the three named statuses are reached, plus the `analysis_limit` case | owner decision 3 |
| **V7** Live update: "computing" shows on a large model without freezing the UI; Cancel returns in ≤ 100 ms | `quantities_input_tests` (`main`, sections 3 and 4): the computing line and its revision, Cancel ("Cancel returns at once", asserted under 1 s), a superseding revision, Escape, on `quantities-a` with a gate holding the pass. `report` (`hooksAndCancel()`): the engine's cancel semantics | P: `Native tests: X11 1x`, `X11 2x`, `Wayland 1x`, `Wayland 2x` (`122422259215`, `122422259068`, `122422259026`, `122422259125`); M: `Fast tier: X11 1x 1/2` (`122459106272`) | `report_benchmark --cancel 10000`, Release, three runs: the run returned 0.05 to 2.80 ms after the flag was set, at four points in the pass; the longest gap between two records was 1.81 ms (§6) | **Partial.** The UI was never run on a large model. The 100 ms budget is measured here, not asserted in CI, because `report_benchmark` is `EXCLUDE_FROM_ALL` and no CI job runs it | gap 2 |
| **V8** Units and precision are stated: the unit codes and precision for m, mm and ft-in, and for `full` and 3 | `report` (`unitsAndPrecision()`, lines 286 to 336: mm 82.4 m³ is 82,400,000,000 mm³; ft-in 4 m³ = 141.2587 ft³ within 1e-4); `report_output` (`metadataMatrix()`, lines 239 to 276) | as V1 | CLI header of the default run: `#units.length=m`, `#units.area=m2`, `#units.volume=m3`, `#precision=full`; the material run states `#precision=3` | **Pass**. mm and ft-in are run in-process only, not through the CLI | none |
| **V9** Source revision stamp: equals the document identity and revision; an edit moves it; undo and redo are both reported exactly | `report` (`quantitiesA()`, lines 261 to 282) | as V1 | CLI header: `#documentId=6afe7174137d67ffdf04a70703d76b13`, `#revision=53`; a second run carries the same pair | **Pass** | none |
| **V10** CSV dialect: UTF-8, no BOM, LF only, minimal RFC 4180 quoting (a name with a comma and quotes), the metadata block, byte-identical repeats, hash-pinned goldens | `report_output`: `csvDialect()`, `quoting()`, `goldens()` (repeated runs identical; equal to the golden), `pins()` (SHA-256 of each golden) | as V1; `report_output`: M shard 4/5, N, P | Both goldens reproduced byte for byte by the CLI (`quantities-a-default.csv`, `.json`, `quantities-a-material.csv`, `.json`); two runs of one request identical; no BOM, no CR, last byte LF | **Pass** | none |
| **V11** Saved schedules: the store round-trips; a save is bit-identical with or without a schedule; a schedule for another document is refused | `report_schedule` (`storeRoundTrip`, `bitIdenticalSave`, `identityGuard`); `report_schedule_cli` (`flags`, `identityGuard`) | M: `report_schedule` (shard 5/5, `122459106245`), `report_schedule_cli` (shard 3/5, `122459106228`); N and P: the full tier | `--save-schedule` leaves the model file's SHA-256 unchanged; a scheduled CSV equals the ad-hoc CSV except line 5 (`#schedule=A-default` for `#ad-hoc=1`); `--schedule` on `quantities-b` refused `WRONG_DOCUMENT`, nothing written; `--delete-schedule` and `--list-schedules` | **Pass** | none |
| **V12** CLI: `--report` writes a new file and never replaces; refusals; the schedule flags; output byte-identical to the session `report.run` for the same request and revision | `report_cli`: `writesNewFileOnly`, `refusesBadFlags`; `report_output`: `sessionParity`; `report_mcp`: `runParity`; `report_schedule_cli`: `flags` | M: `report_cli` (shard 5/5); `report_mcp` (shard 5/5); N and P: the full tier | An existing path is refused (`FILE_EXISTS`, exit 1, the file's SHA-256 unchanged); `--format` against the extension is refused (`INVALID_REQUEST`); repeated runs identical | **Partial.** The CSV is byte-identical to the session's CSV. The JSON file is the session's data less the session-only body IDs and truncation flag (R101.e1: "the exported JSON file does not carry the body IDs", decided by the parent on 2026-10-27) | owner decision 2 |

## 2. Test runs

| Run | Where | What ran | Result |
| --- | --- | --- | --- |
| Local, Release (`build/release`, GCC 16.2.1, `-O3 -DNDEBUG`, Qt 6.12.0, BFD linker, `SKETCHYUP_BUILD_DESKTOP=OFF`, `SKETCHYUP_BUILD_CLI=ON`) | this record | `ctest -R '^(report\|report_output\|report_schedule\|report_cli\|report_schedule_cli\|report_mcp)$' -j2`, under `ulimit -v 8000000` | 6 of 6 passed, 18.6 s |
| Main, fast tier (`40580430398`, `6dcea44a`) | [run](https://github.com/zygote55/sketchyup/actions/runs/40580430398) | the six report suites in fast-tier shards 3, 4 and 5 (`report_schedule_cli` in 3; `report` and `report_output` in 4; `report_cli`, `report_schedule` and `report_mcp` in 5); `quantities_input_tests` on X11 1× (`Fast tier: X11 1x 1/2`, job `122459106272`) | all passed; `quantities_input_tests: all checks passed` |
| Main, nightly full tier (`40503622529`, `61b2018b`) | [run](https://github.com/zygote55/sketchyup/actions/runs/40503622529) | `Development build and checks` (job `122187330427`): 262 CTest tests, including the six report suites and `mcp` and `assistant_tool_surface` | 262 of 262 passed. **The panel is not in this run**: it merged after it |
| PR #480, full tier (`40563748036`, head `6c371a5e`, label `full-ci`) | [run](https://github.com/zygote55/sketchyup/actions/runs/40563748036) | `Development build and checks` (`122400019678`): 266 CTest tests | 266 of 266 passed |
| PR #480, native and audit | same run | `Native tests` X11 1×, X11 2×, Wayland 1×, Wayland 2× (`122422259215`, `122422259068`, `122422259026`, `122422259125`); `audit and accessibility` (`122422259086`) | all success; §4 |
| PR #480, sanitizers | same run | `Sanitized builds` (`122400019710`): 137 CTest tests, the six report suites among them | 137 of 137 passed (§5) |

The nightly's own `Sanitized builds` job (`122187330352`) also ran the six report suites and passed 137 of 137.

## 3. Scripted runs

Run on the local Release build, against the committed `tests/fixtures/reports/quantities-a.sketchyup` (SHA-256
`60be4883…9579b44d`, unchanged by every run below). Each command's exit status and output are in the scratch folder, not
committed. Commands are shown in short form. The CLI was given a fresh `HOME` and `XDG_DATA_HOME`.

| # | Command (short form) | Result | Clause |
| --- | --- | --- | --- |
| S1 | `--metrics all --group-by none --precision full --report a-default-1.csv`, then `…-2.csv` | both exit 0; `cmp` identical; identical to `quantities-a-default.csv` | V10 |
| S2 | the same request to `a-default-1.csv` again | exit 1, `FILE_EXISTS`; SHA-256 of the file unchanged | V12 |
| S3 | `--format csv --report x.json` | exit 1, `INVALID_REQUEST`, "must match the report file's extension" | V12 |
| S4 | `--group-by material --visible --precision 3 --report a-material.csv` | exit 0; identical to `quantities-a-material.csv` | V10, V3 |
| S5 | `--report a-default.json` (default request) | exit 0; identical to `quantities-a-default.json` | V10, V12 |
| S6 | `--save-schedule A-default …`, then `--list-schedules`, `--report … --schedule A-default`, `--delete-schedule A-default` | saved, listed, run and deleted; the model's SHA-256 unchanged after the save; the scheduled CSV differs from the golden only at line 5 (`#schedule=A-default` for `#ad-hoc=1`) | V11 |
| S7 | `--input quantities-b.sketchyup --schedule A-default` | exit 1, `WRONG_DOCUMENT`, naming both identities; no file written | V11 |
| S8 | `--save-schedule 'bad,name'` | exit 1, `INVALID_REQUEST`, the name rule | V11 |
| S9 | `--group-by definition`, `--definition 2`, `--definition 3`, `--group-by tag`, `--visible` on `quantities-a` | the values in §1 (V1, V2, V3) | V1, V2, V3 |
| S10 | `--report` on `drawing-sheets-v27.sketchyup` (the R090.b fixture) | exit 0; header `#sheets=2 #views=3 #items=9`. The totals of this fixture were not compared with a hand value | §11 sheet row (see gap 10) |

The `quantities-b` fixture for S7 was written by `report_benchmark --write-fixture` (12,531,874 bytes).

## 4. Native matrix

ADR 0175's Implementation split names Linux desktop, Windows desktop and macOS. `docs/SUPPORT_MATRIX.md` supports Linux
x86_64 only, so the matrix is Linux only, as R098d did. Windows and macOS were not run.

| Cell | Run and job | `quantities_input_tests` | Notes |
| --- | --- | --- | --- |
| X11 1× (`xcb`, Xvfb, DPR 1) | P `122422259215`; M `122459106272` (fast tier) | all checks passed (both runs) | |
| X11 2× (`QT_SCALE_FACTOR=2`, DPR 2) | P `122422259068` | all checks passed | |
| Wayland 1× (`test-wayland.sh`) | P `122422259026` | all checks passed | prints `qt.qpa.wayland: Failed to create grabbing popup … quantitiesTagsPopupWindow` (gap 8) |
| Wayland 2× (`SKETCHYUP_TEST_SCALE=2`) | P `122422259125` | all checks passed | same warning |
| Headless report suites | none | not in any native cell: they need no display, and they run once in the Development build job (§2) | |
| Accessibility and keyboard audit | P `122422259086` | the `quantities` group audits **Save as schedule…** and **Delete…** only; Rename, Export CSV… and Export JSON… are not audited (R101.e2, gap 7) | |
| Qt 6.8 reference compile | P `122400019906` | compile only | |
| Windows, macOS | none | not run | not in the support matrix (§4 intro) |

The 200 % text check that UX_DESIGN §19.10 names is not in any cell (gap 6).

## 5. Sanitizers and fuzzing

- **Sanitizers.** The `sanitize` preset (`SKETCHYUP_SANITIZE=ON`, address and undefined-behaviour sanitizers) ran the six
  report suites in CI at PR #480 (`122400019710`, 137 of 137) and in the nightly (`122187330352`, 137 of 137). No
  sanitizer report appears in either log. R101.c and R101.d noted that the schedule and MCP suites had not run under
  sanitizers; CI has now run them. No local sanitizer build was made.
- **Fuzzing.** No fuzz campaign exists for the report engine, the CSV writer or the schedule store. ADR 0175 does not require
  one. The fuzz harnesses under `tests/fuzz/` cover the parser, terrain and allocation code; a search of that folder and of
  `scripts/ifc-fuzz/` for the report names found none. This is gap 17.

## 6. Budgets (ADR 0175 §12)

Measured on the Release build above, on `quantities-b` (10,000 placements of the 12-face part with the 250,000-triangle
terrain: 30,257 records) unless the row says otherwise. The machine has 22 CPU threads and 30 GB of RAM, and other workers
were building and testing during several runs. Each row shows the 1-minute load average when its runs started, because
the timings depend on it. Measurements are from `tests/report_benchmark.cpp` (in-process engine timing) and from the
built `sketchyup-cli` (process wall time). Raw logs are in `/tmp/r101f-scratch`, not committed.

| §12 row | Budget | Measured (Release) | Runs and statistic | Verdict (frozen) |
| --- | --- | --- | --- | --- |
| Counts (`groupBy none`) | ≤ 2 s | 0.290 s median (0.267 to 0.334 s); 30,257 contexts | 5 runs; load 2.6 to 4.3 | **Pass** |
| + areas | ≤ 5 s | areas alone 0.315 s median (0.285 to 0.355 s). The cumulative reading is bounded by the all-tables run, 0.827 s median (0.754 to 0.931 s), which is under 5 s | 5 runs | **Pass** |
| + solid volume analysis (`inspectSolid` per context) | ≤ 30 s, async with progress | solid volumes alone 0.848 s median (0.735 to 0.928 s). The panel runs this asynchronously with progress (R101.e2) | 5 runs | **Pass** |
| Cancel returns in ≤ 100 ms; the run stops in ≤ 5 s (the cancel and stop terms of the solid-volume row) | 100 ms; 5 s | returned 0.05 to 2.80 ms after the flag was set, at four points in the all-tables pass (records 1, 3,025, 15,128 and 30,256); longest gap between records 1.81 ms. The stop and the return are the same event in the engine | 3 runs × 4 points = 12 samples; load 3.3 to 4.1 | **Pass** (cancel ≤ 2.80 ms; stop ≤ 2.80 ms) |
| Panel re-run after one edit, ≤ 100,000 faces: p95 ≤ 1 s, off the UI thread | p95 ≤ 1 s | model: 99,996 faces (24,999 records, 8,333 placements, no terrain). Each run moves one placement, then times the engine and `reportDescription` (the panel's pass on a snapshot). **Run and description p95, four series of 200 runs: 965, 1,097, 569, 800 ms.** Medians 550, 850, 502, 501 ms. Engine-only p95: 943, 995, 561, 787 ms | 4 series; load at start 3.8, 6.5, 2.8, 1.8 | **Fail** at 1,097 ms (series 2, under load). Three series met it. See §8 item 1 |
| CSV write, 100,000 rows | ≤ 10 s; output ≤ 256 MiB | 100,002 rows (33,334 synthetic count groups, the R101.b shape): 0.081 s median (0.075 to 0.113 s), 3,989,304 bytes (3.8 MiB). The 256 MiB refusal is not exercised (gap 11) | 5 runs; load 2.6 to 4.3 | **Pass** |
| Schedule re-run | the matching report budget (no extra pass) | `sketchyup-cli --report … --schedule Bench` against the ad-hoc `--metrics all --group-by tag`, five alternating pairs on `quantities-b`. Process wall time: scheduled median 2.25 s (2.13 to 2.55 s); ad-hoc median 2.30 s (2.12 to 2.46 s). Most of each is loading the 12.5 MB file. The CSVs differ only at line 5 | 5 pairs; load 2.4 to 2.6 | **Pass** (the pass is inside the report budgets; the schedule's own store work is not timed alone) |
| Engine memory: peak ≤ 512 MiB; no cross-run cache | 512 MiB | `VmHWM` (whole process, including the document) 282,824 kB median (282,200 to 284,280 kB), about 276 MiB. `VmRSS` before the runs 226,108 kB (the document alone). "No cache" is a design property (R101.a and R101.e1 notes), not something this measurement shows | 5 runs | **Pass** |

Not measured here: the quantities-b shape with its terrain (349,214 faces) under the re-run (outside the ≤ 100,000-face
row; R101.e1 measured 1,281 ms p95), and the desktop panel's own widget timing. The re-run is measured on the engine
pass that the panel calls, not on the panel (gap 5).

## 7. Gaps

Each gap is either resolved in this record or carried. A carried gap names its owner decision (§8) or its work.

1. **V6, the `degenerate` status is not built.** No body the public API can build gets `degenerate` (R101.a, point 6). Carried; owner decision 3.
2. **V7, the UI on a large model, and the 100 ms cancel in CI.** The panel's computing and cancel checks run on `quantities-a`. The 100 ms figure is measured here (§6), and no CI job asserts it. Carried.
3. **V12, the JSON file omits body IDs.** The file is the session data less the session-only fields. Carried; owner decision 2.
4. **The panel re-run budget is missed in one series.** §6 and §8 item 1. Carried.
5. **The panel's own timing was not measured.** The desktop build is not a Release build here. The re-run figures come from the engine pass the panel calls. Carried.
6. **200 % text (UX_DESIGN §19.10) is not run in any cell.** R101.e2 says the same. Carried; a UI check to add.
7. **Rename, Export CSV… and Export JSON… are not in the keyboard audit.** Their keyboard checks fail offscreen (R101.e2). Carried.
8. **Wayland popup warning.** Both Wayland cells print `qt.qpa.wayland: Failed to create grabbing popup … quantitiesTagsPopupWindow`. The test passes. Qt's message says the popup needs a transient parent and a parent window that has received input; the cause in the Tags popover is not investigated here. Carried as a finding.
9. **Two compiler warnings in the desktop build.** `src/app/quantities_panel.cpp:882` (missing field initializers `areaText` and `volumeText`) and `tests/quantities_input_tests.cpp:100` (the return value of `qWaitFor` ignored). Found in the CI logs. Carried; cheap to fix.
10. **The §11 sheet row is not built as written.** No fixture holds "one sheet, two views, three items". The existing `drawing-sheets-v27.sketchyup` gives 2 sheets, 3 views and 9 items in the metadata (S10), and sheet, view and item counts appear only in the metadata, as §2.5 requires. Partly resolved; the exact fixture is carried.
11. **The 256 MiB refusal is not exercised.** It needs about 6.7 million CSV rows. The check runs before anything is published, but no report that large was built (R101.b). Carried.
12. **One record's analysis is not interruptible.** Cancel acts between records. The longest gap measured is 1.81 ms, on `quantities-b`, whose bodies are small (R101.e1). A document with one very large body is not measured. Carried.
13. **A schedule does not pin its display units** (UX_DESIGN §19.11 item 9). Precision is pinned. Carried.
14. **Terrain names are not unique in the CSV** (R101.b). Carried.
15. **The CSV has no `materialsApply` line** (R101.b). The JSON says that counts and volumes ignore the materials filter; the CSV does not. Carried; ADR 0175 allows no other `#` line.
16. **Windows and macOS are not run.** Not in the support matrix (Linux x86_64 only), so not claimed (R098d). Stated, not a gap in a supported cell.
17. **No fuzz campaign for reports or CSV.** Carried; ADR 0175 does not require one.
18. **Sanitizers: closed.** CI ran the six report suites under the sanitize preset on both PR #480 and the nightly (§5). The gap R101.c and R101.d recorded is closed.
19. **No CHANGELOG entry for the panel.** R101.a, R101.b and R101.c added changelog entries; R101.e1 and R101.e2 did not. The acceptance precedents (R097.f, R098.d) added none, so this record adds none. Carried; owner decision 6.
20. **Budgets are not enforced in CI.** `report_benchmark` is `EXCLUDE_FROM_ALL`, and no CI job builds or runs it. The budgets above were measured once, by hand, for this record. Carried; owner decision 5.
21. **The §11 corrections are not yet approved.** R101.a corrected the chain area, the area totals, the context count, the unvalidated count and the `degenerate` row. The corrected figures are the ones checked here. Carried; owner decision 4.

### Gaps left by R101.a to R101.e2

Each gap that an implementation note recorded, and what this record does with it.

| Note | Gap as recorded | Here |
| --- | --- | --- |
| R101.a | The `degenerate` status cannot be built (point 6) | carried (gap 1; owner decision 3) |
| R101.a | V5 beyond the single edit and undo; V7; V10 to V12 | V5 and V10 to V11 resolved (tests and scripted runs); V12 partly carried (gap 3); V7 partly carried (gap 2) |
| R101.a | No fixture with sheets | partly resolved (S10 reads the metadata of `drawing-sheets-v27`); the exact fixture carried (gap 10) |
| R101.a | The desktop selection path is not run by a desktop test | resolved: `quantities_input_tests` section 8 (Selection only, the 2 m² plate, the selection export) runs in CI's native cells |
| R101.a | §12 budgets on the full application and on other platforms | engine budgets measured here; the panel's own timing and other platforms carried (gaps 5 and 16) |
| R101.b | Terrain names not unique in the CSV | carried (gap 14) |
| R101.b | No `materialsApply` line in the CSV | carried (gap 15) |
| R101.b | Desktop re-run, cancel and stop not measured | measured here (§6) |
| R101.b | Schedule re-run not measured in the desktop | measured here as the CLI re-run (§6) |
| R101.b | The 256 MiB refusal not exercised | carried (gap 11) |
| R101.c | Windows and macOS store isolation; the read-only folder check skips as root | carried (gap 16); the root skip not re-checked here |
| R101.c | Sanitizer runs of the schedule tests | resolved: CI's sanitize job (§5) |
| R101.c | Desktop artifacts not regenerated | resolved: the API-schema check passed in CI's Development build job |
| R101.c | A hand-edited request is checked only when it runs | carried; the documented behaviour, not re-checked here |
| R101.c | The desktop did not call the store path | resolved by R101.e2, which uses the store and is run in CI |
| R101.d | A 65-character name is refused by the wire schema before the store | accepted behaviour; the store's own refusal is tested |
| R101.d | 63 of the 64 MCP tool slots used | carried; the next MCP tool needs the bound raised or a tool removed |
| R101.d | The native assistant can run any registry query it is sent | carried (pre-existing; the assistant is off by decision 10) |
| R101.d | Session operations have a generic MCP description | carried (cosmetic) |
| R101.d | Not run under sanitizers or on the native display | resolved: sanitizer jobs (§5) and `native_mcp_tests` in CI's native cells |
| R101.e1 | The panel re-run budget not met (measured at 582 to 968 ms against 250 ms) | measured here against the owner's 1 s: missed in one series (gap 4; owner decision 1) |
| R101.e1 | The exported JSON carries no body IDs | carried (gap 3; owner decision 2) |
| R101.e1 | A pinned precision refused after a units change; units not pinned | carried (gap 13) |
| R101.e1 | A single record is not interrupted by cancel | carried (gap 12) |
| R101.e1 | The desktop panel not built; X11 and Wayland display tests not run; Qt 6.8 compile not run | resolved: the panel is built and tested in CI's native cells, and the Qt 6.8 reference compile succeeded (compile only) |
| R101.e1 | No sanitizer build of the desktop tests | carried: the sanitize job runs the engine and schedule suites, not `quantities_input_tests` |
| R101.e2 | Rename, Export CSV and Export JSON not in the keyboard audit | carried (gap 7) |
| R101.e2 | 200 % text not checked on screen | carried (gap 6) |
| R101.e2 | Status-bar job chip, Ctrl+C on a totals row, the two-line group rows and the material counts in the pickers | carried: UX deviations listed in R101.e2; not in this record's checks |
| R101.e2 | Offscreen only, Qt 6.12 not 6.8 | resolved: CI's native cells run X11 and Wayland at 1× and 2×, on the CI image's Qt |

## 8. Owner decisions

Each item gives the recommendation. I have not made any of these decisions.

1. **The panel re-run budget (§12).** The p95 is 1,097 ms in series 2 and 965, 569 and 800 ms in the other three, all on
   the same model. The series that missed started at a 1-minute load of 6.5, with other workers' builds and tests running; the
   process list was not recorded during that series, so the cause of the extra load is not shown. Options: (a) measure on the owner's own desktop in a normal session, and keep 1 s if that holds; (b) keep 1 s
   and add a cache of per-body solid analysis keyed by each body's revision (the ADR §14 trigger, which costs memory and
   complexity); (c) change the budget. **Recommendation: (a) first.** The background worker that the ADR's §14 trigger
   names is already in place (R101.e2), so it would not shorten the pass.
2. **V12, the JSON file's body IDs.** The session's invalid rows carry body IDs; the exported JSON file does not
   (R101.e1, decided by the parent on 2026-10-27). **Recommendation: accept**, and amend V12 to say "byte-identical to the
   session's data less the session-only body IDs".
3. **V6, the `degenerate` status.** Either a construction the solid analysis reports as `degenerate`, or drop the row.
   **Recommendation: drop the row** and record that no public-API body reaches `degenerate`.
4. **The ADR 0175 §11 corrections.** The chain area is 54 m² (not 9 m²). The area total is 40,259.2 m² (visible 40,241.2 m²).
   The context count is 120 (not 80,041). The unvalidated count is 2 (not 3). **Recommendation: approve**; the arithmetic is
   checked in R101.a's tests and in S9 above.
5. **Budgets in CI.** Whether any budget should run in CI. The CI-time directive of 2026-10-21 favours no. **Recommendation:
   keep them as measured records at each release**, and re-measure before the release that carries R101.
6. **A CHANGELOG entry for the panel.** **Recommendation: one entry for R101 at the release that carries it.**
7. **Platform scope.** The ADR's Implementation split names Windows and macOS for this record; the support matrix does not
   support them. **Recommendation: Linux only, and amend the ADR's split text** (as R098d did).

## 9. Publication

- **ADR 0175.** The status line names this record and its verdict. The §12 table has a measured column and a verdict per row.
  "Implementation notes (R101.f)" records the measurements, the freeze, the platform scope and the open items.
- **Support matrix.** The row "Quantities, reports and schedules (F07, not in the 1.3.0 declaration)" in Exchange
  boundaries. It states what is claimed and the limits. F07 is released only through the M10 gate, and until then it is
  built but not claimed.
- **User guide.** "Quantities, reports and schedules", after "Terrain and site tools".
- **Roadmap.** The R101 row's status is "Delivered, with open findings", with the open items named. PR #480 is recorded as merged.
- **CHANGELOG.** No entry (gap 19).

## 10. Commits and tests

- `6dcea44a` — `origin/main` at the start of this work. The product code this record ran at.
- `6c371a5e` — PR #480's head. The full-tier CI run `40563748036` ran here.
- The commit that adds this record and its publication is the head of PR `docs/r101f-quantities-acceptance`.

**No product code was changed and no test was added.** The verification ran the existing suites, the built
`sketchyup-cli` and `report_benchmark`, without modification.
