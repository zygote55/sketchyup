# R096.g — Terrain editing, drape and stamp: acceptance

2026-10-21. Branch `docs/r096g-acceptance`, based on `main` at `f1da21a3` (R096.a–f2, merged as
PR #396). Every check and budget in this record ran at `f1da21a3`. The local desktop and core
suites ran at `f1da21a3` with the `compareTopology` change applied.

- **Hardware frames:** measured on the AMD discrete GPU on 2026-10-21 from a `git archive` of
  `f1da21a3`. See "Budgets on the AMD GPU".
- **Native matrix:** CI ran it on PR #396 (head `5902f221`, run
  [39696132281](https://github.com/zygote55/sketchyup/actions/runs/39696132281)), with all ten jobs
  passing. This record also ran the four terrain desktop suites locally in the pinned image.
- **`compareTopology` change:** it is a separate code PR,
  [PR #401](https://github.com/zygote55/sketchyup/pull/401), measured against `f1da21a3`. See
  "Decision: `compareTopology` on one-sided changes".

[Summary](R096g-terrain-editing.json).

R096 delivers [ADR 0169](../decisions/0169-terrain-editing-drape-stamp.md) in these slices:

| Slice | What it delivers |
| --- | --- |
| R096.a | The edit core |
| R096.c | Cavity re-triangulation and contour edits |
| R096.b | Sculpt and smooth |
| R096.d | The building-pad stamp |
| R096.e | Projection and drape |
| R096.f1, f1b and f1c | The desktop preview path, Sculpt and Smooth, and the commit budget |
| R096.f2 | Edit contours, Stamp pad, Drape and the Terrain edits list |

This record checks them against:
- the roadmap's Verify line;
- the consistency checks of ADR §6;
- the verification plan and budgets of ADR §9;
- R095.f's one-tile replacement row, which this record closes.

## Verdict

**Accepted. R096 is complete.** No budget or hardware figure is pending.

- **Verify line:** passes. Contours and pad heights stay consistent (C1, C2 and C3). A local edit
  keeps distant terrain unchanged (C5). Undo and save work, and Regenerate is a no-op after any
  sequence of edits (C7).
- **ADR §6 checks:** C1–C7 each pass after every operation kind, Undo and Redo, save and reopen,
  and Regenerate. Every row of §9's verification table also passes, with two test gaps:
  - C5 is proven on terrains up to 60,000 triangles, not on the 250k fixtures;
  - the history-budget refusal is not driven by an R096 edit.
- **Budgets measured on the AMD RX 6800 XT** (three runs each, p95 from the input event to the
  presented frame):
  - every one-tile commit is within 100 ms: sculpt 55.6–74.0 ms, contour edit 90.2–92.3 ms,
    stamp pad 70.2–71.6 ms, drape 51.6–51.8 ms;
  - every one-tile preview is at most 21.9 ms.
- **Budgets measured in software:** the contour-edit preview in the core is ≤ 16.1 ms. Replaying
  1,000 entries takes 1.0–1.7 s, against 3 s. At least 525 one-tile edits stay undoable, against
  64.
- **R095.f's one-tile replacement row** (p95 ≤ 100 ms to the preview frame) is closed: 17.6–21.9 ms
  on hardware.
- **Under llvmpipe:**
  - The previews pass: sculpt on the contours terrain, contour edit, added breakline, stamp and
    drape are all within 100 ms.
  - The sculpt, contour-edit and stamp one-tile commits (p95 102–171 ms) and the sculpt preview on
    the points terrain (93–112 ms) do not pass, because llvmpipe's software frame alone takes
    42–82 ms. The drape commit passes at 81.7 ms.
  - As in R095.f, software rendering does not judge a frame budget; the AMD session does.
- **Complete frame with a preview visible:** the frame part is p95 12.2–17.2 ms on hardware,
  including a framebuffer readback that the M9 harness does not do.
  - It passes against the 33.3 ms row.
  - Against the stricter 16.7 ms row that R095.f applied, only the stamp-pad preview frame is
    marginal (16.3–17.2 ms with readback). See "Gaps and follow-ups".
- **`compareTopology`:** the one-sided adjacency skip from R096.f1c is implemented in its own PR,
  with tests showing that validation outcomes are unchanged. See the decision section.
- **Defects:** none Release-blocking or Major found. No R089.a row is added.

## The Verify line

The roadmap's Verify line reads: "Contours and pad heights remain consistent; editing a local
region preserves distant terrain and supports undo/save."

| Clause | Evidence | Result |
| --- | --- | --- |
| Contours remain consistent | C1 and C2 after every operation kind and after scripted sequences of all of them ([Checks](#the-consistency-checks-6)) | pass |
| Pad heights remain consistent | C3 on the square, L-shaped, tile-corner and fixed-width pads, after later sculpts near the pad, refused contour edits into it, Restamp and `terrain.update_edit` | pass |
| Editing a local region preserves distant terrain | C5 on the gridded, 12,000-point and 60,000-triangle terrains: untouched tiles are pointer-identical with their IDs, and elevations outside the region are bit-equal (2,000 and 1,600 points). C5 at 250k is a gap: see "Gaps and follow-ups" | pass |
| Supports undo | Each operation: Undo restores the record and tiles pointer-identical; Redo restores the edit; N Undos give the original encoding (80 lifts in `terrain_edit`) | pass |
| Supports save | Save and reopen bit for bit, including the edit list, keys and tiling; C7 after reopen; a pending deferred digest is saved as its final value | pass |

## Fixtures

All fixtures are generated in code. No survey data is used. R096.g adds no fixture.

| §9 fixture | Generated by | Used by |
| --- | --- | --- |
| R095 `terrain-contours-m.dxf` (hill, hairpin valley, boundary, pond) | `terrainContoursMetres()` | `terrain_source_edit`, `terrain_drape` |
| The two 250k terrains (points: 249,970 triangles, 256 tiles; contours: 249,298 triangles, 3,169 tiles), and the 2 % smaller ones for stamps and drapes (244,970 and 244,498 triangles) | `terrain_budget_benchmark`, `terrain_edit_benchmark` | budgets, C1 and C6 at 250k |
| Gridded spot-point terrains (1 m grid, co-circular everywhere; 40 × 40 and 120 × 120 m) | `terrain_source_edit`, `terrain_sculpt`, `terrain_stamp`, `terrain_drape` | C5, C7, tile-corner pads, grid drapes |
| L-shaped footprint on a slope (mixed cut and fill, concave corner) | `terrain_stamp_tests` | C3 |
| Footprint over a tile corner (four tiles) | `terrain_stamp_tests` (120 × 120 m grid) | C3, C6 |
| Draped path across several contours and tile seams | `terrain_drape_tests` "path" (246 mesh-edge crossings, 33 contour segments split, seven tiles) | C1, C4, C6 |
| Also: a 40-ring contour hill (12,638 triangles) and a 12,000-point terrain whose base sources reproduce the base mesh | the R096 suites | replay, C7 |

No §9 fixture is missing.

## The consistency checks (§6)

Each check must hold after every operation, after Undo and Redo, after save and reopen, and after
Regenerate. The table names the ctest that proves it for each operation kind. Every test here:
- runs in the dev and sanitize presets;
- runs in CI's development and sanitized jobs;
- passed on PR #396 (run 39696132281) and again on this record's PRs.

| Check | Sculpt and smooth | Contour edit (edit, add, remove, promote) | Stamp and Restamp | Drape (imprint, overlay) | Replay (Remove, Reset, Regenerate, settings) | Result |
| --- | --- | --- | --- | --- | --- | --- |
| **C1** current sources lie on the surface (bit-equal Z, segments are mesh edges; skipped entries left out) | `terrain_sculpt` (rings clipped into runs with split ids) | `terrain_source_edit` (every kind, a scripted sequence) | `terrain_stamp` (outline and daylight line as sources) | `terrain_drape` (each run is a breakline; crossing points at the contour's elevation) | `terrain_replay`, plus every suite's Reset and Remove | pass |
| **C2** display contours pass through source contours within 1e-9 m; no stale tiles | `terrain_sculpt` | `terrain_source_edit` (per-tile cache equal to a fresh build) | `terrain_stamp` | `terrain_drape` | `terrain_replay` (assembled per-tile contours equal the whole-terrain set) | pass |
| **C3** pad vertices at the pad elevation (bit-equal); `elevation_at` within 1e-9 m; outline edges are mesh edges; ray grades equal the slopes | sculpt and smooth over a pad leave it (`verticesProtected`) | edits into a pad refused (`inside-pad`, located), nothing changed | `terrain_stamp`: square, L-shaped, tile-corner and fixed-width pads; Restamp and `terrain.update_edit` | drapes across a pad refused; across its slopes, C3 holds | `terrain_stamp` scripted sequence with Remove and Reset | pass |
| **C4** imprint changes no elevation (10,000 points within 1e-9 m) | — | — | — | `terrain_drape` "path", "clipping", "paint", "grid" and "sliver": worst 2.8e-14 m | — | pass |
| **C5** outside the influence region, tiles pointer-identical with their IDs, vertices bit-identical | `terrain_sculpt` | `terrain_source_edit` (1,600 elevations outside the rebuilt tiles bit-equal) | `terrain_stamp` | `terrain_drape` (paint: tiles not crossed are only repainted) | `terrain_edit` (60,000-triangle grid, 2,000 points) | pass |
| **C6** seams: edge-manifold, identical copies, no crack or T-junction | every suite (`checkTerrainSeams`), plus 250k in `terrain_edit_benchmark` | the same | the same (tile-corner pad) | the same (seams crossed) | the same | pass |
| **C7** Regenerate after any sequence is a no-op (same record, IDs and digest) | `terrain_sculpt` (pinned replay digest `81f5fd22…3b688`) | `terrain_source_edit` (sequence digest pinned) | `terrain_stamp` (`04bc036f…98ffc2`) | `terrain_drape` (`11e74534…22ddcb1a`) | `terrain_replay` (in the core, through the batch, after reopen) | pass |

Each check also holds:
- after **Undo and Redo**: in every suite, with tiles pointer-identical after Undo;
- after **save and reopen**: bit for bit in every suite;
- on **a second document**: identical in every suite.

The pinned replay digests are the same in Debug and Release builds.

## The verification plan (§9)

| Verify clause | Evidence | Result |
| --- | --- | --- |
| Contours remain consistent | C1 and C2 after each kind and after scripted sequences of all of them, on the contour fixture and the hill (`terrain_source_edit`, `terrain_sculpt`, `terrain_drape`) | pass |
| Pad heights remain consistent | C3 on the L-shape and tile-corner pads, after later sculpts near the pad (protected vertices unchanged), a refused contour edit into the pad (nothing changed) and a Restamp (`terrain_stamp`) | pass |
| Local edit preserves distant terrain | C5 on the gridded, 12,000-point and 60,000-triangle terrains, with 1,600–2,000 elevations outside the region bit-equal. At 250k, `terrain_edit_benchmark` checks only C1 (over the 416 contours) and C6 after its commits. §9's C5 on the 250k fixtures, with 10,000 `elevation_at` points, is not run: a gap | pass, with a gap at 250k |
| Undo | Undo restores the record and tiles pointer-identical, and the encoding bit for bit; Redo restores the edit; after N edits, N Undos give the original encoding (`terrain_edit`: 80 lifts) | pass |
| Save | Save and reopen bit for bit, with the edit list, keys and tiling (`terrain_edit` format round trip, every suite); C7 after reopen; the deferred digest is saved as its final value (`terrain_sculpt` "deferred digest") | pass |
| Reversible previews | A preview leaves the revision, history, dirty state and encoding unchanged, and Escape restores the frame (`terrain_edit_input`). Committed tiles equal the previewed candidate bit for bit: for a sculpt stroke, a contour drag, an added contour, a promotion, a stamp and a drape, from real input (`terrain_edit_input`) and in the core (every suite) | pass |
| Seams | C6 after every operation, on the tile-corner fixture and the 250k contours terrain (79-triangle tiles) | pass |
| Imprint keeps shape | C4 at 10,000 random points, worst 2.8e-14 m | pass |
| Determinism | The same operation on two documents gives identical records and IDs; replay digests are pinned per fixture | pass |
| Limits and refusals | Triangle limit (stamps, imprints, contour edits: `triangle-budget`, `LIMIT_EXCEEDED`); locked terrain; outside the outline; into a pad; crossing contour edit (located, bounded report); full edit list (4,096 entries or 8 MiB); hostile regions, combs and zigzags. Each changes nothing. The history-budget refusal (`LIMIT_EXCEEDED` for one edit over the 512 MiB terrain history) is not tested for an R096 kind: the largest single R096 edit measured, a whole-terrain region paint, is charged 137–148 MB | pass, with a gap (history-budget refusal) |
| Cavity adapter | `terrain_triangulation_tests` (the cavity API and 3,000 generated cavities through the `terrain-cavity` harness); `parser_fuzz_terrain_cavity` 271,021 inputs in 300 s with no finding | pass |
| Edit-list decoder | `parser_fault_corpus_tests`; `parser_fuzz_terrain_edits` with all production kinds in its seeds, 474,260 inputs in 301 s (R096.e, 2026-10-20) with no finding | pass |

## Budgets

ADR §9's budgets are adopted by decision 10, at 250,000 triangles. They are measured three ways.

| Harness | What it times | Conditions |
| --- | --- | --- |
| `terrain_edit_benchmark` | The core, the command and the display caches, without drawing | Release; development host |
| `terrain_budget_benchmark --sculpt-frames 64` and `--edit-frames 64` | From the input event to the presented frame (`paintGL`, `glFinish` and a framebuffer readback) | The pinned image's Xvfb at 1920 × 1080 with llvmpipe, 4 CPUs |
| The same frame harnesses on the AMD GPU | The same | The R082.ii and R095.f controls (below) |

The llvmpipe figures are the ADR's slice measurements, from the R096.f1, f1b, f1c and f2 notes.
Each is the best of three runs, with ranges, at or just before `f1da21a3`. This record's base runs
at `f1da21a3` (in the `compareTopology` measurement below) agree with them.

| Operation (250k triangles) | Budget | llvmpipe (pinned image) | AMD RX 6800 XT, 3 runs | Result |
| --- | --- | --- | --- | --- |
| One-tile replacement: input to preview frame (R095.f row), sculpt | p95 ≤ 100 ms | points 93–112 ms, contours 66–67 ms (the software frame alone 42–82 ms) | points 21.5–21.9 ms, contours 17.6–18.8 ms (with and without the Terrain section) | **pass** (hardware) |
| Sculpt touching nine tiles: preview / commit | measured and reported | preview 138–169 ms; commit 341–433 ms | preview 39.2–63.3 ms; commit 236.6–406.0 ms | reported |
| One-tile edit: release to committed frame, sculpt | p95 ≤ 100 ms | 106–124 ms (CPU part 51–64 ms) | points 55.6–57.0 ms, contours 68.1–74.0 ms | **pass** (hardware) |
| The same: contour edit | p95 ≤ 100 ms | 114–171 ms | 90.2–92.3 ms | **pass** (hardware; 8–10 ms of headroom) |
| The same: stamp pad | p95 ≤ 100 ms | 102.2 ms | 70.2–71.6 ms | **pass** (hardware) |
| The same: drape | p95 ≤ 100 ms | 81.7 ms | 51.6–51.8 ms | **pass** |
| Contour-edit cavity of ≤ 2,048 triangles: preview | p95 ≤ 100 ms | core ≤ 16.1 ms (R096.c); input to frame 53–84 ms; added breakline 57 ms | 15.6–16.6 ms; added breakline 15.0–15.1 ms | **pass** |
| Stamp of a 64-vertex footprint: preview | measured and reported | core 7.9–9.1 ms (R096.d); a one-tile pad to frame 60.6 ms | a one-tile pad to frame 20.4–21.2 ms | reported |
| Drape preview, one tile | (R095.f row, reported) | 49.7 ms | 16.1 ms | pass |
| Regenerate, Terrain settings, update or remove an entry, replaying 1,000 entries | ≤ 3 s, with progress and cancel | 1,000 lifts 1.0–1.1 s, 1,000 sculpts 1.35–1.66 s; with fewer entries, 15 and 27 pads 0.7–0.9 s and 90–100 drapes 0.64–0.82 s; cancel within 52 ms; the Edits list's Remove of 256 changes 1.69 s to the frame, with progress, and its cancel within 22 ms | Remove of 256 changes 1.09 s to the frame; cancel 8.9–10.0 ms | **pass** |
| Undo depth | ≥ 64 one-tile edits within the 512 MiB terrain history | at `f1da21a3`: about 520 (points) and 585 (contours), from one one-tile edit's charge (1.02 and 0.91 MB) after the record's one-time charge of about 3 MB; a whole-terrain region paint still leaves room for over 360 | — | **pass** |
| Added resident memory with the edit mesh cache | within the ≤ 200 MB row | the edit mesh is 22.1 and 30.3 MB at `f1da21a3`, on top of R095.f's 99.8–108.6 MiB (104.6–113.9 MB): about 127–144 MB. History is not counted here: a whole-terrain region paint keeps 140–157 MB more resident at `f1da21a3` (243 and 151 MB in R096.e) while it is undoable, charged to the 512 MiB terrain history | — | **pass** for the terrain and its edit mesh; large edits' history disclosed (see the follow-ups) |
| Complete frame with the terrain and a preview visible | within the M9 frame budget | not judged in software | the frame part with a preview visible is 12.2–17.2 ms (readback included); the terrain without a candidate is 1.4–5.4 ms, or 14.0–14.5 ms with the Edit contours overlay | **pass** at the 33.3 ms row; marginal at the 16.7 ms row for the stamp preview (see the follow-ups) |

**The one-tile commits under llvmpipe.**
- R096.f1 measured 187–216 ms. R096.f1b brought the CPU part (command, refresh and cache rebuild)
  down to 51–64 ms, and R096.f1c removed the Terrain section's rescan.
- What remains is llvmpipe's own frame: 42–82 ms with no candidate drawn.
- On the AMD GPU that frame is 1.4–5.4 ms, and the end-to-end p95 is well within 100 ms. The
  commit's cost there is the CPU part.

### Budgets on the AMD GPU (2026-10-21)

- **Machine:** `sketchyup-gpu`, the guest used by R082.ii and R095.f.
  - CachyOS, kernel `7.2.10-1-cachyos` (linux-cachyos 7.2.10-1), 12 vCPUs, 22 GB RAM.
  - AMD Radeon RX 6800 XT (radeonsi, navi21, ACO, DRM 3.64), 16 GB VRAM.
  - Mesa `mesa-git 26.3.0_devel.231096.74b69717c52-1` (OpenGL 4.6 Core, Mesa 26.3.0-devel
    git-74b69717c5).
  - Qt 6.12.0 (`qt6-base 6.12.0-3`), GCC 16.2.1 (`16.2.1+r23+gd564253eb6c8-1`).
  - XFCE on X11. The display is DP-1, 3840 × 2160 at 60 Hz. Every run's JSON reports the radeonsi
    renderer, platform `xcb` and a 1920 × 1080 framebuffer.
  - Guest load average (1 min) was 1.4–4.6. The 4.6 was before the first run, just after the
    build. Hypervisor load was not measured.
- **Build:**
  - Source: a `git archive` of `f1da21a33f8662251e24c1ff62983aa736cbf5e1`.
  - Release, `-DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF`, target `terrain_budget_benchmark` only.
  - It used CMake and Ninja copied from an earlier session's tools, in one `systemd-run --user`
    unit (`MemoryMax=12G MemorySwapMax=0 CPUQuota=800% TasksMax=256 RuntimeMaxSec=3600`, `-j8`).
  - It took 2 min 14 s with a peak of 4.9 GB.
  - Binary SHA-256:
    `d0abe91a8379fb0d634b5f5f28d0ddeaafbcb65311fe06b1e75972ace172b908`.
- **Controls:** R082.ii's AMD frame controls, as R095.f used them, through R095.f's `run-case.sh`
  (only the session path and unit name changed).
  - One transient unit per run: `MemoryMax=4G MemorySwapMax=0 LimitCORE=0 CPUQuota=100%
    TasksMax=128 RuntimeMaxSec=900s`, pinned with `taskset -c 11`.
  - Environment: `QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME=generic QT_SCALE_FACTOR=1`, one
    OMP, BLAS and LP thread, and private TMP and XDG directories.
  - DISPLAY, XAUTHORITY, XDG_RUNTIME_DIR and DBUS came from the XFCE session.
    `LIBGL_ALWAYS_SOFTWARE` was not set.
  - The recorded cgroup limits match these settings. `memory.swap.peak` was 0 in every run, and
    `memory.peak` was at most 1.3 GB.
  - The UI thread and the preview worker share the one CPU.
- **Samples:** the ADR notes' strokes and samples.
  - 64 one-tile samples on the largest tiles, and 16 nine-tile strokes.
  - Samples whose touched tiles hold more than 2,048 triangles are left out: 3 sculpt strokes on
    points, 1 stamp and 2 drapes. No sample was refused.
  - The edit modes have 192 contour-drag steps and 64 commits, 63 added-breakline previews,
    189 stamp steps with 63 commits, and 62 drapes.
- **Runs:** three interleaved rounds of `--sculpt-frames 64`, `--sculpt-frames 64
  --terrain-section` and `--edit-frames 64`. All nine runs exited 0, at 46–48 s each.

Results: p95 / p50 / max in ms, from the input event to the presented frame. "+section" means the
terrain is selected and its Entity Info section is refreshed, as the window does.

| Operation | Run 1 | Run 2 | Run 3 | ≤ 100 ms (p95) |
| --- | ---: | ---: | ---: | --- |
| Sculpt preview, one tile (points) | 21.5 / 16.7 / 37.6 | 21.5 / 17.3 / 38.9 | 21.6 / 17.1 / 37.8 | pass |
| Sculpt preview, one tile (contours) | 17.6 / 13.2 / 20.5 | 18.0 / 13.5 / 20.4 | 18.1 / 13.6 / 20.9 | pass |
| Sculpt preview, one tile (points +section) | 21.6 / 16.8 / 37.5 | 21.8 / 17.3 / 38.6 | 21.9 / 17.3 / 38.6 | pass |
| Sculpt preview, one tile (contours +section) | 17.9 / 13.0 / 20.7 | 18.8 / 14.1 / 20.6 | 18.1 / 13.4 / 21.0 | pass |
| Sculpt commit, one tile (points) | 55.6 / 49.6 / 85.2 | 56.4 / 50.2 / 84.8 | 56.0 / 50.2 / 83.9 | pass |
| Sculpt commit, one tile (contours) | 69.6 / 55.7 / 71.4 | 68.8 / 56.5 / 70.9 | 68.1 / 55.9 / 71.0 | pass |
| Sculpt commit, one tile (points +section) | 56.1 / 50.9 / 84.1 | 56.5 / 51.7 / 85.7 | 57.0 / 51.4 / 85.8 | pass |
| Sculpt commit, one tile (contours +section) | 71.5 / 59.4 / 79.5 | 74.0 / 61.4 / 75.9 | 73.7 / 60.4 / 79.1 | pass |
| Contour edit preview (contours) | 16.1 / 13.4 / 22.5 | 16.6 / 13.1 / 18.9 | 15.6 / 12.4 / 20.5 | pass |
| Contour edit commit, one tile (contours) | 90.2 / 79.7 / 92.1 | 92.3 / 81.0 / 96.8 | 90.5 / 80.3 / 97.7 | pass |
| Add breakline preview (points) | 15.0 / 13.7 / 18.3 | 15.0 / 14.0 / 19.3 | 15.1 / 13.8 / 18.5 | pass |
| Stamp pad preview, one tile (points) | 20.4 / 16.7 / 28.0 | 21.2 / 17.0 / 28.5 | 21.2 / 17.1 / 28.0 | pass |
| Stamp pad commit, one tile (points) | 71.6 / 44.8 / 76.0 | 70.2 / 44.8 / 75.6 | 70.8 / 44.8 / 74.9 | pass |
| Drape preview, one tile (points) | 16.1 / 14.9 / 19.3 | 16.1 / 15.0 / 19.8 | 16.1 / 15.1 / 19.9 | pass |
| Drape commit, one tile (points) | 51.8 / 49.7 / 53.7 | 51.6 / 49.9 / 54.2 | 51.8 / 50.0 / 54.5 | pass |
| Sculpt preview, nine tiles (points) | 61.9 / 57.2 / 66.2 | 62.7 / 57.9 / 66.6 | 62.7 / 57.9 / 66.2 | reported |
| Sculpt preview, nine tiles (contours) | 39.2 / 34.9 / 40.2 | 41.1 / 35.8 / 41.6 | 40.5 / 36.2 / 42.6 | reported |
| Sculpt preview, nine tiles (points +section) | 61.8 / 56.9 / 65.7 | 62.9 / 57.9 / 66.7 | 63.3 / 58.1 / 66.3 | reported |
| Sculpt preview, nine tiles (contours +section) | 39.2 / 34.9 / 40.1 | 40.6 / 35.9 / 41.6 | 39.9 / 35.3 / 41.6 | reported |
| Sculpt commit, nine tiles (points) | 313.0 / 287.9 / 313.0 | 318.4 / 291.8 / 318.4 | 320.7 / 287.7 / 320.7 | reported |
| Sculpt commit, nine tiles (contours) | 236.6 / 205.6 / 236.6 | 243.0 / 210.6 / 243.0 | 251.8 / 209.7 / 251.8 | reported |
| Sculpt commit, nine tiles (points +section) | 317.0 / 286.7 / 317.0 | 406.0 / 302.9 / 406.0 | 320.7 / 294.8 / 320.7 | reported |
| Sculpt commit, nine tiles (contours +section) | 240.7 / 210.1 / 240.7 | 249.8 / 215.2 / 249.8 | 248.3 / 216.1 / 248.3 | reported |

The edit mode's replay case (the Edits list's Remove of the first of 256 changes) took
1,085.8–1,093.2 ms to the frame, with 56–59 progress values. A cancel took effect within 8.9–10.0 ms
and changed nothing.

Where a one-tile commit's time goes on hardware (p95 ms, run 1; runs 2 and 3 are within about 2 ms):

| One-tile commit | Total | Release event (command + refresh) | Frame | of which: cache rebuild | Frame without a candidate |
| --- | ---: | --- | ---: | ---: | ---: |
| Sculpt, points | 55.6 | 31.3 (20.2 + 10.7) | 26.2 | 23.2 | 1.4 |
| Sculpt, contours | 69.6 | 30.7 (24.5 + 5.6) | 37.0 | 26.3 | 5.4 |
| Sculpt, contours +section | 71.5 | 37.0 (23.8 + 12.9) | 33.8 | 23.0 | 4.7 |
| Contour edit, contours | 90.2 | 47.2 | 41.8 | 28.1 | 14.5 (the tool's source-line overlay) |
| Stamp pad, points | 71.6 | 33.4 | 36.3 | 32.7 | — |
| Drape, points | 51.8 | 23.3 | 28.2 | 24.9 | — |

The preview frames, p95 over the three runs (ms, readback included):

| Frame | Sculpt (points) | Sculpt (contours) | Contour edit | Add breakline | Stamp pad | Drape |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Preview frame | 13.3–13.6 | 12.2–12.8 | 13.7–14.5 | 13.4–13.6 | 16.3–17.2 | 14.1–14.2 |

- **The GPU part is small on hardware.** A frame without a candidate is 1.4–5.4 ms, against
  42–82 ms under llvmpipe. What remains is CPU work: the command, the refresh and the cache rebuild.
- **The contour-edit commit has the least headroom, 8–10 ms.** About 75 ms of it is CPU: the event
  takes 47–48 ms and the rebuild 28 ms. The tool's source-line overlay adds about 14 ms to every
  frame.
- **Nine-tile commits** (reported only, 237–406 ms) are dominated by the command (103.6–159.1 ms) and
  the rebuild (102.4–144.5 ms, or 226.6 ms in the outlier run). The command's cost is the one the
  R096.f1c notes record. The rebuild re-tessellates up to 13 rebuilt tiles; since R096.f1b, seam-ring
  tiles only have their normals patched.
  - With 16 strokes, p95 is the maximum, so one slow stroke sets it. The 406.0 ms in
    `sculpt-section-2` (points) is one such outlier; the other runs of that case are
    317.0–320.7 ms.
- **The complete-frame row.**
  - The BUILD_PLAN frame rows are 16.7 ms (100k triangles) and 33.3 ms (1M triangles). R095.f
    applied the stricter 16.7 ms to a 250k terrain.
  - The frame part with a preview visible is 12.2–17.2 ms. It includes a framebuffer readback
    (`grabFramebuffer`) that the M9 harness does not do.
  - Every frame passes the 33.3 ms row. All but the stamp-pad preview frame (16.3–17.2 ms) pass
    16.7 ms.
  - This record treats the row as a pass. The stamp frame without the readback is a follow-up.
- **Evidence:**
  - On the AMD box, in `sketchyup-gpu:/home/keith/sketchyup-verification/r096g-amd-terrain-edit-20261021/evidence/`:
    - the JSON, stderr, cgroup limits and exit code of each run;
    - `environment.txt` (with glxinfo), `build.log`, `build.sh`, `run-case.sh`, `run-all.sh`,
      `binaries.sha256` and `source-revision.txt`.
  - The source and the build tree are kept beside the evidence.

## Decision: `compareTopology` on one-sided changes

**Background.** R096.f1c found where the time of a nine-tile command goes. `Document::applyEdit`
runs twice per desktop batch, and about half of each run is `compareTopology`.
- A replaced terrain tile is a one-sided change: the old tile is erased and the rebuilt tile is
  created.
- For such a change, the two adjacency maps `compareTopology` builds cannot report a modified edge,
  because no edge ID is on both sides.
- Building them also validated both sides (`Topology::adjacency` calls `Topology::validate`). That
  is the only effect the maps have on the outcome.

**Change** (`src/geometry/topology.cpp`, [PR #401](https://github.com/zygote55/sketchyup/pull/401)).
- When no edge ID is on both sides, `compareTopology` calls `Topology::validate` on the before side
  and then on the after side, in the order the adjacency calls ran.
- It does not build the maps.
- Every other comparison is unchanged.

**Why it is safe.**
- `Topology::adjacency` throws only through its `validate` call. After validation, every face-loop
  edge has a record, so its own lookups cannot fail.
- The skipped path therefore runs the same checks and throws the same messages, in the same order.
- The modified-edge list is empty either way, because the loop that fills it visits only edges
  both sides hold.
- No validation is removed. The R096.f1c option of skipping validation too, on the ground that the
  freeze already validated the new side and the old side is stored, would rely on an invariant held
  by the caller. It was not taken, so `compareTopology` keeps its own contract for every caller,
  including `amendLast`.

**Tests** (`topology_tests`, new "one-sided comparisons"). On a 3 × 3-cell tile and a rebuilt copy
with new IDs throughout:
- a created tile, an erased tile and a disjoint replacement, with and without lineage inference,
  each give the full path's modified edges (none) and deterministic changes;
- every defect validation catches is refused with the full path's exact message, on either side of
  a created, erased or disjoint comparison. The defects are a missing edge record, a wrong wire
  flag, a zero allocator, an allocator below an ID, reversed endpoints, an unknown endpoint and a
  degenerate edge;
- when both sides are defective, the before side's message comes first, as before;
- shared edges still go through the adjacency, so a moved vertex reports its modified edges.

The terrain suites (`terrain_edit`, `terrain_replay`, `terrain_source_edit`, `terrain_sculpt`,
`terrain_stamp`, `terrain_drape`) pass unchanged, with their pinned digests.

**Measured.** The pinned image, Release, at `f1da21a3` (base) and with the change (new), with
interleaved runs on the development host (4 CPUs, load average 2.3–3.2).
- `terrain_edit_benchmark` (one `applyEdit` per commit): three runs each.
- `terrain_budget_benchmark --sculpt-frames 64` under llvmpipe (the desktop batch: two `applyEdit`
  runs per commit): two runs each.

The table gives p95 in ms, as a range over the runs.

| Commit | Points: base | Points: new | Contours: base | Contours: new |
| --- | ---: | ---: | ---: | ---: |
| Nine tiles, core (`terrain_edit_benchmark`, 9,484 and 6,246 touched triangles) | 70.7–71.7 | **52.0–54.6** | 51.6–56.5 | **40.4–43.7** |
| One tile, core | 11.1–11.7 | 9.2–9.8 | 14.4–14.9 | 13.0–13.8 |
| Nine tiles, desktop command (`terrain.sculpt` through the batch) | 232.8–286.6 | **174.4–175.2** | 163.6–186.2 | **129.6–133.2** |
| Nine tiles, CPU part (command, refresh and rebuild) | 377.4–449.3 | 304.7–310.2 | 271.6–305.3 | 233.5–240.4 |
| Nine tiles, input to frame | 432.0–512.6 | 358.1–371.6 | 348.9–402.4 | 312.7–326.2 |
| One tile, desktop command | 29.2–31.2 | 24.8–28.4 | 36.1–58.3 | 32.0–32.4 |
| One tile, input to frame (llvmpipe) | 123.6–126.2 | 116.3–121.6 | 113.4–182.6 | 102.1–112.5 |

- **Nine tiles:** each `applyEdit` run in the core is about 11–19 ms faster.
  - The desktop command, which runs `applyEdit` twice, drops by 34 ms (contours) and 58 ms
    (points) when comparing the best runs, or 21–25 %.
  - The points figure is more than twice the core saving. The desktop runs are noisy under
    llvmpipe, and the batch's staged run compares more tiles. The R096.f1c estimate was 45–65 ms;
    contours came in below it.
  - The AMD session's nine-tile commits (237–406 ms, measured at `f1da21a3`, without this change)
    spend 103.6–159.1 ms in the command, so they should fall by a similar amount. They were not
    re-measured on hardware.
- **One tile:** the change saves 1–5 ms. A one-tile commit replaces one or two tiles, so there is
  less adjacency to skip.
- **Outcome:** the change is implemented. The one-tile budgets above, measured without it, are
  unaffected or slightly better.

## Native matrix

§9 asks for `terrain_edit_input_tests` on X11 and Wayland at 1× and 2×, with accessibility,
keyboard and text-size extensions.

- **In CI:** `native.yml` runs the four terrain desktop suites on X11 and Wayland at 1× and 2×:
  - `terrain_import_dialog_tests`, `terrain_desktop_tests` and `terrain_site_desktop_tests`
    (R095.f);
  - `terrain_edit_input_tests` (R096.f1), also at fractional 1.5 on both platforms and at 200 %
    text.
- **CI result:** all ten jobs passed on PR #396 at `5902f221` (run 39696132281).

**Local run (this record, 2026-10-21).**
- **Where:** the pinned image `sketchyup-ci-pinned:r094a`, dev preset (Debug, `-g0`), 4 CPUs and
  10 GiB.
- **Code:** `f1da21a3` with the `compareTopology` change.
- **Lines:** exactly as `native.yml` runs them:
  - X11 through `xvfb-run` and `scripts/test-deadline.sh`, with deadlines of 90 s, or 120 s for
    `terrain_edit_input_tests`;
  - Wayland through `scripts/test-wayland.sh`, with `SKETCHYUP_TEST_SCALE`.

| Suite | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
| --- | --- | --- | --- | --- |
| `terrain_import_dialog_tests` | pass (5 s) | pass (4 s) | pass (2 s) | pass (6 s) |
| `terrain_desktop_tests` | pass (8 s) | pass (10 s) | pass (6 s) | pass (14 s) |
| `terrain_site_desktop_tests` | pass (7 s) | pass (9 s) | pass (5 s) | pass (8 s) |
| `terrain_edit_input_tests` | pass (65 s) | pass (60 s) | pass (62 s) | pass (66 s) |

- **Wayland log message:** on Wayland, Qt logs "Failed to create grabbing popup" for the viewport
  context menu. This is the synthetic-input limit that R095.f found, and the suites pass with it.
- **Core suites:** with the same build, `topology`, `terrain_edit`, `terrain_replay`,
  `terrain_source_edit`, `terrain_sculpt`, `terrain_stamp` and `terrain_drape` pass. This was a
  ctest `-j2` run under `ulimit -v 8000000`, taking 195 s in total.

**Accessibility and keyboard reachability.**
- `terrain_edit_input_tests` checks:
  - the accessible names of the menu, rail slot, strip and Edits list;
  - each row's accessible sentence;
  - the strip's Tab order and F6;
  - the keyboard-only paths: `[x, y]`, `@E, N`, typed regions, handle focus with Tab and Shift+Tab,
    and Delete and Shift+Delete.
- `narrow_layout_input_tests` checks the full rail at a 1000 px window and lists the Terrain group.
- The "audit and accessibility" job passed on PR #396.

## Sanitizers and fuzzing

- **Sanitizers:** the sanitize preset builds and runs the six R096 core suites with ASan and UBSan
  in CI's "Sanitized builds" job: `terrain_edit`, `terrain_replay`, `terrain_source_edit`,
  `terrain_sculpt`, `terrain_stamp` and `terrain_drape`. That job passed on PR #396. Each slice also
  ran them locally under the sanitize preset.
- **Fuzzing:** libFuzzer, clang 23 with ASan and UBSan, the documented options. Each slice ran
  these campaigns on 2026-10-20, all with no finding.

| Target | Slice | Inputs | Seconds | Coverage | Peak RSS |
| --- | --- | ---: | ---: | ---: | ---: |
| `parser_fuzz_terrain_edits` | R096.a | 45,407 | 180 | 522 | 581 MB |
| `parser_fuzz_terrain_cavity` | R096.c | 271,021 | 300 | 6,131 | 508 MB |
| `parser_fuzz_terrain_edits` (source edits) | R096.c | 196,342 | 180 | 871 | 572 MB |
| `parser_fuzz_terrain_edits` (sculpts) | R096.b | 349,176 | 301 | 1,179 | 587 MB |
| `parser_fuzz_terrain_edits` (pads) | R096.d | 259,620 | 301 | 1,679 | 622 MB |
| `parser_fuzz_terrain_edits` (drapes) | R096.e | 474,260 | 301 | 2,352 | 616 MB |

R096.f adds no parser or decoder. R096.g did not repeat the campaigns: the owner cut testing time,
and no code under fuzz changed after them. A finite campaign measures coverage; it does not prove
every input safe.

## Gaps and follow-ups

None blocks acceptance. Each is recorded from the ADR 0169 notes or from this record's AMD session.

| Item | Kind | Status |
| --- | --- | --- |
| C5 at 250k with 10,000 `elevation_at` points (§9) | Gap (test) | C5 is proven on terrains up to 60,000 triangles with 1,600–2,000 points. At 250k, the benchmark checks C1 and C6 only. Add the 250k C5 check to `terrain_edit_benchmark` |
| History-budget refusal for an R096 edit (§9 limits row) | Gap (test) | The terrain history refuses one edit over 512 MiB (`LIMIT_EXCEEDED`, D-043). No R096 test drives it: the largest single R096 edit measured is charged 137–148 MB |
| Resident memory held by a large edit's history | Follow-up (disclosure) | A whole-terrain region paint keeps 140–157 MB resident while it is undoable (at `f1da21a3`), so the process can sit above the 200 MB added-memory row. It is charged to the 512 MiB terrain history, which bounds it |
| Contour-edit commit headroom on hardware: 90.2–92.3 ms against 100 ms | Follow-up (performance) | The levers are the tool's source-line overlay, about 14 ms on every frame (draw it from a cached GPU batch instead of per frame), and the commit's cache rebuild of 28 ms. Re-measure on the AMD GPU after any change to the contour tool or the display path |
| Nine-tile sculpt commit outlier: 406.0 ms in one run (`sculpt-section-2`, points) against 313–321 ms in the others | Follow-up (reported row, no budget) | With 16 strokes, p95 is the maximum, so one slow stroke sets it. The nine-tile costs are the command (103.6–159.1 ms, two `applyEdit` runs) and the rebuild (102.4–144.5 ms, re-tessellating up to 13 rebuilt tiles). Use more strokes if the row gets a budget |
| Stamp-pad preview frame 16.3–17.2 ms with readback against the stricter 16.7 ms row | Follow-up (measurement) | Passes the 33.3 ms row. Measure the frame without the readback, as the M9 harness does, before any 16.7 ms claim |
| The duplicated edit validation of a batch (staged, then published) | Declined (R096.f1c) | Removing either run would weaken the validation contract |
| Skipping validation as well as the adjacency for one-sided changes | Declined (this record) | It would depend on the caller's invariant (the freeze validated the new side, the old side is stored) |
| Per-tile contour GPU batches; a per-tile digest tree | Not needed | R096.f1b's per-row contour batches and deferred digest met the budget; the digest's definition is unchanged |
| Large point sets: an entry stores the whole new geometry (about 24 bytes a point) | Logged follow-up (R096.c) | Store per-point changes instead of the whole set |
| Assistant's `terrain.describe` clause about `edits` (decision 9) | Deferred (R096.a–c) | The assistant sees edits through `terrain.describe` results; the description's clause waits for the first assistant-facing change, to keep the pinned tool-surface baseline. MCP exposes every R096 command |
| Sparse contour meshes look faceted after sculpting (§11) | Known limit | Sculpt moves only existing vertices; Add Detail is out of scope (§10) |
| Edits list deviations from the mockup | Recorded (R096.f2 part 3) | A drape row's title names its first material or line count, and a skipped row names the conflict's kind rather than the other change |
| Preview surfaces draw the tile's colour without texture or translucency | Recorded (R096.f1) | — |

## CTest

| Check | Where | Result |
| --- | --- | --- |
| CI on PR #396 (`5902f221`): development build and checks, sanitized builds, native tests (X11 and Wayland, 1× and 2×, fractional, audit), Arch package, Blender | GitHub Actions run 39696132281 | all pass |
| `topology`, `terrain_edit`, `terrain_replay`, `terrain_source_edit`, `terrain_sculpt`, `terrain_stamp`, `terrain_drape` with the `compareTopology` change (`ctest -j2`, `ulimit -v 8000000`) | pinned image, dev preset | **7 of 7 pass** (195 s) |
| The four terrain desktop suites, X11 and Wayland at 1× and 2× | pinned image, dev preset | **16 of 16 pass** |

## Commands

Build and A/B measurement in the pinned image (`sketchyup-ci-pinned:r094a`; the build tree is
mounted at `build`):

```sh
ulimit -c 0
systemd-run --user --scope -q -p MemoryMax=10G -p MemorySwapMax=0 \
  docker run --rm -u 1000:1000 -e HOME=/tmp --cpus=4 --memory=10g --memory-swap=10g \
  -v "$PWD":/w -v "$BUILD":/w/build -w /w sketchyup-ci-pinned:r094a bash -c \
  'cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF &&
   cmake --build build/bench -j4 --target terrain_budget_benchmark terrain_edit_benchmark'
# base (f1da21a3) and new binaries, interleaved:
build/$v/terrain_edit_benchmark --edits 1 --replay 0 --cancel 0 --source-edits 0 --sculpts 100 --sculpt-replay 0
xvfb-run -a -s "-screen 0 2560x1440x24" env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
  QT_QPA_PLATFORMTHEME=generic build/$v/terrain_budget_benchmark --sculpt-frames 64
```

Desktop suites in the pinned image (dev preset), as `native.yml` runs them:

```sh
cmake --preset dev -DCMAKE_CXX_FLAGS_DEBUG=-g0 -DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF
cmake --build --preset dev -j4 --target terrain_edit_input_tests terrain_desktop_tests \
  terrain_site_desktop_tests terrain_import_dialog_tests
xvfb-run -a env QT_QPA_PLATFORM=xcb [QT_SCALE_FACTOR=2] LIBGL_ALWAYS_SOFTWARE=1 \
  scripts/test-deadline.sh 120 build/dev/terrain_edit_input_tests
[SKETCHYUP_TEST_SCALE=2] SKETCHYUP_TEST_DEADLINE=120 scripts/test-wayland.sh build/dev/terrain_edit_input_tests
```

On the AMD GPU (over SSH from the session directory, with `R` set to
`~/sketchyup-verification/r096g-amd-terrain-edit-20261021`; `build.sh`, `run-case.sh` and
`run-all.sh` are kept in `$R/evidence`):

```sh
systemd-run --user --wait --pipe --collect --unit=r096g-build -p MemoryMax=12G -p MemorySwapMax=0 \
  -p CPUQuota=800% -p TasksMax=256 -p RuntimeMaxSec=3600 $R/evidence/build.sh
#   build.sh: cmake -S $R/src -B $R/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF
#             cmake --build $R/build -j8 --target terrain_budget_benchmark
# run-all.sh: three interleaved rounds; each run-case.sh call is one capped unit
for i in 1 2 3; do
  run-case.sh sculpt-$i          $R/build/terrain_budget_benchmark --sculpt-frames 64
  run-case.sh sculpt-section-$i  $R/build/terrain_budget_benchmark --sculpt-frames 64 --terrain-section
  run-case.sh edit-$i            $R/build/terrain_budget_benchmark --edit-frames 64
done
```

## Caveats

- **Software figures:** the llvmpipe and core figures come from one shared development machine
  (load average 2–7 from other sessions), in a container with software rendering. Frame budgets are
  judged on the AMD session only.
- **Hardware run conditions:** the AMD runs use one CPU per process (`CPUQuota=100%`), so the
  preview worker and the UI thread share it. A desktop with more cores would be no slower.
- **Not measured on hardware:** the Wayland and Intel configurations, which
  [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) waives.
- **Digests:** the replay digests are pinned for this toolchain. A compiler or CDT change that
  alters a mesh must update the generator identity.
