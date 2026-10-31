# R089.a — Open-defect triage list

2026-10-10. Source revision `8534bb67bd1f003c140b6f5f7df6d6385fbc5dc7` (`main`).
**Refreshed 2026-10-11.** Source revision `d4dcac19a00773cb57ee96698b194ab3bceb2333`
(`main` after #298). The refresh re-read the records merged since
([R082.gg](R082gg-placement-limits.md), [R082.w v2](R082w-memory-budget-v2.md),
[R082.hh](R082hh-far-inference.md), [R083.e](R083e-parser-fuzzing.md),
[R083.g](R083g-unexpected-modal-guard.md), [R084.q](R084q-visible-focus.md),
[R084.r](R084r-display-precision.md), [R084.s](R084s-keyboard-panel-tabs.md),
[R085.p](R085p-keyboard-layouts.md), [R085.q](R085q-keypad-decimal.md),
[R086.by](R086by-corpus-v2.md), [R086.bz](R086bz-remote-evaluation-v2.md) and
[R088.e](R088e-viewport-accessibility-limits.md)). D-003 was reclassified, D-002
was closed and D-021–D-023 were added; see the table and the counts.
**Refreshed 2026-10-12.** Source revision `db68cd14bcc1ee93ad9adaa4bb981bc63984b46a`
(`main` after #299). The refresh records the partial
[M9 hardware session](../M9_HARDWARE_SESSION.md) and the owner's evidence waiver
([ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)): D-003 is fixed (AMD
hardware within the v2 budget, [R082.ii](R082ii-amd-hardware-session.md); Intel waived),
D-004, D-005 and D-020 are now Limitations that are untested on the waived
configurations, and D-024 (10,000-placement edit feedback) and D-025 (label overlap)
are new. See the table and the counts.
**Updated 2026-10-12 (R082.jj).** [R082.jj](R082jj-edit-feedback.md) fixes D-024: AMD edit
feedback at 10,000 placements is now 37.6–40.6 ms p95 for deep and textures. See the counts.
**Updated 2026-10-13 (R084.v).** [R084.v](R084v-keyboard-reachability.md) fixes D-026, added by
R084.u the same day. See the counts.
**Release-candidate triage (2026-10-13).** The release candidate (RC) is `main` at
`7b01bb7a1d3fa19b6b5cc7f1a4c27a2ad5cd03a2`. The update re-read the RC walkthrough
[R089.d](R089d-release-candidate.md), the RC provider run [R086.ca](R086ca-rc-remote-evaluation.md),
CI run [38435734443](https://github.com/zygote55/sketchyup/actions/runs/38435734443) and the
[R088.f parity report](R088f-core-parity-report.md). It adds D-027–D-029 and extends D-021;
see [Release-candidate triage](#release-candidate-triage-2026-10-13). From this update the
list is the release-candidate triage. The gate decision is in [M9](M9.md).
**Updated 2026-10-14 (R091.h).** [R091.h](R091h-sheet-layout.md) adds D-031 (Minor): a
silent Wayland exit of `keyboard_reachability_tests` with a Weston "finalizing a layer with
views still on it" BUG line, seen once in CI on PR #320; and D-032 (Minor): focus rings below the
threshold at fractional 1.5×. See the counts.
**Updated 2026-10-15 (D-033).** The X11 2× AT-SPI bridge line failed once on `main` (run
[38690132218](https://github.com/zygote55/sketchyup/actions/runs/38690132218)) with the demo
aborting at start and an empty log. It is a test-harness race in Xvfb's server reset, added and
fixed as D-033; see [Candidates checked and not carried forward](#candidates-checked-and-not-carried-forward).
**Updated 2026-10-15 (D-034, D-007; branch `fix/wayland-render-hang`).** `render_input_tests`
timed out once on Wayland 2× on `main` after #327 with no output; it is recorded as D-034
(Minor, unverified), and the native-test harness now explains such failures itself (see
[Harness diagnostics](#harness-diagnostics-d-034)). The `sheet_export_input_tests` focus
failure on PR #328 is a reproduced instance of D-007, fixed for the export dialog's choosers;
D-007 stays open for its other instances. See the counts.
**Updated 2026-10-15 (D-035; branch `fix/assistant-sheet-tools`).** The 1.1.0 RC provider
corpus failed Advanced 1/3 on its token budget because the assistant's tool list carried the
drawing-sheet tools. Added and fixed as D-035 (Major); see
[Candidates checked and not carried forward](#candidates-checked-and-not-carried-forward).
**Updated 2026-10-15 (D-036; Qt 6.8 reference at the 1.1.0 RC).** The Qt 6.8 reference clean
machine ran at the 1.1.0 RC `093484f9` ([F01-rc-1.1.0](F01-rc-1.1.0.md#failures)). Every
lifecycle step passed, but the sheet PDF written on Qt 6.8.0 has a malformed Info dictionary.
Added as D-036 (Minor, Qt 6.8 only). The owner decided to ship 1.1.0 with it recorded and fix
it in 1.1.1. See the counts.
**Updated 2026-10-16 (1.1.1 published).** D-036 (malformed PDF document information on Qt 6.8.0, Minor,
shipped in 1.1.0) was found by the Qt 6.8 reference clean machine at the 1.1.0 RC and is fixed
by the 1.1.1 patch ([F01.a](F01a-1.1.1-patch.md)). See the counts.
**Updated 2026-10-17 (1.2.0 RC `71a6b4f3`).** The F02 RC walkthrough
([F02-rc-1.2.0](F02-rc-1.2.0.md#superseded-rc1-71a6b4f3)) adds D-037 (Major, found by the gate): 1.2.0
does not compile against Qt 6.8.0, the declared minimum. It adds the F02 follow-ups D-038
(Minor, a paint marks a driver Changed with "changed shape" wording) and D-039 (Minor,
`checkUniqueGeometry` cost per edit), and D-040 (Minor, a Weston segfault in one CI Wayland 2×
line on `main`). D-021 is extended with the F02 integration's CI exposure timeouts. See the counts.
**Updated 2026-10-17 (D-037 fixed; branch `fix/d037-qt68-orientation`).** The 1.2.0 RC
walkthrough found D-037 (Major: 1.2.0 does not compile against Qt 6.8.0, the declared
minimum) on the Qt 6.8 reference clean machine. The Qt 6.8 minimum stays and
the attribute is guarded at compile time; see "Closed by the D-037 fix" below.
**Updated 2026-10-17 (1.2.0 RC2 `d80fb729`).** The RC2 walkthrough
([F02-rc-1.2.0](F02-rc-1.2.0.md#triage-at-rc2)) verifies the D-037 fix on the Qt 6.8 reference.
The owner-approved corpus v4 run at RC2 failed resize, advanced and site 0/3 on a product defect,
added as D-041 (Major, found by the gate); its fix (PR #344) becomes RC3. See the counts.
**Updated 2026-10-17 (1.2.0 RC3 `d9af0396`).** PR #344 fixes D-041; RC3 is checked as a delta of
the RC2 walkthrough ([F02-rc-1.2.0](F02-rc-1.2.0.md#triage-at-rc3)). See the counts.
**Updated 2026-10-17 (D-038 fixed; branch `fix/d038-paint-digest`).** A paint inside a
parametric definition no longer marks its drivers Changed; see "Closed by the D-038 fix" below
and the counts.
**Updated 2026-10-17 (D-039 fixed after 1.2.0; branch `perf/d039-variant-geometry-count`).** The
unique-geometry check now costs O(distinct variants) per edit; see "Closed by the D-039 fix"
below. See the counts.
**Updated 2026-10-17 (D-021 fixed, D-007 extended; branch `test/wayland-activation-exposure`).**
D-021's exposure timeouts are root-caused and fixed in the Wayland harness; see "Closed by the
D-021 fix". The transient-activation waits that PR #340 fixed in `entity_info_input_tests` are
fixed in five more suites (D-007). See the
[activation and exposure matrix](#activation-and-exposure-matrix-2026-10-17) and the counts.
**Updated 2026-10-17 (D-032 fixed; branch `fix/d032-fractional-focus`).** Focus rings at the
fractional 1.5× factor are snapped to whole device pixels by one application-wide filter, and
`visible_focus_input_tests` (Model and Sheets) runs at 1.5 on X11 and Wayland in the
fractional-scaling CI job; see "Closed by the D-032 fix" below and the counts.
**Updated 2026-10-18 (branch `test/weston-output-size`).** D-040 is narrowed to a double
destroy of the Weston input panel's slide animation and mitigated by starting the test
Weston without `weston-keyboard`. It is not reproduced and stays open until CI shows a clean
period; the counts, recomputed from the table, are unchanged. The Wayland 2× output size was
measured and kept; see the D-021 entry below.
**Updated 2026-10-18 (D-027 and D-028 fixed; branch `test/d027-d028-tooling`).** The M6 offline
script records a null source commit when `git` is absent, as the M5 script does (D-027), and
the Wayland harness takes a per-line deadline, set to 120 s on `visible_focus_input_tests`
(D-028); see "Closed by the D-027 fix" and "Closed by the D-028 fix" below and the counts.
**Updated 2026-10-18 (D-022 fixed; branch `test/d022-fuzz-asan`).** The parser fuzz targets'
alloc-dealloc mismatch is root-caused to the libFuzzer link: the fuzzer runtime's private
operator new and delete took over every form but the nothrow ones. The targets now define the
nothrow forms, and all 11 run their seeds and a 60 s campaign under the documented options;
see [R083.i](R083i-fuzz-asan.md), "Closed by the D-022 fix" below and the counts.
**Updated 2026-10-18 (D-023 fixed; branch `fix/d023-focus-contrast`).** The focus-ring contrast
residuals next to the 3D canvas are fixed: the breadcrumb, the component scope banner and the
Assistant container have a two-tone ring, the canvas's own ring clears 3:1 against dark chrome,
native scroll bars are themed, and the controls that overflowed their scroll area are laid out;
see "Closed by the D-023 fix" below and the counts.
**Updated 2026-10-18 (D-042 added and fixed; branch `fix/clipper2-tessellation-hang`).** Face
tessellation and section caps could allocate without bound on faces larger than about 10 m
(Clipper2's inexact segment test); see "Closed by the D-042 fix" below and the counts.
**Updated 2026-10-19 (D-043 added and fixed; branch `fix/d043-terrain-history-budget`).** A
single terrain over about 151,000 triangles (about 76,000 for Regenerate, Terrain settings and
Explode) could not be applied: its tiles exceeded the 64 MiB per-edit history budget. Terrain
edits now have their own history budget; see "Closed by the D-043 fix" below and the counts.
**Updated 2026-10-19 (D-044 added; R095.f, branch `docs/r095f-terrain-acceptance`).** The
[R095.f terrain acceptance](R095f-terrain.md) finds that the terrain import's parse and
triangulation, budgeted at 3 s "with progress and cancel" (ADR 0168 §6), has neither a progress
indication beyond "Checking…" nor a cancel that stops the run. Added as D-044 (Minor). A
250,000-triangle terrain also grows its file by about 12 MB against the 8 MB budget: the binary
mesh chunk the budget names is within it, but the terrain record's stored sources are JSON text.
Added as D-045 (Minor). The owner then restated the budget as the mesh chunk (2026-10-19), so
D-045 is closed on the same branch. See the counts.
**Updated 2026-10-19 (D-044 fixed; branch `fix/d044-terrain-import-progress`).** Terrain import's
parse, preparation, triangulation and tiling now report stage progress and stop when cancelled:
the import dialog and the roles step of Create terrain from selection show the stage with a
progress bar, and Cancel, closing the dialog or a changed setting stops the running dry run within
55 ms at 250,000 triangles; see "Closed by the D-044 fix" below and the counts.
**Updated 2026-10-21 (D-047 added and fixed; branch `fix/d047-qt68-includes`).** The Qt 6.8
reference build of the second 1.3.0 release candidate (`6ed335fb`) failed: the package's build
compiles the test suites, and `tests/terrain_edit_input_tests.cpp` used `QJsonDocument` without
including it, which Qt 6.11 and 6.12 headers pull in and Qt 6.8.0 does not. A keep-going build
of the same archive on the reference snapshot (`ninja -k 0`, 809 targets) found no other error.
Added as D-047 (Major, as D-037) and fixed on the same branch; see "Closed by the D-047 fix"
below and the counts.
**Updated 2026-10-21 (D-046 added and fixed; branch `fix/provider-stream-error-detail`).** The
owner-approved corpus v5 run at the first 1.3.0 release candidate (`09b8a061`) stopped after its
first five tasks, each failing in 4–5 s with "Provider returned an incomplete or invalid
response". Two owner-approved diagnostic requests showed the cause: the ChatGPT account had
reached its plan usage limit, and the service streams an `error` event with code
`subscription_sharing_usage_limit_exceeded` before `response.failed`. The usage-limit message
was recognised only on `response.failed`, so the generic diagnostic won. Added as D-046 (Minor)
and fixed on the same branch; see "Closed by the D-046 fix" below and the counts.

**Updated 2026-10-31 (branch `fix/flakes-wayland`: CI flakes D-031, D-034, D-040 and D-019).** D-031 recurred on
`main` on 2026-10-30. Its deadline stacks place the hang at process exit, in the Qt 6.11.2 Wayland client
(the frame-event thread holds up `QWaylandDisplay`'s destructor), not in SketchyUp; it did not reproduce in 80
runs, so it stays open. D-034 did not recur and did not reproduce in 40 runs; the stacks that would place a
recurrence have been in place since 2026-10-15. D-040 gets a further harness mitigation (the shell's own
animations off in the isolated Weston), not verified. D-019 is decided as a Limitation: the probe holds the
window hidden through a file operation, which product flows do not do, except possibly a close request in the
first 150 ms of an operation (an open question, recorded in its row). See the rows below and the counts.

**Updated 2026-10-30 (D-006, D-007, D-008 investigated; branch `fix/flakes-x11`).** No status changed to Fixed. D-007's dialog waits now wait for keyboard focus, D-008's scene test waits for the transition's finish event, and D-006 is not reproduced, so a failing run now prints its counters. See [CI flake investigation (2026-10-30)](#ci-flake-investigation-2026-10-30).

**Release-candidate triage (1.6.0 RC1, 2026-10-31).** The RC is `58afe4a6`, the head of `main` and `release/1.6.0`. Since the 1.5.0 RC (`1ca204fd`), 21 merge commits reached `main`, and 187 files changed. The update adds D-086 and D-087, both open: D-086 is the durable outcome store's budget, which stops a model over about 32 MiB from committing (an owner decision), and D-087 is a credit notice that a retained re-render can carry for a terrain outside its view. It checks every open row against the RC. Nothing is release-blocking. See [Release-candidate triage (1.6.0 RC1)](#release-candidate-triage-160-rc1-2026-10-31).
**Release-candidate triage (1.5.0 RC1, 2026-10-30).** The RC is `1ca204fd`, the head of `main` and `release/1.5.0`. The update adds D-076 to D-081, six defects that the 1.5.0 records name, each fixed before the RC. It checks every open row against the RC and gives the counts. Nothing is release-blocking. See [Release-candidate triage (1.5.0 RC1)](#release-candidate-triage-150-rc1-2026-10-30).
**Release-candidate triage (1.4.0 RC1, 2026-10-28).** The RC is `9ed43c90`, the head of `main`
and `release/1.4.0`. Since the D-048 refresh (2026-10-27, `c56e7123`), 117 commits reached `main`. The
update adds the defects that the 1.4.0 records name, D-049 to D-069 (19 fixed, 2 open), and checks
every open row against the RC. Three rows gained evidence and their text is updated: D-015 (`.skp`
import is now in the product), D-016 (the presentation batch is a separate feature with an optional
MP4) and D-018 (the RC1 provider run, [R086.h](R086h-rc140-remote-evaluation.md)). D-048 is fixed by
PR #498, merged. D-029 is still open: the PKGBUILD changed only `pkgver`. The other open rows are
unchanged. The Wayland harness, the Weston mitigation and the deadlines are as before, and no
changed file cited by an open row alters its evidence. Nothing is release-blocking, and no Major row
is open. RC1's full tier added D-070 (Minor, a test sampling window in `skp_import_dialog_tests`), and
the RC walkthrough added D-071 (Minor, the release notes' rollback omits removing `sketchyup-ifc`;
2026-10-29). See [Release-candidate triage (1.4.0 RC1)](#release-candidate-triage-140-rc1-2026-10-28).
Before the RC update, this was a working triage list of known defects against `main`. It was
not the release-candidate triage, and it is not a release sign-off: it does not accept
R089, M9 or any Core row. It consolidates defects that are recorded in prose across
the evidence, because the repository has no issue tracker entries for them.

## Method

These sources were searched for findings, retained failures, intermittent results,
known limitations and other open items:

- All `docs/verification/*.md` records (R001–R088, CI notes, native spike), and
  the `.txt`/`.json` logs they point to where a record names a failure.
- The gate records [M0](M0.md)–[M6](M6.md), [M7](M7-accepted.md) and [M8](M8.md),
  including M8's "Remaining issues".
- [PR_ROADMAP](../PR_ROADMAP.md) status lines for R082–R089, the
  [support matrix](../SUPPORT_MATRIX.md), the [user guide](../USER_GUIDE.md)
  troubleshooting table, the [scope matrix](../SCOPE.md) and `docs/decisions/`.
- `TODO`/`FIXME`/`XXX`/`HACK` markers in `src/` and `tests/` (none found), and
  skipped or disabled tests. The only conditional skips are the eleven optional
  consumer tests (`SKIP_RETURN_CODE 77`), which skip without their external tool
  and run in CI's Blender and consumer steps; they are not defects.
- CI history: the last 30 `main` workflow runs (`gh run list --branch main`), the
  logs of the two failed runs, and the file-operation hang tracker kept outside
  the repository.

Each candidate was checked against later evidence records and the current code,
tests and CI workflow. Only items still open on `main`, or items whose status could
not be determined, are listed. Items marked **Unverified** could not be confirmed
open or closed from the available evidence.

Classes: **Release-blocking** means possible data loss or corruption, a recovery
failure, a critical security issue, a crash or hang in a Core workflow, or a Core
scope row whose acceptance fails. **Major** means a wrong result or broken workflow
with a workaround, or a hardware/platform failure on a supported configuration.
**Minor** means cosmetic, a performance miss within tolerance, or a test-infrastructure-only
problem. **Limitation** means a documented, intended boundary.

## Triage table

| ID | Title | Sources | Scope | Evidence that it is still open | Class | Rationale | Fix owner |
| --- | --- | --- | --- | --- | --- | --- | --- |
| D-037 | SketchyUp 1.2.0 does not compile against Qt 6.8.0, the declared minimum | [F02-rc-1.2.0](F02-rc-1.2.0.md#failures) | N02 (packaging), N03 | **Fixed 2026-10-17** (found by the 1.2.0 RC gate; see "Closed by the D-037 fix"). `src/app/window_sidebar.cpp` (R094.a, `c3cfd6ac`) uses `QAccessible::Attribute::Orientation` for the sidebar handles' AT-SPI orientation. Qt 6.8.0's `qaccessible_base.h` declares only `Custom` and `Level`; `Locale` and `Orientation` come with a later Qt (the 6.11.2 headers have them). Building the RC archive on the Qt 6.8 reference (`base-devel-20241013.0.269705`, 2024-10-15 archive, Qt 6.8.0, GCC 14.2.1) stops at `window_sidebar.cpp:112: error: 'Orientation' is not a member of 'QAccessible::Attribute'`. `CMakeLists.txt` still asks for `find_package(Qt6 6.8 …)`, and the 1.2.0 support matrix claims Qt 6.8.0 through 6.12.0 and "Build requires Qt 6.8 or later". A package built on Qt 6.11 cannot start on Qt 6.8 (R087.u), so no 1.2.0 runs on a Qt 6.8 system. | Major | A supported configuration (Qt 6.8.x, per the support matrix) has no working 1.2.0. No data effect: the RC package works on Qt 6.11.2 and 6.12.0. The installed documents or the code must change, so it cannot ship at this RC unchanged | Fixed: the attribute is compiled only on Qt 6.11 or later, where `Orientation` first appears; the Qt 6.8 minimum stays. Needs the new RC |
| D-006 | Intermittent X11 viewport assertion "Camera movement only reorders transparency" | [R085.i](R085i-assisted-output-transitions.md#retained-attempts-and-validation), [R085k X11 log](R085i-r085k-xcb.txt) | N01, P02 | **Open (2026-10-30).** R085.i retains it as "intermittent … remains unexplained". Not reproduced in 151 X11 runs of its CI line and fractional-scale variants (51 at 1×, 40 at 1.5×, 60 under two busy loops) or in 20 Wayland 1.5× runs; see [the flake section](#ci-flake-investigation-2026-10-30). The assertion (`tests/viewport_tests.cpp`) now prints the render counters when it fails. | Minor | The failed check is a render-statistics assertion: either geometry was re-uploaded or transparency was not re-sorted after a camera move. The worst case is an extra upload or a transient blend-order glitch, with no document effect. | [R085](../PR_ROADMAP.md#r085) (viewport cache, with [R082](../PR_ROADMAP.md#r082)): investigation. Next occurrence: attach the "Camera movement counters" line from the CI log |
| D-007 | Wayland native-test focus and activation races | [R015](R015-topology.md), [R035.a](R035a-entity-measurements.md), [R084.p](R084p-dialog-keyboard.md), [R087.t](R087t-native-ci-reproduction.md), [PR #273](https://github.com/zygote55/sketchyup/pull/273), [main run 38011483053](https://github.com/zygote55/sketchyup/actions/runs/38011483053), [PR #328 run 38747320047](https://github.com/zygote55/sketchyup/actions/runs/38747320047) | N03 | **Open (2026-10-30).** Recurring intermittent fixture failures where a window reports active before its native surface has focus. The common cause is that `QTest::qWaitForWindowActive(dialog)` returns at once for a transient dialog while its parent is active. On Wayland 1× the wait returned before the dialog had focus in 162 of 165 calls. All ten dialog waits now use `qWaitForWindowFocused`, and 165 of 165 calls then held focus (see [the flake section](#ci-flake-investigation-2026-10-30)). Examples: the R015 Extrude shortcut, the R087.t "Tab reaches shortcut control without assigning focus", and the entity-info Wayland 2× focus timeout on `main` before #273. Each received a fixture wait or re-request; none has a confirmed root cause. Three `main` runs have passed since #273. **2026-10-15: one instance root-caused and fixed.** PR #328 (`4767b538`) failed `SKETCHYUP_TEST_SCALE=1 scripts/test-wayland.sh build/dev/sheet_export_input_tests` with "Tab stays in the dialog looking for sheetExportAllowChanged; focus none, active window none" (the check message is printed; this is not D-031). It is the R091.h Qt Wayland client race ([R091.h round 3](R091h-sheet-layout.md#round-3-focus-after-choose-scene-on-wayland)) on the export dialog's PDF file chooser, which the test accepts as soon as it shows: in the traced failures Weston sends `wl_keyboard.enter` for the chooser and the client asks for a `wl_display.sync`, the chooser is destroyed about 4 ms later, before `done`, and Qt then queues "no focus window" over the export dialog's own `enter`. Reproduced at `68ff9ae2` in the CI image, Wayland 1×, four lanes, `WAYLAND_DEBUG=client`: 7 of 64 runs, every one with this message. The chooser and the print dialog are now `Settled<QFileDialog>` and `Settled<QPrintDialog>` (`sheet_export_dialog.cpp`): after, 0 of 100 traced runs under the same load, and 39 of 40 untraced runs passed at Wayland and X11 1× and 2× (the one failure lost its Xvfb when two local lanes started `xvfb-run -a` at once). The other D-007 instances keep no confirmed cause. **2026-10-17: the transient-activation instances.** [PR #340](https://github.com/zygote55/sketchyup/pull/340) root-caused the entity-info Wayland 2× failure: `QTest::qWaitForWindowActive(dialog)` passes at once while the main window is active, because `QWindow::isActive()` counts a transient child as active with its parent (Qt 6.11.2), so the dialog could close before Weston activated it, and that late activation took focus from the main window. Branch `test/wayland-activation-exposure` makes the same change where the pattern remained: the text size prompt (`text_size_input_tests`, `viewport_label_overlap_tests`), the shortcut editor (`shortcut_input_tests`, both waits), five dialogs in `visible_focus_input_tests` (the modal sweep, the evidence dialog, Export sheets, the parameters scope dialog and Assistant preferences), and the Render sheet (both waits) and the Export sheets dialog in `keyboard_reachability_tests`. Each now waits for the dialog's own keyboard focus (`qWaitForWindowFocused`, the 5 s default) before it acts or closes; no check changed. Waits on the main window are unchanged: a top-level window's `isActive()` is exact. In the [matrix](#activation-and-exposure-matrix-2026-10-17) no changed wait failed, and no run failed a focus or activation check before the fix, so the matrix does not show this race; the change removes the premature pass. One run after the fix failed R091.h's file-operation focus-return check in `keyboard_reachability_tests`, a path this branch does not change (see the matrix). `keyboard_reachability_tests` still re-maps the window if Weston leaves none active after a sheet hides (R084.v). D-007 stays open for the other instances. | Minor | These are test-infrastructure races in isolated Weston. No product focus defect has been shown. | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only). Still open: the R084.v re-map in `keyboard_reachability_tests` (a top-level window, whose waits are exact); no failing check reproduced |
| D-008 | Scene-transition native test timed out in CI under delayed events | [R085.d](R085d-scene-animation-fixture.md) | P03 | **Open (2026-10-30).** CI waited 1.5 s for the saved pose and failed. The wait stood for the 160 ms transition ending, so it now waits for that transition's `finished` signal; the 5 s bound only stops a hang. The transition ended in 192–607 ms in 34 runs with the new wait, and the 1.5 s miss was not reproduced in 26 runs before it. The cause is recorded as "not a confirmed root cause". Product timing is unchanged. | Minor | Eventual-correctness timing in a test fixture. Final-pose equality is still enforced. | [R085](../PR_ROADMAP.md#r085) (test-only). Next occurrence: the "Scene transition ended after N ms" line shows whether the transition was late or never ended |
| D-009 | X11 hardware fixture cannot place its window on a requested output without a compositor helper | [R085.h](R085h-physical-outputs.md), [R085.i](R085i-assisted-output-transitions.md) | N04 | **Open.** Both Qt-only X11 placement attempts fail the requested-output assertion. X11 transitions qualify only through the Hyprland-specific helper. | Minor | Test infrastructure: the placement is of the fixture's own window, as Qt documents for `setScreen`. Rendering on both outputs passes once placed. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-019 | Qt Wayland protocol error when re-showing a window hidden during a file operation | [R083.f](R083f-file-worker-hang.md) | N01 | **Limitation (decided 2026-10-31).** Observed in 6 of 8 probe runs at 1×: the probe hid the window, ran a file operation, then showed the window again (R083.f, with its reproduction results). The probe is the artefact: it keeps the window hidden for a whole file operation, which no product flow intends. The product's only hide of the main window is the close barrier (`WindowUnmapBarrier` and `Window::closeEvent`, `src/app/window.cpp`). It runs after `canReplace()` and re-shows the window with no file operation in it; R083.f saw no protocol error in 0 of 8 runs that hid and showed the window without file work. Once the file-operation progress window is shown, it is application-modal, and Qt refuses spontaneous close events for a window it blocks (`QGuiApplicationPrivate::processCloseEvent`, qtbase 6.11.2). That window appears after 150 ms, so a close request in the first 150 ms of an operation is not refused. No test covers that case and it was not reproduced: it is the open question for the owner, and a hide or restore feature must retest this row. | Limitation | Not reached by the product's flows as read; the Qt Wayland client breaks the protocol only after a hide across a file operation, which the probe does on purpose. Recorded so a future hide/restore feature retests it. | [R085](../PR_ROADMAP.md#r085) |
| D-021 | Wayland test-window exposure timeouts under host load | [R083.g](R083g-unexpected-modal-guard.md), [R084.r](R084r-display-precision.md), [R084.s](R084s-keyboard-panel-tabs.md), [R085.q](R085q-keypad-decimal.md), [R089.d](R089d-release-candidate.md#failures) | N03, N01, P05 (test harness) | **Fixed 2026-10-17** (see "Closed by the D-021 fix": Weston sends no frame callback until its desktop shell has drawn, which took 8–20 s under load, and Qt marks a window unexposed when a callback is 100 ms late). Before the fix, in local bounded-container Wayland 2× runs, fixtures fail in `qWaitForWindowExposed` before any test step: 2 of 4 `units_input_tests` runs (load average about 28), 3 of 6 `file_operation_input_tests` runs (load above 30; 6/6 passed at about 15), five Wayland 2× cases in the R084.s matrix (style, solar, reference image, annotation, audit) and one `numeric_input_tests` run. Identical reruns pass without a changed wait. These records attribute none to CI. The cause is not established beyond host load. **At the RC (2026-10-13)**, on the one-CPU runner: `annotation_input_tests` Wayland 2× failed its "Annotation window exposed" check once, and `interaction_tests` Wayland 2× exited 2 once (window not exposed and active within 5 s; the "active" part overlaps D-007). Both passed on re-run. CI run 38435734443 passed the annotation line; CI runs `interaction_tests` on X11 only. **At the 1.2.0 RC (2026-10-17)**: the F02 integration saw the same "window exposed" timeout in CI at roughly 1% of Wayland lines (a figure from the F02 integration's CI history, not counted line by line here); each passed on re-run. CI run 39022075019 at the RC's pre-merge head passed every line, and the 47 local non-CI cells at the RC are recorded in [F02-rc-1.2.0](F02-rc-1.2.0.md). | Minor | Test infrastructure. The failure precedes any product code in the test and is not a focus defect, so it is distinct from D-007 (focus lost after exposure). | Fixed (test harness): `scripts/test-wayland.sh` sets `QT_WAYLAND_FRAME_CALLBACK_TIMEOUT` to the 60 s deadline |
| D-022 | Parser fuzz targets abort on their first seed under ASan with Qt 6.11.2 JSON parsing (F1) | [R083.e](R083e-parser-fuzzing.md), [R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md#f1-at-the-rc), [R083.i](R083i-fuzz-asan.md) | D02, D06 (test tooling) | **Fixed 2026-10-18** (see "Closed by the D-022 fix"). Was: six of ten libFuzzer targets (native JSON, container, GLB, template, component, extension) aborted on an unmodified valid seed with an ASan `alloc-dealloc-mismatch (operator new vs free)` whose stacks lay inside `libQt6Core.so.6` under `QJsonDocument::fromJson`; the minimized `{"":2}` reproduced it in a three-line libFuzzer program, and it reproduced unchanged at the RC. **Cause:** the link, not Qt or SketchyUp. Qt's parser sorts object members with `std::stable_sort`, whose libstdc++ buffer allocates with `operator new(size_t, std::nothrow_t const&)` and frees with sized `operator delete(void*, size_t)`, a correct pair. In a `-fsanitize=fuzzer` link, `libclang_rt.fuzzer` (linked before the ASan C++ runtime) exports the weak `operator new`/`delete` of its private libc++, which call `malloc`/`free`, but keeps its nothrow `new` local, so only the nothrow `new` resolves to ASan's; the delete reaches `free`. **Fix:** the fuzz targets define the nothrow `new`/`delete` forms (`tests/fuzz/fuzzer_allocation.cpp`) so they use the same operators as the rest of the target; the check stays on. All 11 targets (with `parameter-expression`) pass their seeds and run 60 s each under the documented options with no finding. | Minor | Test tooling only: the mismatch came from the libFuzzer link, and no application or Qt frame mismatched. The sanitized test suites, which do not link libFuzzer, never had it. | Fixed (fuzz harness). Continuous fuzzing stays an owner decision ([R083.i](R083i-fuzz-asan.md#continuous-fuzzing-recommendation)) |
| D-023 | Focus-ring contrast residuals next to the 3D canvas and native scroll bars | [R084.q](R084q-visible-focus.md), [R084.s](R084s-keyboard-panel-tabs.md) | N05 | **Fixed 2026-10-18** (see "Closed by the D-023 fix"). Was: R084.q recorded that the dark-theme ring has 1.64:1 contrast against the light default canvas where the tool rail, breadcrumb and assistant container touch it; the canvas's own ring is 2.997:1 against dark chrome; native scroll bars are not themed; 14 controls per run are wider than their scroll area at 200% text. R084.s left one or two tab-bar scroll buttons in the Tab order between the tab bar and its page; [R084.v](R084v-keyboard-reachability.md) removed them from the Tab order. | Minor | Cosmetic and keyboard-friction issues. Every ring kept at least 3.93:1 against the colour directly inside it, and no control was unreachable. | Fixed: a two-tone ring (ring inside a halo in the control's own background) where a control borders the canvas, a canvas ring token of 3.03:1 against dark chrome, themed `QScrollBar`, and two layout fixes for the 14 overflowing controls |
| D-027 | `verify-m6-offline.sh` fails in its final summary when `git` is missing | [R089.d](R089d-release-candidate.md#failures) | E04–E07, O03 (study tooling) | **Fixed 2026-10-18** (see "Closed by the D-027 fix"). Was: at the RC, attempt 1 completed every step, including 190/190 regressions, then raised `FileNotFoundError: … 'git'` in the summary. `verify-m5-offline.sh` handled a missing `git`; the M6 script did not. Attempt 2, with `git` installed from the same pinned archive, passed. | Minor | Test infrastructure only: every study check passed before the summary failed, and the study passes when `git` is present. | Fixed (study tooling): the script records a null source commit when `git` is absent, as the M5 script does |
| D-028 | `visible_focus_input_tests` exceeds the 60 s Wayland harness limit at 2× on one CPU | [R089.d](R089d-release-candidate.md#failures) | N05 (test harness) | **Fixed 2026-10-18** (see "Closed by the D-028 fix"). Was: three attempts on the one-CPU runner ended at 60.2–60.3 s in `test-wayland.sh` with no failed check. The same build passed on six CPUs (Wayland 1× 14.4 s, 2× 28.4 s), and CI run 38435734443 passed the same line. | Minor | Test infrastructure: a harness timeout under a one-CPU quota. No check failed, and the suite passes with more CPU and in CI. | Fixed (test harness): `SKETCHYUP_TEST_DEADLINE=120` on this suite's Wayland lines; every other line keeps 60 s |
| D-031 | `keyboard_reachability_tests` exits silently on Wayland with a Weston "finalizing a layer with views still on it" BUG line | [R091.h](R091h-sheet-layout.md#defect-note-d-031), [PR #320 run 38605636463, attempt 1](https://github.com/zygote55/sketchyup/actions/runs/38605636463/attempts/1) (job 115995661310) | N03 (test harness) | **Open.** Once, in CI on PR #320 (R091.d, `5bf306b8`), attempt 1: the Wayland 1× line `SKETCHYUP_TEST_SCALE=1 scripts/test-wayland.sh build/dev/keyboard_reachability_tests` printed "Side tabs: … passed", then, 7 s after Weston started, the only further output was Weston's `BUG: finalizing a layer with views still on it.` and the step failed with exit 1. No check message was printed. Attempt 2 on the same commit passed every job, and R091.h's local matrix passed the suite on Wayland. R091.d changed no desktop code. The failure is in the window phase that follows "Side tabs" (Render with Blender reopening), with no application message, so the cause (a client crash, a compositor shutdown race, or the harness ending Weston early) is not established. **2026-10-31: the cause is placed, not reproduced.** The recurrence on `main` ([run 41145912353](https://github.com/zygote55/sketchyup/actions/runs/41145912353), `0b0c75d5`, 2026-10-30) is the same line at `QT_SCALE_FACTOR=1.5`: `visible_focus_input_tests --workspace=sheets` printed its complete focus report and then did not exit. The deadline stacks, in place since 2026-10-15, show the test's main thread in `QWaylandDisplay::~QWaylandDisplay()`, reached when `main` returns, waiting in `QThread::wait()` for the Wayland frame-event thread. That thread is in `QWaylandWindow::handleFrameCallback()`, blocked on the window's `mFrameSyncMutex` (qtbase 6.11.2, `src/plugins/platforms/wayland`). No thread in the dump is inside that mutex, so its holder is not identified. A window freed while its frame callback is still queued is one candidate for the lifetime race; it is not shown. The Weston BUG line is printed after the harness stops Weston, so it is a consequence of the hang. Reproduction in the CI image (`sketchyup-ci-pinned:r097a-ifc`, 4 GB, 2 CPUs, the exact line): 0 of 50 runs solo, and 0 of 30 with both CPUs busy, at `496b8284`'s harness. No harness change is made for this row, because the next occurrence already prints the stacks that placed this one. | Minor | Test infrastructure: no check failed, and the hang is in the Qt Wayland client at process exit, after every check had reported; no SketchyUp code is on the stack. Distinct from D-021 (exposure timeout with a named check) and D-007 (focus lost after exposure, with a named check). | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only): keep the Weston log and a core dump on failure; the owner is the Qt 6.11.2 Wayland client teardown (upstream report not yet filed) |
| D-032 | Focus ring under the threshold at fractional 1.5× | [R091.h](R091h-sheet-layout.md#native-matrix) | N05 | **Fixed 2026-10-17** (see "Closed by the D-032 fix"). Was: `visible_focus_input_tests` at `QT_SCALE_FACTOR=1.5` failed on X11 and Wayland (143 and 135 measurements in the Model run, 88 and 90 in the Sheets run, in `sketchyup-ci-pinned:r094a`): tool buttons, panel tab bars, the breadcrumb and controls in the material, solar, drawing-plane and assistant-preference dialogs. Qt's style sheet style paints every border antialiased, so a ring edge in the middle of a device pixel left that pixel a blend of the ring and the chrome beside it, under 3:1. A 2 px ring (R091.h) has the same part-covered edge pixel. Now 0 failures at 1×, 1.5× and 2× on both platforms, both themes. | Minor | A rendering residual of a 1 px indicator at a fractional factor; the ring is still drawn and keyboard focus is unaffected. Every 1× and 2× cell passes. | [R084](../PR_ROADMAP.md#r084): a 2 px ring or device-pixel-aligned focus drawing, with the suite at 1.5 in CI |
| D-034 | `render_input_tests` timed out on Wayland 2× with no output | [main run 38742605671](https://github.com/zygote55/sketchyup/actions/runs/38742605671) (job 116447893819, `68ff9ae2`, after #327) | N03 (test harness) | **Unverified.** Once on `main`: the section-fixture line `SKETCHYUP_TEST_SCALE=2 SKETCHYUP_RENDER_SECTION_FIXTURE=1 SKETCHYUP_RENDER_MODEL=…/section-nested.sketchyup scripts/test-wayland.sh build/dev/render_input_tests` printed nothing for 60 s and Weston was stopped. Blender is not involved: Wayland lines set no `SKETCHYUP_BLENDER_TEST`, so the fake worker runs. The silence does not place the hang: the suite prints one line, at its end, to `std::cout`, which is block-buffered into the CI pipe, so a hang anywhere in its 8–11 s flow, or at exit, prints nothing; the four "could not load cursor" lines are Weston's own keyboard and shell clients, not the test. No other `render_input_tests` timeout is in the 80 failed Native build runs since 2026-10-07, while the line has run on every run since 2026-10-06. Locally, at `68ff9ae2` in the CI image without Blender: 0 of 740 runs of the exact line hung (540 with six parallel lanes, 300 of them through the new harness; 200 with two). One six-lane run failed a 5 s "First native queued worker starts" wait under that load, which is not this signature. **2026-10-31: no recurrence, and the harness would name a frame.** None of the 12 failed `main` runs after the D-034 run (2026-10-16 to 2026-10-30) has a `render_input_tests` timeout, and the stacks that are now printed at the deadline placed the same class of hang on 2026-10-30 (D-031 above). Reproduction of the exact line in the CI image at `496b8284`, before this branch's harness change: 40 of 40 runs passed, 12 to 24 s each, with 2 CPUs. No product or test change is made for this row. | Minor | One CI occurrence with no check failed and no frame known; treated as test infrastructure until a stack shows otherwise. | The harness now prints every thread's stack and flushes stdio at the deadline ([Harness diagnostics](#harness-diagnostics-d-034)); a recurrence names the frame, and the fix goes to its owner |
| D-041 | The ChatGPT-plan decoder rejects replies that batch `transaction.inspect` with `transaction.diff` | [F02-rc-1.2.0](F02-rc-1.2.0.md#rc2-provider-corpus-d-041) | A04, A05 | **Fixed at RC3 `d9af0396`** (found by the 1.2.0 RC2 gate; fix [PR #344](https://github.com/zygote55/sketchyup/pull/344)). In the owner-approved corpus v4 run at RC2 (`d80fb729`, profile `chatgpt-astra`), resize, advanced and site scored 0/3 each: `OpenAiConversation::decode` (`src/integrations/openai_provider.cpp`) rejected multi-call replies that combine `transaction.inspect` with `transaction.diff`, so those turns failed. The private run evidence is kept outside the repository. | Major | A wrong result in the supported provider workflow (three corpus tasks cannot complete); no document is changed by a rejected reply, and no data is lost | [R086](../PR_ROADMAP.md#r086): fixed by PR #344 (RC3): `transaction.diff` and `transaction.describe` may share a read-only batch; `openai_provider_tests` covers it |
| D-038 | A paint inside a definition marks its driver Changed, with "changed shape" wording | [R094.h Findings](R094h-configurable-components.md#findings) | F02 | **Fixed 2026-10-17** (see "Closed by the D-038 fix"). A driver's `bound` digest covers its target member's whole record, colour included (ADR 0165 §4), so painting the top made Stretch Top Changed and the strip said the driver "now starts from a changed shape". A template edit that changes only a driven member's appearance now restamps the digest, and the driver stays bound. | Minor | Specified behaviour with misleading wording: Rebind or Undo cleared it, and no geometry or document was wrong | Fixed: [ADR 0165 addendum](../decisions/0165-constrained-component-parameters.md#addendum-d-038-2026-10-17-a-paint-keeps-its-drivers-bound); no file-format change |
| D-039 | `checkUniqueGeometry` costs O(variant placements × members) per edit | [R094.g](R094g-variant-instancing.md#the-22-edit-feedback-gap-in-the-first-report) | F02, N01 | **Fixed 2026-10-17** (after 1.2.0; see "Closed by the D-039 fix"). Every `Document::apply` walked each variant placement's member records while any placement showed a variant: 1.1–1.5 ms per placement move with 10,000 variant placements against 0.4–0.6 ms with default placements (software, R094.g). The variant index now counts placements per variant, so the check is O(distinct variants). | Minor | A performance cost about 1 ms at 10,000 placements, well inside the edit-feedback budget; disclosed in the 1.2.0 release notes | Fixed: per-variant placement counts in the variant index; ships in the next release after 1.2.0 |
| D-042 | Face tessellation and section caps could allocate without bound on faces larger than about 10 m | This PR (branch `fix/clipper2-tessellation-hang`); R095.c (PR #369) | Core modelling (faces, sections); every edit, load and render validates surfaces | **Fixed 2026-10-18** (see "Closed by the D-042 fix"). Was: Clipper2 2.0.1's beta `Triangulate` tested segment crossings with int64 grid products in double. On the 1e-7 m tessellation grid, products pass 2^53 on faces larger than about 10 m, a shared end-point could read as a crossing, and `FixupEdgeIntersects` split edges until memory ran out (systemd-oomd killed the developer session twice). PR #369 only bypassed single triangles. In a capped harness, up to 6 % of random millimetre-rounded quads, L-shapes and faces with a hole of 10–1,000 m hung, and a 100 m quad prism's section cap did too | Release-blocking | A hang/OOM reachable from ordinary modelling (a floor slab or site plan), which the class definition lists as "a crash or hang in a Core workflow"; there was no workaround short of splitting the face | Fixed: exact `SegsIntersect` (shared end-points skipped as upstream 6a36be1c8b, remaining tests on the exact `CrossProductSign`/`ProductsAreEqual`) and hard work bounds in the vendored triangulator; see [PATCHES](../../third_party/clipper2/PATCHES.md) |
| D-043 | Terrain edits over about 76,000–151,000 triangles exceed the 64 MiB history budget | This PR (branch `fix/d043-terrain-history-budget`); found by the R095.e2 worker (PR #374) | Terrain (R095): `terrain.import`, `terrain.create_from_edges`, `terrain.regenerate`, `terrain.set_parameters`, `terrain.explode`, the import and settings dialogs | **Fixed 2026-10-19** (see "Closed by the D-043 fix"). Was: a terrain is created, regenerated or exploded as one edit whose tile bodies are charged at about 440 B per triangle, and `Document::applyEdit` refused any edit over the 64 MiB `historyLimit` with a raw `runtime_error` ("Edit exceeds the 64 MiB history budget"). Create and import refused above 151,250 triangles (275 × 275 grid), Regenerate and Explode above 76,050 (their before and after are both charged), well under the 250,000-triangle document limit (ADR 0168 decision 3). Commands and sessions reported it as `INVALID_OPERATION`, the CLI as a failed `INVALID_OPERATION`, and the import dialog showed the raw text after a successful dry run | Major | A broken workflow inside the declared terrain limit, with a workaround (a coarser or split terrain); nothing was changed by the refusal | Fixed: terrain's own history budget (`Document::terrainHistoryLimit`, 512 MiB charged) for the record, tile and lineage changes of terrain edits; general history unchanged |
| D-044 | Terrain import parse and triangulation show no progress and cannot be cancelled | [R095.f](R095f-terrain.md#gaps-and-defects); ADR 0168 §6 Budgets | Terrain (R095): File → Import terrain…, Create terrain from selection's roles step, `sketchyup-cli --import-terrain` | **Fixed 2026-10-19** (see "Closed by the D-044 fix"). Was: ADR 0168 §6 budgets parse and triangulate at "≤ 3 s, with progress and cancel". The import dialog ran the dry run on a worker and showed only "Checking…" in its footer and the preview; its Cancel closed the dialog, and the dialog's destructor waited for the running dry run (`pool_.waitForDone()`), because `runTerrainImport` and `triangulateTerrain` took no cancellation or progress callback. The CLI printed one result at the end. The dry run took 0.36–0.50 s at 250,000 triangles, so the wait was short. | Minor | A missing part of a performance budget whose time limit is met; no wrong result, and closing the dialog changed nothing | Fixed: a `TerrainProgress` (stage progress and cancellation) through parsing, preparation, triangulation (with CDT's upstream abort hook) and tiling; stage text and a determinate bar in both dialogs; Cancel, closing and a changed setting cancel the run; CLI stage progress on a terminal |
| D-045 | A 250,000-triangle terrain grows its file by about 12 MB, over the 8 MB file-growth budget | [R095.f](R095f-terrain.md#budgets); ADR 0168 §6 Budgets | Terrain (R095): persistence | **Closed (2026-10-19): budget restated by the owner.** Budget: "File growth ≤ 8 MB (binary mesh chunk)". Measured (Release, `terrain_budget_benchmark`): the `terrain-mesh` chunk is 6.31 MB for 249,970 triangles from 125,000 points and 7.57 MB for 249,298 triangles from 416 contour rings (3,169 tiles), within 8 MB; the whole file grows by 12.51 MB and 12.26 MB, because the document chunk also holds the terrain record's stored sources as JSON text (6.08 MB and 3.13 MB) and the tile body records. Open and save stay within their 2 s budgets. | Minor | A size budget missed by about half with no functional effect; the sources are stored by design (decision 7 and R095.c2's Regenerate). Whether the budget covers only the chunk, as its parenthesis says, is an owner question | Closed: the owner restated the budget as the `terrain-mesh` chunk (ADR 0168 §6, 2026-10-19); the stored sources are measured and reported, not budgeted. No format change |
| D-046 | A ChatGPT-plan usage limit is reported as an incomplete or invalid response | [F03-rc-1.3.0](F03-rc-1.3.0.md) (1.3.0 RC1 corpus run, owner-approved diagnostic requests) | A04, A05 (Assistant panel, ChatGPT-plan connection) | **Fixed 2026-10-21** (see "Closed by the D-046 fix"). Was: the plan service answers HTTP 200 and streams `error` (`error.code` `subscription_sharing_usage_limit_exceeded`) before `response.failed`. `completedOpenAiStream` mapped the usage-limit codes to the "Manage ChatGPT usage" message only on `response.failed`, so the `error` event ended the task with the generic "Provider returned an incomplete or invalid response", and neither the panel nor `provider_trial` said why. Present since the usage-limit handling; 1.2.0 has it | Minor | A wrong diagnostic: the task stops as it should, nothing is applied and the document is unchanged | Fixed: the usage-limit codes are recognised on the `error` event too; any other stream error ends with "Provider returned an error: CODE: MESSAGE" from sanitized fields (ADR 0034 addendum), and `provider_trial` records them as `streamError` |
| D-047 | SketchyUp 1.3.0 RC2 does not build against Qt 6.8.0, the declared minimum | [F03-rc-1.3.0](F03-rc-1.3.0.md) (Qt 6.8 reference build at RC2) | N02 (packaging), N03 | **Fixed 2026-10-21** (found by the 1.3.0 RC2 gate; see "Closed by the D-047 fix"). Was: `makepkg` on the 2024-10-15 snapshot (Qt 6.8.0, GCC 14.2.1) stopped at step 630 of 809 with `tests/terrain_edit_input_tests.cpp:2165: error: invalid use of incomplete type 'class QJsonDocument'`. The file (R096.f1) uses `QJsonDocument` but includes only `<QJsonArray>`; Qt 6.11 and 6.12 headers include it transitively. CI builds only on Qt 6.11.2, so no CI job could see it | Major | The package does not build on the declared minimum Qt, as D-037; the application code itself compiled, and no installed file changes | Fixed: `#include <QJsonDocument>` in the test. A keep-going build of the RC2 archive on the reference snapshot found no other error. Follow-up: a Qt 6.8 compile job ([ADR 0170](../decisions/0170-ci-tiers.md) follow-up) |
| D-040 | Weston 15.0.1 segfaulted in `weston_view_animation_destroy` during a CI Wayland 2× line | [main run 38920762782](https://github.com/zygote55/sketchyup/actions/runs/38920762782) (job 117047789178, `9d3a786c`) | N03 (test harness) | **Mitigated (unconfirmed) 2026-10-18; stays open** until CI has run a reasonable period without the crash. Once on `main` (2026-10-16): after `section_viewport_tests` passed, the next line (`style_input_tests`, Wayland 2×) ended when the isolated Weston received SIGSEGV; the D-034 trace shows `wl_list_remove` ← `weston_view_animation_destroy` ← `wl_event_loop_dispatch_idle` in Weston's main thread. No SketchyUp check failed. Later `main` runs passed the line. **Cause narrowed (not reproduced).** In Weston 15.0.1, `libweston/animation.c` frees a finished view animation from an idle source (`idle_animation_destroy`, a tail call, so the trace shows `wl_event_loop_dispatch_idle` directly; an `LD_PRELOAD` probe records that caller for every such destroy). The startup fade's idle callback (`do_shell_fade_startup`) only starts a fade and destroys nothing, so it does not match the stack as such (the PR #356 lead). A `wl_list_remove` fault there means the animation was destroyed twice. The only session-time code that destroys a view animation directly, without cancelling its queued idle destroy, is `show_input_panel_surface()` in `desktop-shell/input-panel.c` (the focus animation is off, and shell teardown runs after the loop); upstream `main` is unchanged. The tests reach it: Qt's text-input-v1 shows the `weston-keyboard` panel, and in a 2× run the crashing line, `style_input_tests`, started 5 panel slides and `text_input_tests` 6. With the idle destroy delayed by fault injection, the probe recorded the panel destroying a slide whose idle destroy was still queued. A crash also needs the replacement slide not to reuse the freed block, as when the panel is shown while no surface has keyboard focus. Focus-toggle, window-churn and timed hide/show clients (18 sessions) did not crash Weston. **Mitigation:** `scripts/test-wayland.sh` starts Weston with a generated `weston.ini` holding `[input-method]` `path=` instead of `--no-config`, so `weston-keyboard` never starts and no panel slide runs; no test uses the input method, and removing the helper costs nothing. The other shell animations (startup and close fades) remain, but no direct destroy of them was found during a session. **2026-10-31: the remaining shell animations are off too (branch `fix/flakes-wayland`).** `scripts/test-wayland.sh` now writes `[shell]` `startup-animation=none`, `close-animation=none` and `focus-animation=none` next to the empty input-method path, so the isolated session starts and closes no view animation. The values are the ones Weston 15.0.1's `desktop-shell.so` accepts; the session's Weston log shows the config loaded with no `invalid … animation type` warning. Checks after the change, in the CI image at `1.5` or `2×` as each line runs: `visible_focus_input_tests --workspace=sheets` 5 of 5 and without `--workspace` 3 of 3, `style_input_tests` 2× 3 of 3, `keyboard_reachability_tests` 1× 3 of 3, `sheet_export_input_tests` 1.5 3 of 3, all passed. No failure of this signature (a Weston `Killed by signal SEGV` or `Weston exited`) is on `main` after 2026-10-16. The crash was not reproduced, so this is a further mitigation, not a verified fix: the row stays open until CI has a clean period with the change. | Minor | An external compositor crash in the isolated test session, like D-020 and D-031; no product frame is involved | [R085](../PR_ROADMAP.md#r085) (test-only): watch CI for recurrence; keep the core dump; close after a clean period. Upstream: report the input-panel double destroy to Weston |
| D-010 | System Wayland 1.26 client leaks a destroyed-proxy reference | [R062.b proxy](R062b-wayland-proxy.md), [R058.c](R058c-native-diagnostics.md) | N01 | **Open (external).** The leak is reproduced standalone without Qt or SketchyUp. Only sanitized CI uses the patched private client (`scripts/ci-wayland-client.sh`); users get the system library. | Limitation | A 96-byte leak per affected dialog event, in a third-party library. Disclosed in R062.b as "a documented external limitation until a fixed distribution library is available". | [R085](../PR_ROADMAP.md#r085): track the distribution fix |
| D-011 | Qt Wayland client-decoration framebuffer leak | [R050](R050-native-render-ui.md) | N01 | **Open (external).** Sanitized Wayland steps still set `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` (`.github/workflows/native.yml`). | Limitation | Upstream Qt allocation, with no application frame in the stack. Disclosed in R050 as a test-environment workaround. | [R085](../PR_ROADMAP.md#r085): track upstream |
| D-012 | Compositor crash with RGB565/odd-stride surfaces at fractional scale | [R085.a](R085a-current-platform-validation.md), [R085.b](R085b-native-surface-color.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | N01, N04 | **Mitigated.** The application now requests eight-bit surfaces and all 20 fractional cases pass. The external Weston/Mesa defect is not shown to be fixed. | Limitation | External defect, avoided by the surface-color policy ([ADR 0157](../decisions/0157-native-surface-color.md)). Disclosed in the support matrix. | [R085](../PR_ROADMAP.md#r085) |
| D-013 | Qt text shaping crashes with no installed fonts | [CI-PR172](CI-PR172-schema-cache.md) | N02, P05 | **Mitigated.** A Qt-only control program reproduces the crash. `packaging/arch/PKGBUILD` hard-depends on `ttf-dejavu`. | Limitation | Occurs only when a declared package dependency is removed. | [R087](../PR_ROADMAP.md#r087) |
| D-014 | Recovery and publication faults are injected, not physical power-loss crashes | [R083.b](R083b-integrated-publication-faults.md), [R083.d](R083d-local-failure-matrix.md) | D04, D02 | **Open by design.** R083.b "does not claim physical filesystem crash recovery". No data-loss defect is recorded in the fault matrix. | Limitation | The evidence boundary is stated; the final data-loss audit remains R083 work. | [R083](../PR_ROADMAP.md#r083) |
| D-015 | Interchange is lossy conversion; `.skp` is import-only (1.4.0); no `.skp` export and no DWG | [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), ADRs [0111](../decisions/0111-native-gltf-import-workflow.md), [0115](../decisions/0115-native-obj-interchange-workflow.md), [0119](../decisions/0119-native-stl-interchange-workflow.md), [0123](../decisions/0123-native-dxf-interchange-workflow.md) | X02–X04 | Documented boundary. Checked at RC1: the 1.4.0 group adds `.skp` import (scope C01, [R102e](R102e-skp-import.md)), and the support matrix states it as import only. Checked at the 1.5.0 RC1: the 1.5.0 changes add a GeoTIFF elevation import (C06, [ADR 0181](../decisions/0181-maps-geolocation-feasibility.md)), which reads a raster into a terrain and is not a model format, and the `.skp` statement is unchanged. | Limitation | Losses are reported per conversion. DWG is a gated investigation, not Core, and `.skp` export is not claimed. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-016 | Blender handoff is one-way; animation exports PNG frames without movie encoding or resume | [user guide](../USER_GUIDE.md), [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), [M7](M7-accepted.md) | P03, P08, P09 | Documented boundary. Checked at RC1: the 1.4.0 presentation batch (F05, [R099e](R099e-presentation.md)) is a separate feature that writes PNG frames and an optional H.264 MP4 per preset. The animation export named here is unchanged. Checked at the 1.5.0 RC1: R099.f changes the render limits for terrains and large captures ([R099e §12](R099e-presentation.md)), not the handoff or the PNG frames, so the boundary stands. | Limitation | P09 requires the one-way behavior to be documented, and it is. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-017 | Finite planar/sweep geometry limits; retained internal faces may be non-manifold | [M3](M3.md), [M4](M4.md), [M2](M2.md) | G01, E01, E05 | Documented boundary. | Limitation | Explicit enforced limits with bounded rejection. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-018 | Remote assistant quality is probabilistic, and acceptance covers one configuration | [M5](M5.md), [R086.b](R086b-remote-evaluation.md), [R086.as](R086as-openai-inspection-batches.md) | A04, A05 | Documented boundary. Earlier failed campaigns (R086.ah, R086.ak) stay recorded; the current host passes 24/24. At the RC, [R086.ca](R086ca-rc-remote-evaluation.md) passes 24/24 with no blockers, a bounded result for the frozen tasks only. At the 1.4.0 RC1, [R086.h](R086h-rc140-remote-evaluation.md) passes 24/24 on corpus v6 with no blockers, again bounded to the frozen tasks. At the 1.5.0 RC1, [R086.i](R086i-rc150-remote-evaluation.md) passes 24/24 on corpus v6 with no blockers, again bounded to the frozen tasks. At the 1.6.0 RC1, [R086.j](R086j-rc160-remote-evaluation.md) passes 24/24 on corpus v6 with no blockers, again bounded to the frozen tasks. | Limitation | Disclosed in M5 and the support matrix. | [R086](../PR_ROADMAP.md#r086) |
| D-001 | Local provider configurations do not pass the frozen corpus (experimental by decision) | [R086.c](R086c-local-evaluation.md), [R086.ak](R086ak-remote-local-guidance.md), [R086.as](R086as-openai-inspection-batches.md), [M5](M5.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | A05 (also A08) | **Resolved by scope decision (2026-10-11).** The owner decided that a passing local configuration is not a 1.0 requirement ([ADR 0160](../decisions/0160-provider-scope-for-1-0.md)). The failures remain true and recorded: The frozen Ollama `qwen3:4b-instruct` profile scores 0/3 on all seven live tasks (20 of 21 reach the 300 s deadline). The later Qwen3-Coder campaign stops after 10 reports with room and resize failing. R086.as states that A05 still needs an accepted local configuration. No later record supersedes these. | Limitation | A05 now requires a passing remote provider only, and the remote OpenAI ChatGPT-plan profile passes the frozen corpus (24/24, [R086.b](R086b-remote-evaluation.md)). The local profile is experimental and labeled so. No safety blocker or false completion was observed. | [R086](../PR_ROADMAP.md#r086): the remote re-run at the RC passed ([R086.ca](R086ca-rc-remote-evaluation.md)); a local profile can be promoted only by a later decision |
| D-004 | Intel integrated reference GPU misses the 16.7 ms complete-frame target on 100k-triangle scenes under one CPU; not remeasured | [R082.i](R082i-complete-frame.md), [R082.j](R082j-viewport-reuse.md), [R082.an](R082an-public-memory.md), [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) | N01, D06 | **Waived, untested on Intel at the current revision (2026-10-12).** The latest Intel public campaign (R082.an, one CPU, Intel Arc/Wayland) missed the target in all five families: p95 17.3–19.6 ms. R082.j had passed all 20 Intel runs with an eight-CPU quota. The Intel rerun scheduled in [M9 hardware session](../M9_HARDWARE_SESSION.md) section 1 was not run. AMD passes every 100k target with a wide margin (viewport p95 2.1–2.6 ms, [R082.ii](R082ii-amd-hardware-session.md)). | Limitation | A known, recorded miss on the integrated GPU under a one-CPU quota, documented as untested for the current revision by the owner's waiver. Workflows function; only frame time is affected. The support matrix and release notes must not claim Intel performance. Reopen as Major if daily use on the Intel laptop shows slow frames. | [R082](../PR_ROADMAP.md#r082): none for 1.0; remeasure if Intel is promoted to a supported claim |
| D-005 | 282 ms opening heartbeat gap on Intel/Wayland not remeasured after the progress fix | [R082.m](R082m-native-file-timing.md), [R082.bb](R082bb-public-tooltip-progress.md), [R082.ak](R082ak-public-progress.md), [R082.ii](R082ii-amd-hardware-session.md), [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) | D02, N01 | **Unverified; Intel/Wayland rerun waived (2026-10-12).** R082.m retains 186–282 ms maximum open gaps on Intel Arc/Wayland. R082.bb's fix brought all 18 AMD/X11 gaps below 50 ms (worst 33.0 ms), and the 2026-10-11 AMD/X11 file campaign agrees: open gap 19.7–29.2 ms, save gaps up to 32.4 ms, every operation under 400 ms. No Intel/Wayland rerun exists. | Limitation | UI latency only, untested on Intel/Wayland at the current revision. Every open and save is within the five-second target. | [R082](../PR_ROADMAP.md#r082): none for 1.0 |
| D-020 | Weston 15.0.1 segfaults when a progress popup is created for a minimized window; Hyprland check waived | [R083.f](R083f-file-worker-hang.md), [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) | N01 | **Open (external); Hyprland check waived (2026-10-12).** Seen only on Weston 15.0.1, which is the isolated test compositor, not the primary Hyprland target. Other compositors were not tested. The check on Hyprland (a long open or save started, then the window minimized, before the progress popup appears; [M9 hardware session](../M9_HARDWARE_SESSION.md) section 8) was not run. | Limitation | An external compositor crash, not shown on the primary target, and untested there by the owner's waiver. The owner will see it in use if it occurs. | [R085](../PR_ROADMAP.md#r085) |
| D-029 | PKGBUILD dependencies carry no versions | [R087.u](R087u-clean-machine-packages.md), [R088.d N02](R088d-core-parity-gaps.md#row-status), [release notes](../RELEASE_NOTES.md#install-and-upgrade) | N02 | **Open by design for 1.0.** pacman installs a package built against newer Qt or glibc on an older system, where both executables fail to start (`Qt_6.11` and `GLIBC_2.43` symbol versions not found in R087.u). The release notes tell users to install a package built for their repository snapshot. At the 1.5.0 RC1 the PKGBUILD changed only `pkgver`, so the row stands. | Limitation | Documented in the release notes. Arch does not support partial upgrades, so it matters only when a package is installed outside its snapshot. The RC package installs and runs on the pinned and fresh images, and a package built from the RC archive runs on the Qt 6.8.0 reference. | [R087](../PR_ROADMAP.md#r087): versioned lower bounds for Qt and glibc after 1.0 |
| D-048 | An edit on a 100,000-face model holds the UI thread for about 140 to 160 ms in the `Viewport::changed` slots, and the Outliner rebuilds every row on each edit | [R101.f](R101f-quantities-reports.md) finding 22; this record's measurement (`edit_latency_benchmark`, `tests/edit_latency_benchmark.cpp`) | N01 (native viewport), O04 (Outliner and tags), F07 (Quantities section); [routine-edit budget](../BUILD_PLAN.md#initial-performance-budgets) | **Fixed in PR #498 (merged 2026-10-27; branch `fix/d048-outliner-rebuild`).** See "Closed by the D-048 fix" below. Was: Release build (`CMAKE_BUILD_TYPE=Release`, `CMAKE_LINKER_TYPE=BFD`), offscreen, section hidden. One placement moves per edit; 20 edits after one warm-up; load average 2 to 6 from other workers. **99,996 faces (8,333 placements):** the edit and the synchronous slots p50 139.7 ms, p95 151.9 ms, max 157.3 ms; an earlier session gave p50 157.0, p95 168.6, max 169.6 ms. The transform alone is 0.23 ms. **49,992 faces (4,166 placements):** p50 66.6, p95 67.9, max 72.0 ms (earlier session: 71.5, 76.8, 81.3). **Section shown** (99,996 faces): p50 175.3 ms. **Per slot,** mean per edit at 99,996 faces, from temporary timers that are not committed: Outliner tree rebuild 115 ms (74%); `filter()` 21 ms (14%); the inference snapshot copy in `Viewport::refresh` 11.5 ms (7%); `TextPanel::refresh` 3.4 ms inside the Outliner call and 3.9 ms in its own slot (5%); every other slot and `Window::sync` step under 1 ms in total. At 49,992 faces the same steps take 48, 6 and 5.8 ms, so the three largest costs scale with the record count (24,999 records here), not with the edited body. The Outliner is rebuilt with no visibility check: with the section shown, its tab is not current ("outliner on screen: no") and the rebuild costs the same. Not measured: the repaint and the viewport's mesh rebuild. They run in paint, not in the slots, and offscreen the viewport's GL is not ready, so that path returns before drawing. Scope of the claim: the synchronous slots only, on this machine, not on the declared reference hardware | Major (reclassified 2026-10-27 by the parent; was Minor, provisional) | A performance miss against the routine-edit feedback budget (p95 below 100 ms) in the real window, the budget D-024 missed at 10,000 placements, which was Major. No wrong result and no data effect | Fixed by PR #498, step 1: the Outliner and Tags rows are compared with the last build, and a tree is rebuilt only when they differ (see the closure section). Not implemented, because step 1 meets the budget (p95 43 ms at 8,333 placements), and kept as proposals: (2) defer the Outliner while it is off screen, as the components panel does (its gain is not measured); (3) an empty-search fast path in `filter()` (about 20 ms at 99,996 faces; low risk); (4) a lazy inference snapshot (about 11 ms; medium risk); (5) time the repaint on a GL-capable run |
| D-049 | The IFC helper reads the library's log while worker threads append to it, so four-thread imports can differ | [R097f](R097f-ifc-exchange.md#determinism-re-run-item-8-2026-10-28); [ADR 0171](../decisions/0171-ifc-exchange-subset.md) (implementation notes, four-thread determinism) | F04 (IFC import) | **Fixed 2026-10-27** (PR #495, merge `461e2de8`). Was: `iteratePass` (`src/ifc/geometry.cpp`) read the library's messages after `initialize()`, while worker threads appended to them. This is a data race, not a use-after-free. At the default four threads it changed only the report, in 53 and 104 of 3,430 runs (the kernel messages, and once the reason for an unapplied opening). At three threads it changed the geometry in 15 of 2,200 runs, losing one opening element. After the fix, 1,600 runs per fixture at four threads and 1,600 at three gave identical packages. | Minor | The shipped default changed only the report, and no model change was seen there. The three-thread case needs a caller to pass that count: the application caps the count at four and re-runs a failed import on one thread. Classed Minor by judgement, since a C++ data race is undefined behaviour | Fixed (PR #495): the log is read only where no worker can write, and it is sorted at more than one thread |
| D-050 | A STEP solid's free surfaces are tessellated into the good body, while the notice says they were imported as a body of their own | [R098d](R098d-step-exchange.md#7-findings-for-the-owner) finding 11; [§10](R098d-step-exchange.md#10-r098f-the-three-step-import-defects-2026-10-28) | F04 (STEP import) | **Fixed 2026-10-28** (PR #510, merge `fafc0431`). Was: `src/ifc/step_import.cpp` tessellated a part's whole shape. The six free faces of the designed bad-face fixture came back in the good box's body (12 faces, 24 triangles), and `elements.notImported` was empty. | Major | A wrong result in a supported import: the body contains faces that the report says were not imported. The workaround is to remove the free surfaces from the source file | Fixed (PR #510): a part with surfaces is tessellated from its solids only; its surfaces are counted and listed in `elements.notImported` |
| D-051 | The STEP import report names no location for the parts it leaves out | [R098d](R098d-step-exchange.md#7-findings-for-the-owner) finding 12; [§10](R098d-step-exchange.md#10-r098f-the-three-step-import-defects-2026-10-28) | F04 (STEP import) | **Fixed 2026-10-28** (PR #510). Was: `step_import.cpp` never called `Report::locate`, so the report held counts only, against ADR 0172 §5. Every outcome that leaves a part out is now located (32 per kind, totals complete), with its `#` label, reason and assembly path. | Minor | The omitted parts were counted but not named. The model was unaffected | Fixed (PR #510) |
| D-052 | A PMI-only AP242 file is refused without naming or counting its entities | [R098d](R098d-step-exchange.md#7-findings-for-the-owner) finding 13; [§10](R098d-step-exchange.md#10-r098f-the-three-step-import-defects-2026-10-28) | F04 (STEP import) | **Fixed 2026-10-28** (PR #510). Was: the refusal (`nothingToImport`) named no entity and counted none. The refusal stays. It now names and counts, by STEP kind and with `#` labels, the entities the import does not read (the designed PMI fixture: five kinds, one each, five locations). | Minor | The refusal was right, and its message gave no reason. No document changed | Fixed (PR #510) |
| D-053 | A STEP file that converts reports nothing for its PMI and GD&T | [R098d §11](R098d-step-exchange.md#11-r098g-the-two-step-reporting-defects-2026-10-28) | F04 (STEP import) | **Fixed 2026-10-28** (PR #511, merge `e12b3a45`). Was: the PMI count ran only on the refusal path. `tallyPmi` now counts the 66 AP242 PMI kinds by kind, locates each one, imports none, and sets `pmiNotImported`. The converting fixture gives 8 kinds, 41 entities and 32 located. | Minor | The geometry converted correctly. The report left out PMI that the import does not read (ADR 0172 §§2 and 5) | Fixed (PR #511) |
| D-054 | A STEP file deeper than the document's depth limit drops parts without a count, and the limit is one level too deep | [R098d §11](R098d-step-exchange.md#11-r098g-the-two-step-reporting-defects-2026-10-28) | F04 (STEP import) | **Fixed 2026-10-28** (PR #511). Was: `treeTooDeep` returned at its limit without recording the entry, and its notice said the parts were attached. The limit was also one level deeper than the document accepts: a body at depth 127 has 129 records, and the document refuses more than 128. The limit is now 126, and each entry below it is counted and located with reason `tree-too-deep`. | Minor | Only assemblies about 127 levels deep are affected. The refusal was a clear error; the silent drop was the defect | Fixed (PR #511) |
| D-055 | Start refuses a presentation batch with the wrong reason after the Blender has changed | [R099e](R099e-presentation.md#findings) O6 | F05 (presentation batch) | **Fixed 2026-10-28** (PR #501, merge `8268248d`). Was: `startBatch` ran `startProblem` before its re-check. With no cached answer for a changed Blender, `videoAvailable` was false, and Start refused with "H.264 is not in this Blender's FFmpeg build". No batch started. Start now re-checks the encoder first and says so, and a refusal names the codec the batch chose. | Major | A broken workflow with a workaround: a batch with video could not start after its Blender was replaced. The workaround is to change the Blender before opening the batch dialog. No frame or document was affected | Fixed (PR #501) |
| D-056 | Identical presentation batches write PNG frames with different bytes | [R099e](R099e-presentation.md#findings) C3 | F05 (presentation batch) | **Fixed 2026-10-28** (PR #501). Was: Blender wrote a `Date` and a `RenderTime` into each PNG's text chunks, so the bytes differed between identical batches, against a spec that asked for identical bytes. Pixels, decoded video and planned poses were already identical. The worker now turns the stamp off and strips the PNG text chunks (`blender_worker.py`). | Minor | A spec mismatch in metadata; no pixel changed | Fixed (PR #501) |
| D-057 | A presentation request over 16 KiB is reported as an input error, not a limit | [R099e](R099e-presentation.md#findings) C5 | F05 (presentation CLI) | **Fixed 2026-10-28** (PR #501). Was: `sketchyup-cli --export-presentation` returned `INPUT_ERROR` for an over-size request, while R099.b documents `LIMIT_EXCEEDED`. An over-size request is now `LIMIT_EXCEEDED`, and an unreadable request stays `INPUT_ERROR`. | Minor | The wrong error code only; nothing ran | Fixed (PR #501) |
| D-058 | A render still running shows as Failed in the jobs list | [R099e](R099e-presentation.md#findings) (R099.e2 gaps); [ADR 0100](../decisions/0100-render-queue-lifecycle.md) | F05 (render queue) | **Fixed 2026-10-28** (PR #506, merge `86070665`). Was: `RenderQueue::result()` on a running job made `jobs()` show it as Failed, and its entry outlived the job (`render_queue.cpp`). Asking for the result early is now refused, and the render keeps its state. | Minor | A wrong status in the list; the render itself was not affected | Fixed (PR #506) |
| D-059 | An MPEG-4 Part 2 batch refusal names H.264 | [R099e](R099e-presentation.md#findings) (R099.e2 gaps) | F05 (presentation batch) | **Fixed 2026-10-28** (PR #506). Was: a batch that chose MPEG-4 Part 2 and was refused for its codec was told "H.264". `presentationVideoRefusal` names the chosen codec, and the text is tested for both codecs. | Minor | Wrong text only; no output was affected | Fixed (PR #506) |
| D-060 | A presentation frame's record holds the planned camera, not the camera Blender rendered | [R099e](R099e-presentation.md#findings) (R099.e2 gaps) | F05 (presentation batch) | **Fixed 2026-10-28** (PR #506). Was: the worker reported no camera, so the batch could not compare the rendered pose with the plan. Each frame's record now holds the rendered camera, and the batch checks it to 1 µm, through the stand-in and with real Blender. | Minor | A verification gap; no wrong frame was observed | Fixed (PR #506) |
| D-061 | Batch capture of a 250,000-triangle terrain document takes 1.7 s against a 500 ms budget | [R099e](R099e-presentation.md#findings) (budget row, R099.b) | F05 (budget) | **Fixed 2026-10-28** (PR #500, merge `71de3fc8`). Was: p95 1,747 ms on the 249,218-triangle terrain (budget 500 ms). The encoder now skips the terrain JSON it discards, in capture and save. p95 is 160 to 183 ms on the same document. | Minor | A performance miss on a one-time cost per batch, before the first frame. It is now within budget | Fixed (PR #500) |
| D-062 | The catalog dialog's dry run freezes the UI on each selection in a large opened catalog, and the test process peaks at 1.6 GB | [R100g](R100g-custom-styles-assets.md#8-findings) F-1 and F-3 | F06 (asset catalogs) | **Fixed 2026-10-28** (PR #504, merge `5890639b`). Was: `refreshInsert` ran on the UI thread and base64url-encoded the whole opened catalog into each dry run, which the engine decoded again. On a 128 MiB catalog R100.g measured a selection at 2.13 s (p50) and the fix's before-run at 4.8 to 5.3 s; the library-folder form took 0.30 s. The test process peaked at 1.63 GB. The dialog now passes the catalog it listed, under a key only the dialog can send. An opened-file selection takes 90 to 106 ms (p50), and the process peaks at 496 MB. | Major | A broken workflow with a workaround (keep the catalog in the library folder): seconds of UI freeze in a supported dialog, and a memory peak over the R082 1 GiB class | Fixed (PR #504) |
| D-063 | Blender stand-in suites fail on shared-library CI builds | [ADR 0183](../decisions/0183-helper-process-sandbox.md) (sanitized-build notes, 2026-10-27) | R109 (CI, test infrastructure) | **Fixed 2026-10-28** (PR #505, merge `c0e2f64f`). Was: the Blender policy granted read access to the build tree only in sanitizer builds. The fast tier links shared project libraries, so every Blender stand-in suite failed there from #502 onward. The grant now follows `SKETCHYUP_SHARED_PROJECT_LIBS`. Release and package builds are static and unchanged. | Minor | CI-only, on `main` until #505; no shipped build was affected | Fixed (PR #505) |
| D-064 | The IFC helper's sanitizer build is killed by its seccomp policy, which turned main's CI verdict red | [R100g](R100g-custom-styles-assets.md#8-findings) F-7 | IFC helper (sanitizer CI, test infrastructure) | **Fixed 2026-10-27** (PR #496). Was: after the #485 merge, `ifc_worker_probe` was killed with SIGSYS in the sanitizer IFC build, because the helper's sanitizer-only calls used `sched_yield`, which the policy refuses. R100.b2 changed no IFC code. | Minor | CI-only, and in sanitizer builds only; `main`'s verdict was red until #496 | Fixed (PR #496) |
| D-065 | The `.skp` helper is killed by its own sandbox when stdout is a character device such as `/dev/null` | [R102e](R102e-skp-import.md#6-defects-and-contradictions-found) F2; [§11](R102e-skp-import.md#11-r102f-closing-the-findings) | C01 (`.skp` import) | **Fixed 2026-10-28** (PR #513, merge `2326adfe`). Was: glibc's stdio issues `ioctl(TCGETS2)` on a character device to test for a terminal. The SKP policy had no `ioctl` rule, so the default kill applied (SIGSYS). The policy now allows `ioctl` with `TCGETS` and `TCGETS2` only. | Minor | Developer probes and any caller that sends stdout to `/dev/null`. The application and the CLI use pipes | Fixed (PR #513) |
| D-066 | `.skp` import hashes its input in quadratic time | [R102e §11.1](R102e-skp-import.md#111-findings) F7; [§11.3](R102e-skp-import.md#113-runs) | C01 (`.skp` import) | **Fixed 2026-10-28** (PR #513). Was: `Sha256::update` erased each 64-byte block from the front of a buffer that held the whole input, so the hash, and so the import, was quadratic. `skp_package` took 148 s before the fix and 0.15 s after. The owner's file imports in the same time as before, within noise. | Minor | Performance: a large `.skp` could take long enough to reach the helper's 120 s deadline. No user file was shown to do so | Fixed (PR #513) |
| D-067 | The `.skp` "newer than tested" notice fires one major version late | [R102e §6](R102e-skp-import.md#6-defects-and-contradictions-found) F1; [§11.1](R102e-skp-import.md#111-findings) | C01 (`.skp` import) | **Fixed 2026-10-28** (PR #513). Was: the check was `major > 25`, so 2025 files had no notice, while ADR 0176 §2 applies the label above the tested corpus (2024). It now applies above 24. The owner has not separately confirmed the threshold ([R102e §12](R102e-skp-import.md#12-owner-decision-2026-10-28)). | Minor | A missing diagnostic for 2025 files; no import was wrong | Fixed (PR #513) |
| D-068 | The Quantities Tags popover prints a Qt Wayland popup warning | [R101f](R101f-quantities-reports.md#7-gaps) gap 8 | F07 (Quantities panel), Wayland test | **Fixed 2026-10-30** (PR #527, branch `fix/d068-d069-warnings`). The popover's transient parent was already the panel's window. The grab also needs an input serial, which synthetic test input cannot supply. On Wayland the test now fills the popover through `buildPicker` instead of showing it. The viewport context menu prints the same message ([R095.f](R095f-terrain.md)) and is unchanged. Was: Both Wayland cells print `qt.qpa.wayland: Failed to create grabbing popup … quantitiesTagsPopupWindow`. Qt says the popup needs a transient parent and a parent window that has received input. The cause in the Tags popover was not investigated. The test passes. | Minor | A warning on Wayland; the test passes, and the record shows no effect | R101 follow-up (not yet planned) |
| D-069 | Two compiler warnings in the desktop build | [R101f](R101f-quantities-reports.md#7-gaps) gap 9 | F07 (Quantities panel) | **Fixed 2026-10-30** (PR #527, branch `fix/d068-d069-warnings`). The dev build and the IFC helper also reported 32 sites in `src/` and `tests/` and 8 AutoMoc notes, and the Qt 6.8 and Clang CI jobs reported seven more; all are fixed. The `quantities_input_tests.cpp` call at that line uses its result and did not warn, so it did not change. Was: `src/app/quantities_panel.cpp:882` (missing field initializers `areaText` and `volumeText`) and `tests/quantities_input_tests.cpp:100` (the `qWaitFor` result is ignored). Found in CI logs; the record calls them cheap to fix. | Minor | Compiler warnings only; no wrong behaviour is recorded | R101 follow-up (not yet planned) |
| D-070 | `skp_import_dialog_tests` missed the progress sheet's read step once on Wayland 2× at the 1.4.0 RC1 | [RC1 full tier run 40801765717](https://github.com/zygote55/sketchyup/actions/runs/40801765717) (attempt 1, job 123225428669, `9ed43c90`) | C01 (`.skp` import dialog), test harness | **Fixed 2026-10-29** (PR #519). Was: Case S8 (a full import) failed one check, "FAIL: the read step was the working one while the helper ran", in `SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/skp_import_dialog_tests`. No other check in the suite failed. The driver polls the progress sheet every 20 ms and must see `skpProgressStep2State` read "●" while the stand-in helper runs for about 0.3 s (`importBody(package, "0.3")` in `tests/skp_import_dialog_tests.cpp`); on a loaded runner no poll landed in that window. The rerun of the job (attempt 2) passed, and the same line passed on the full tier of PR #515 (run 40790714698). | Minor | A sampling window in the test: no product check failed, and the import itself completed | Fixed (PR #519): the stand-in holds its read step until the driver has seen it working (the driver makes a gate file). The STEP and IFC import suites, which sampled the same way, get the same hold |
| D-071 | The release notes' rollback fails while `sketchyup-ifc` is installed | [G-1.4.0](G-1.4.0.md#triage-at-the-140-rc) (the 1.4.0 RC walkthrough, 2026-10-29) | Packaging, release notes (rollback) | **Fixed 2026-10-29 on `main`** (the PR that records the 1.4.0 publication: `RELEASE_NOTES.md` now says to remove `sketchyup-ifc` first; the GitHub release's notes carry the same step). The 1.4.0 package's installed notes keep the old text, because they are fixed at the RC. Was: `docs/RELEASE_NOTES.md` 1.4.0 says to roll back with `pacman -U sketchyup-1.3.0-1-x86_64.pkg.tar.zst`. With `sketchyup-ifc` 1.4.0-1 installed, which requires `sketchyup=1.4.0`, pacman refuses: "installing sketchyup (1.3.0-1) breaks dependency 'sketchyup=1.4.0' required by sketchyup-ifc", exit 1, and both packages stay at 1.4.0-1. After `pacman -R sketchyup-ifc` the same command installs 1.3.0-1. Checked offline on the pinned image with the RC packages. | Minor | A missing documentation step: pacman refuses the whole transaction, so nothing is changed or lost, and its message names the package to remove | Release notes on `main` after 1.4.0, and the GitHub release's notes at publication (the installed notes are fixed at the RC) |
| D-072 | The sanitized native MCP step leaks a 96-byte `wl_proxy` at exit in `style_catalog_input_tests` | [Full-tier run 40944129473](https://github.com/zygote55/sketchyup/actions/runs/40944129473) (job "Sanitized builds", step "Sanitized native MCP inspection"); the leak appeared after PR #520 (R100.h) and PR #521 (R100.i) merged without the full tier | Wayland test (`style_catalog_input_tests`, the 200 % catalog window in `largeText`), test teardown | **Fixed 2026-10-29** by this PR (branch `fix/d072-wayland-leak`). Was: LeakSanitizer failed the step at exit after every case had printed "ok": `Direct leak of 96 byte(s)` from `wl_proxy_create_for_id` via `queue_event`. The 200 % window closes and Weston moves keyboard focus to the main window with a keyboard enter and a clipboard `data_offer`, a new-id proxy. A Qt worker thread read those events into the default queue, nothing dispatched them, and at disconnect libwayland drops the closure's reference but not the proxy's own, so the proxy leaks. Reproduced in the CI image: 9 of 10 runs before the fix. | Minor | A leak at test-process exit that only the sanitizer reports; no product behaviour is wrong | Fixed (test): `style_catalog_input_tests` waits for a `wl_display_sync` answer on the default queue before exit, which dispatches the events the compositor sent earlier (Qt's `QGuiApplication::sync` is not a round trip on Wayland). No suppression. 10 of 10 CI-image runs pass after the fix |
| D-073 | The Asset catalogs insert could wait forever when the insert finished at once | [PR #527 run 41025723475](https://github.com/zygote55/sketchyup/actions/runs/41025723475) (Native tests: Wayland 2×, `style_catalog_input_tests` timed out at 60 s in `refusedInsertReport`) | F06 (Asset catalogs dialog), since R100.i (#521) | **Fixed 2026-10-30** (this branch, `fix/d073-catalog-insert-race`). Was: `CatalogDialog::insertSelected` started the `ExchangeRunner` thread and only then connected `QThread::finished` to `insertFinished`, then waited in a nested event loop. `QThread::finished` is emitted by the worker thread as it exits, so an insert that ended at once (a refused read) could exit before the connection existed; nothing settled the insert, and the dialog stayed at "Inserting…" with the loop never quitting. The hung run's stack: `insertSelected` in `QEventLoop::exec`, no runner thread left. The other exchange dialogs listen to `ExchangeRunner::runFinished`, which the runner posts to the UI thread, so they cannot miss it. Fix: the dialog connects `runFinished` too. | Major | A hang in a workflow (the insert never settles); found by a flaky CI timeout, with no data loss | Fixed |
| D-074 | A GeoTIFF terrain import never ended when the window was narrower than the chosen spacing | [ADR 0181 implementation notes (R107.b3)](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b3) (found by reading the R107.b1 decimation loop) | R107 (DEM import, command line; not in a release) | **Fixed 2026-10-30** (PR #534). Was: `readGeoTiffTerrain` (R107.b1, PR #529) raised the spacing one metre at a time until the grid fitted the terrain limit; a window narrower than two posts at that spacing never fitted, so `sketchyup-cli --import-terrain … --dem-window` with a narrow window looped forever. Now the search is bounded by the widest spacing that leaves two posts across, and a narrower window is refused by name (`window-small`). | Major | A hang reachable from the command line; found by review before any release carried it, with no data loss | Fixed |
| D-075 | Editing a large component fails on the unique-geometry limit, and an explode or make-unique leaves the replaced definition counted | Owner's model, 2026-10-30 (80,203 unique vertices; definition 3 with 27,766): [ADR 0005 implementation notes (D-075)](../decisions/0005-document-protocol.md#implementation-notes-d-075) | Components (ADR 0005, ADR 0165): `component.edit`, `component.make_unique`, `group.explode`, in the CLI and the desktop | **Fixed 2026-10-30** (PR #540). Was: the component edit's draft counted the edited definition twice, so the owner's model failed with "vertices would be 107,664 of 100,000", and a 60,000-vertex definition could not take a one-member move. Exploding the living room counted 107,969 instead of 80,203, because the replaced definition's record stayed in the document. The draft is now a component draft: it counts its plain bodies, and the real document's check still counts every record on publish. An explode or make-unique removes a definition that its edit leaves with no placement and nothing referring to it, in the same undoable edit. | Major | A supported workflow is blocked: a model near the unique-geometry limit cannot have a large component edited, and an explode or make-unique reaches the limit early | Fixed (PR #540): the cause, the rule and the tests are in [ADR 0005](../decisions/0005-document-protocol.md#implementation-notes-d-075) |
| D-076 | A real USGS 1/3 arc-second GeoTIFF tile is refused: the post-step tolerance is tighter than the file's stored step | [ADR 0181 implementation notes (R107.b1.1)](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b11) ("Contradictions with the ADR") | R107 (GeoTIFF elevation import, C06; not in a release) | **Fixed 2026-10-30** (PR #530, merge `28f7e008`, commit `4845f0be`). Was: `src/io/geotiff_terrain.cpp` compared each post step with its nominal value within 1e-9 relative, a bound that PR #529 introduced. USGS writes its 1/3 arc-second step as `9.25925927753796e-05`, 2e-9 from 1/10800, so the real tile was refused. The bound is now 1e-6 relative. The fixture `dep13-usgs-scale-f32.tif` and the test `USGS 1/3 arc-second step` cover it. | Major | A step that ADR 0181 §7.2 and §2.8 include could not be imported as distributed. The 1 arc-second tile, which passed, was the workaround. The file was refused by name, so no data was changed. Never in a release. | R107 (GeoTIFF reader), R107.b1.1 |
| D-077 | A GeoTIFF import into a model with other terrains chose its spacing without the room those terrains take, so a window could be refused when a coarser spacing would have fitted | [ADR 0181 implementation notes (R107.b, UX design)](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b-ux-design) (gap 3); [R107.b3](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b3) | R107 (GeoTIFF elevation import, C06; not in a release) | **Fixed 2026-10-30** (PR #534, merge `aace26f9`, branch `feat/r107b3-dem-engine`). Was: `readGeoTiffTerrain` decimated each grid to one terrain's limit, and the model's total was checked only after the spacing was chosen (`triangle-budget`). The dry run therefore did not count the room. Fixed: `GeoTiffTerrainOptions::otherTerrainTriangles` carries that room into the spacing, and the notice names it when the room sets the spacing. | Major | A wrong dry-run result, and a refused window, for a model with other terrains. No data was changed. The workaround is an explicit spacing (`--dem-spacing`). Never in a release. | R107 (GeoTIFF engine), R107.b3 |
| D-078 | A DEM import set the site's coordinate-system label with no position behind it and wrote no geographic record, so the zone and vertical checks never ran for that site | [ADR 0181 implementation notes (R107.b, UX design)](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b-ux-design) (gap 4); [R107.b3](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b3) (item 6) | R107 (GeoTIFF elevation import, C06; not in a release) | **Fixed 2026-10-30** (PR #534). Was: `importGeoTiffTerrain` (`src/automation/terrain_commands.cpp`) set the site's CRS label (EPSG:326nn or EPSG:327nn) with no position behind it, and wrote no geographic record (ADR 0181 §2.1). Fixed: an import that establishes the origin writes the record at the origin's UTM position, and an origin outside the file's zone is refused (`origin-outside-zone`). | Major | The site's coordinate system was recorded without the zone and vertical checks, so an origin outside the file's zone was not caught. No data was lost. Never in a release. | R107 (GeoTIFF engine), R107.b3 |
| D-079 | The GeoTIFF import took the file's SHA-256 after it read the posts, so a file changed during the read could be recorded with the digest of bytes it did not read | [ADR 0181 implementation notes (R107.b, UX design)](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b-ux-design) (gap 5); [R107.b3](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b3) (item 7) | R107 (GeoTIFF elevation import, C06; not in a release) | **Fixed 2026-10-30** (PR #534). Was: `readGeoTiffTerrain` hashed the file after it read the posts, but ADR 0181 §3.2 needs the digest of the bytes that were read. Fixed: the reader records the file's size and modification time, hashes the whole file, reads the posts, and refuses the import (`file-changed`) if either has changed. A rewrite that keeps both is not caught, as R107.b3 records. | Minor | Only a file changed during the import could cause the mismatch. Before the fix the import went ahead and recorded the wrong digest. No data was lost. Never in a release. | R107 (GeoTIFF engine), R107.b3 |
| D-080 | The native dialog keyboard audit treated a radio group with no checked button as unreachable by Tab, though Qt enters such a group at its first button | [ADR 0181 implementation notes (R107.b4)](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107b4) ("Keyboard audit") | N05 (all-dialog keyboard audit) | **Fixed 2026-10-30** (PR #536, merge `46243fcb`). Was: `tests/native_dialog_audit.hpp` took such a group as unreachable. Fixed: the audit enters the group at its first button, as Qt does. The import group and the existing cases pass with the change. | Minor | A wrong finding from a test helper. No product defect is shown. Test infrastructure only. | N05 (native audit helper), R107.b4 |
| D-081 | A STEP export of the 90,954-face model could not be read back: its 224 MB file exceeded the application's 128 MiB input limit | [R098d §14.2](R098d-step-exchange.md#142-the-changes-and-what-each-did) (change 3) and [§14.8](R098d-step-exchange.md#148-findings-for-the-parent) | F04 (STEP exchange) | **Fixed 2026-10-29** (PR #524, merge `3b1fb961`, branch `perf/r098j-step-speed`). Was: the STEP writer kept each edge's surface curves (the `PCURVE`s, their `DEFINITIONAL_REPRESENTATION`s and the `SURFACE_CURVE`s), so the file was 224 MB and the application refused to read its own export (`LIMIT_EXCEEDED`). Fixed: the writer leaves them out (`WriteSurfaceCurMode`). The file is 100.6 MB and re-imports with the same faces, vertices, edges and non-planar face count. | Major | Shipped in 1.4.0. A model of that size could be exported but not read back by the application. The exported file was written, and no model data was lost. | R098.j (STEP export writer) |
| D-082 | On a large model every MCP and assistant transaction stage fails `STAGE_LIMIT` and every `snapshot.begin` fails `SNAPSHOT_LIMIT`, although the model is within the document limits | Peer session's report on the installed 1.5.0 (`sketchyup-cli --mcp`, a 27.9 MB model of 2,481 bodies with 30 assets), relayed by the owner on 2026-10-31 | MCP and assistant transactions; read snapshots ([ADR 0020](../decisions/0020-inspection-snapshots.md), [ADR 0022](../decisions/0022-private-staging.md)) | **Fixed 2026-10-31** in [PR #555](https://github.com/zygote55/sketchyup/pull/555). Cause, confirmed: `StagingSession::prepare` refused any proposal when the live model's logical size exceeded a third of the staging budget, and `PreparedEdit::retainedBytes()` and `snapshot.begin` charged `readSnapshotBytes()`, which counts every record at full size, though a proposal or snapshot shares its unchanged records with the live model. Tests: a 52 MiB synthetic model in `tests/staging_tests.cpp`, `tests/inspection_session_tests.cpp` and `tests/transaction_dispatch_tests.cpp`; the old code fails each of them. | Major | Every transaction fails on a model above about 42.7 MiB of logical size, and every snapshot above 32 MiB, both within the document limits. No data effect: each refusal happens before publication. | Fixed: a proposal or snapshot is charged for the records it does not share with the live model, checked at admission, and the caps are unchanged ([ADR 0022 implementation notes](../decisions/0022-private-staging.md#implementation-notes-d-082)). |
| D-083 | `geometry.delete` on a group erased only the group, so its children were left naming a parent that no longer existed | Peer report, 2026-10-31; [ADR 0005 implementation notes (D-083)](../decisions/0005-document-protocol.md#implementation-notes-d-083) | Geometry (`geometry.delete`, "Delete context"), in the CLI, the assistant and the API | **Fixed 2026-10-31** (PR #554). Was: `Document::erase` removed the group's record and nothing else, so the edit's validation failed with "Missing parent entity". Fixed: erasing a context removes its descendant bodies and their placements in one undoable edit, as erasing a selected context does. | Minor | Has a workaround: `geometry.erase_selection` with a `kind: "context"` entity. The failed command published nothing. | Geometry (ADR 0005, D-083) |
| D-084 | A placement made inside a component definition edit names a parent the edit does not show, and the refusal is a raw map error | Peer report, 2026-10-31; [ADR 0165 implementation notes (D-084)](../decisions/0165-constrained-component-parameters.md#implementation-notes-d-084) | Components (`component.instance` inside `component.edit`) | **Fixed 2026-10-31** (PR #554). Was: `placeComponent` looked the parent up before checking that the edit held it, so an ID outside the edit (a scene ID in a definition-only edit, for example) failed with "map::at". Fixed: such an ID is refused by name, "Placement parent N is not in this edit", and publishes nothing. A member of the edited definition and "0" for its root were already accepted; the peer's "Missing parent entity" for those two forms did not reproduce (ADR 0165, D-084). | Minor | Has a workaround: place without `parent`, which lands at the definition's root, then `scene.reparent`. | Components (ADR 0165 §4, D-084) |
| D-085 | A `--script` batch's `catalog.import` with `library` could not find a catalog that `--export-catalog --to-library` had written | Peer report, 2026-10-31; [ADR 0174 implementation notes (D-085)](../decisions/0174-custom-styles-asset-authoring.md#implementation-notes-d-085) | Catalogs (`catalog.import`), `sketchyup-cli --script` | **Fixed 2026-10-31** (PR #554). Was: only some CLI operations named the application's data folders before they read them. A `--script` batch used the executable's own folder, so `catalogLibraryPath` reported "No catalog with that name is in the catalog library". Fixed: the CLI names the folders once at startup, so every operation reads the folder the desktop application uses. | Minor | Has a workaround: `--import-catalog FILE --entries NAME`, which reads a catalog file by path. | Catalogs (ADR 0174 §7, D-085) |
| D-086 | The durable outcome store cannot commit a model whose saved container is over about 32 MiB: its 64 MiB budget holds both the before and the after container | [ADR 0022, D-082 notes](../decisions/0022-private-staging.md#implementation-notes-d-082) | MCP and assistant transactions at commit (`OUTCOME_LIMIT`); durable history | **Open (2026-10-31).** `encode` in `src/io/outcome_store.cpp` (lines 116–118) requires the record and both containers within `hardLimit`, 64 MiB. A saved container above about 32 MiB therefore fails at commit with `OUTCOME_LIMIT`. D-082 moved the refusal from staging to commit for such a model: on the 52 MiB synthetic model in ADR 0022, sealing a draft succeeds and the durable commit fails. The ADR's figure is arithmetic, not a measurement. | Limitation | No data effect: the refusal happens before publication. Raising the durable budget changes a storage budget, so it is an owner decision, as ADR 0022 notes. | Open: owner decision whether to raise the budget |
| D-087 | A retained render re-rendered from a manifest written before the in-view credit rule can burn a terrain's source notice for a terrain outside its view | [ADR 0181, R107.c3.1 notes, item 3](../decisions/0181-maps-geolocation-feasibility.md#implementation-notes-r107c31) | R107 (DEM source credit): re-rendering a retained render whose manifest has `credits` but no `stampCredits` (1.5.0 renders, and stills prepared between R107.c3 and R107.c3.1) | **Open (2026-10-31).** The record says a re-rendered still "from before this change can carry a notice for a terrain outside its view" and calls it "a known gap". It is not built. The retained manifest records the camera, so the view test could be run again from the stored files. | Minor | The owner's rule is that a terrain's notice is burned only in its view. The gap adds an attribution line to a re-render. It does not change geometry or data, and a fresh prepare applies the rule. | Open: needs a decision on re-running the view test for retained manifests |
| D-088 | `sketchyup-cli --version` printed "SketchyUp 1.6.0" instead of "sketchyup-cli 1.6.0" | The 1.6.0 RC1 local package acceptance (2026-10-31): `verify-package.sh` stopped at its `sketchyup-cli --version` check; a regression from PR #554 (D-085) | CLI (`src/automation/main.cpp`), since #554; never in a release | **Fixed 2026-10-31** (branch `fix/d088-cli-version`). Was: D-085 set the application name to "SketchyUp" at startup for every operation, so that every operation reads the same data folders, and the `--version` option printed Qt's own text, the application name and version. The CLI now prints "sketchyup-cli VERSION" itself, and `native_format_tests` checks the exact output (it had checked only the ending). #554 ran the fast tier, which has no package check. | Major | A documented command-line output changed, which breaks the package check and any script that reads the version; found at the release candidate, before any release | Fixed: RC1 is replaced by RC2 |

## Candidates checked and not carried forward

These were recorded as defects or failures and are closed on `main` by later evidence:
the R082.m heartbeat on AMD (R082.bb); the eight R084.a naming findings (R084.b, R084.o);
theme persistence (R084.c); dark-theme link contrast (R084.i); X11 Outliner rename
(R084.k); the AT-SPI selection label (R084.l); filtered shortcut selection (R084.m);
unnamed provider selectors (R084.n); camera-rounding self-occlusion (R082.g);
incomplete DRM aggregation (R082.w); the decoded-image publication race
([CI texture publication](CI-texture-publication.md)); the `render_input_tests`
launch-proof race (atomic write in #247; both failures predate it); the
`assistant_panel_tests` asynchronous deadline (mock fix on `main`); CI disk exhaustion
([artifact reclamation](CI-development-artifact-reclamation.md)); the six-hour package
timeout ([package budget](CI-PR202-package-budget.md)); and M8's cancelled `main`
workflows (#266). The `main` run for #230 (38011062071) failed on the
`render_input_tests` race above; the run for #247 (38011483053) is D-007.

Closed by the 2026-10-12 hardware session:

- **D-003** (10,000-instance, 1,000,000-triangle fixture; hardware acceptance of the R082.w v2 budget).
  The AMD run ([R082.ii](R082ii-amd-hardware-session.md)) exits 0 for all six 10,000-placement
  cases: repeated, deep, textures and far. Peak RSS is 616–759 MB against the 1.25 GiB class,
  sampled resident growth at most 3.3 MB (64 MiB), retained history 1.6 MB (64 MiB), the DRM
  resident upper bound at most 469 MB (2 GiB), with complete DRM accounting, and complete-frame p95
  17.3–21.7 ms against 33.3 ms. Far at 10,000 placements now has a recorded run. Intel reruns and the
  sharing-off hardware control are waived ([ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)),
  so the budget is hardware-accepted on AMD only. The inference-update growth with placements
  noted under D-003 showed up as the edit-feedback miss D-024, fixed by R082.jj (below).

Closed by R090.e:

- **D-030** (`annotation.create` always rejected through `transaction.apply`). Class: **Major**
  (a broken workflow with a workaround). Found 2026-10-14; present since the command was added
  (`7d3ba13b`, 2026-10-06) and in the 1.0.0 release. The shared schema validator
  (`src/automation/inspection.cpp`) applied `properties`, `required` and the length and item
  bounds only under an explicit `type`. The two `oneOf` choices of the `annotation.create`
  schema (distance or label) have no `type`, so both matched every request and the
  "exactly one" count failed. Every `annotation.create` through a headless `--session`,
  stdio MCP `transaction.apply` or a recipe step returned `INVALID_REQUEST` "Parameter must
  match exactly one supported schema"; the 1.0.0 CLI and the R090.d build both did. The
  batch path (`--script`, batch requests) and the desktop Annotations panel do not use this
  validator and were unaffected; the desktop assistant does not offer annotation commands.
  No other shipped schema has a `oneOf` choice without `type` that constrains anything.
  Released as a fix for users in [1.0.1](https://github.com/zygote55/sketchyup/releases/tag/v1.0.1)
  (2026-10-14, `release/1.0.x`; release check [R089.g](https://github.com/zygote55/sketchyup/blob/v1.0.1/docs/verification/R089g-1.0.1-patch.md)).
  Fix: a schema without `type` now applies its keywords to the kind of value it meets, as
  JSON Schema does. `transaction_dispatch_tests` covers typeless `oneOf` choices directly
  and the real `annotation.create` distance and label requests through `transaction.apply`,
  including three that must still be rejected ([R090.e](R090e-drawing-sheets.md)).

Closed by the D-021 fix (2026-10-17, branch `test/wayland-activation-exposure`):

- **D-021** (Wayland test-window exposure timeouts under host load). Class: **Minor** (test
  infrastructure). **Cause.** Two behaviours meet in the isolated session.
  (1) Weston draws nothing, and so sends no frame callback, until its `weston-desktop-shell`
  helper is up: the desktop shell marks the output ready only when the helper commits the
  background (`background_committed` → `weston_output_set_ready` in `desktop-shell/shell.c`;
  `weston_output_schedule_repaint` returns while the output is not ready), and until the helper
  reports `desktop_ready` (or 15 s pass) an opaque startup curtain covers the output, so every
  window under it counts as occluded and gets no frame callback (`weston_output_repaint`,
  `libweston/compositor.c`, Weston 15.0.1). The helper is a separate process that paints a
  full-output background; at Wayland 2× the headless output is 6400×4000 pixels. On a loaded
  host it took 8–20 s. (2) Qt's Wayland client marks a window unexposed when a frame callback is
  more than 100 ms late (`QWaylandWindow::mFrameCallbackTimeout`, Qt 6.11.2), a heuristic for a
  compositor that stopped drawing a hidden window. So the test window was exposed when
  configured, unexposed 100 ms after its first frame, and stayed so until the curtain lifted;
  `qWaitForWindowExposed()` (5 s) passed only if it sampled the window inside those 100 ms.
  The failure needs no product code and no test step, which matches every record above.
  **Evidence.** In the pinned image `sketchyup-ci-pinned:r094a` (Qt 6.11.2, Weston 15.0.1,
  Mesa 26.2.3), with the exact CI lines on the `main` build `3bf68899` and CPU load from a
  separate container (one busy loop per CPU, as `stress-ng --cpu $(nproc)`; 22 CPUs):
  - `WAYLAND_DEBUG` traces of 12 runs (`text_size_input_tests`, Wayland 2×, four lanes; tracing
    slows every client): the test window's first `wl_callback.done` came 0.4–2.4 s after the
    shell's `desktop_ready`, 15–27 s after Weston started; in two runs it had not come when the
    test ended. Weston scene-graph dumps of three loaded runs (`weston --debug`,
    `weston-debug scene-graph`) show the "desktop shell fade surface",
    fully opaque, over the whole output until then. With `startup-animation=none` (no curtain)
    the windows still showed "Painted frame rate: 0.00" while no background had been committed.
  - Qt's `qt.qpa.wayland.backingstore` log in all 16 logged exposure failures: "Didn't receive
    frame callback in time, window should now be inexposed" about 0.7–1.5 s after start, then
    nothing until the "window exposed" check failed.
  - Interleaved A/B, four lanes, the same load: the `main` harness failed 12 of 40 runs
    (`shortcut_input_tests` 5, `text_size_input_tests` 4, `viewport_label_overlap_tests` 3,
    all Wayland 2× "window exposed"; `file_operation_input_tests` and `units_input_tests` 1×
    0 of 16); with `QT_WAYLAND_FRAME_CALLBACK_TIMEOUT=60000`, 0 of 40. A second interleaved
    set (three 2× suites, 24 runs per arm): `main` 11 failures; waiting in the harness for the
    background before starting the test, 1 (that wait took 8.0–19.7 s, median 10.4 s, and the
    failure was a 60 s deadline); the timeout setting, 0; both, 0.
  The cause also explains why the records saw it under load and more at 2× (four times the
  pixels for the helper to paint), and why identical reruns passed.
  **Fix.** `scripts/test-wayland.sh` sets `QT_WAYLAND_FRAME_CALLBACK_TIMEOUT=60000` (the
  deadline) unless the caller sets it. Nothing hides a window in this session, so a late frame
  callback means a slow compositor, and the test window now stays exposed while it waits. The
  60 s deadline and the tests' 5 s exposure waits are unchanged. A readiness wait for the
  background was rejected: it fixes the same failures but spends 8–20 s of the deadline under
  load. **Verified.** See the
  [activation and exposure matrix](#activation-and-exposure-matrix-2026-10-17).
  **Not addressed.** The 2× session's output is 3200×2000 logical (6400×4000 pixels), not
  the 1600×1000 the harness's `--width="$((1600 * test_scale))"` suggests (Weston's headless
  `--width` is logical); that quadruples the software compositing work at 2× but changes the
  screen the 2× tests see, so it is left for its own change. **Measured and kept 2026-10-18
  (branch `test/weston-output-size`).** A trial with `--width=1600 --height=1000` at every
  scale (`wl_output.mode` 3200×2000, `xdg_output` 1600×1000 at 2×; 1× unchanged) changed no
  2× check, but the large output was throttling the tests: with the background painted sooner,
  the test windows get frame callbacks and render (`keyboard_reachability_tests` sent 563
  `wl_surface.frame` requests instead of 363). Over the 87 Wayland 2× CI lines, interleaved
  A/B in `sketchyup-ci-pinned:r094a`, wall time was 370 s → 357 s on 22 CPUs but 1037 s →
  1222 s on 4 pinned CPUs, and CPU time rose 24–36%. On 4 CPUs the 60 s deadline stopped
  `keyboard_reachability_tests` and `visible_focus_input_tests` (Model) on both sides and
  `visible_focus_input_tests --workspace=sheets` after only (D-028). With no check-level
  benefit, the harness keeps the 6400×4000 device output and says why in a comment. Qt's
  emulated blocking swap still waits at most 100 ms for a frame callback
  (`QWaylandGLContext::swapBuffers`, hard-coded) and can mark a window unexposed until the
  next callback; after this fix no run failed on it.

Closed by the D-033 harness fix (2026-10-15, branch `fix/startup-abort`):

- **D-033** (the demo aborts at start in the X11 AT-SPI bridge test, with an empty log). Class:
  **Minor** (test infrastructure). Seen once in CI: `main` push run
  [38690132218](https://github.com/zygote55/sketchyup/actions/runs/38690132218) (job
  116273878087, `c788cb6a`), line `xvfb-run -a … QT_SCALE_FACTOR=2 … dbus-run-session -- python
  tests/accessibility_bridge_test.py`: "Application exited with -6; last 0 log lines" and "Demo
  exited before accessibility registration", about 120 ms after the demo started. The 1× line
  had passed, and the same code passed before and after.
  **Cause.** The demo could not connect to the X server: `qt.qpa.xcb: could not connect to
  display`, then Qt's `qFatal` "no Qt platform plugin could be initialized" in
  `QGuiApplicationPrivate::createEventDispatcher` (inside `QApplication`'s constructor, before
  any SketchyUp code), hence SIGABRT. The server was running. Before starting the demo, the test's
  `atspi_init` opens and closes an X display (libatspi reads the `AT_SPI_BUS` root property),
  which can leave Xvfb without clients; Xvfb then resets, and a connection attempted during the
  reset can fail. The log was empty because Qt, with no terminal on stderr, sends its messages, the
  `qFatal` included, to journald, which the CI container lacks; no Qt message appears anywhere
  in the CI logs.
  **Evidence.** Reproduced locally in `sketchyup-native-fonts-check:20261008` with the `main`
  `dev` build (`43a4007a`), running the 2× line in six concurrent lanes on six CPUs, each lane
  waiting for its previous Xvfb to exit. With the default server, 8 of 167 runs failed (4.8%), all
  with the CI signature, including the exact CI sequence of back-to-back `xvfb-run -a` lines on
  `:99`. With `-noreset`, 0 of 248 failed (121 with the `main` test, 127 with this fix). With the
  reset avoided another way (a client holding one X connection open for the whole run), 0 of 122
  failed. An `LD_PRELOAD` handler attached
  `gdb` at the abort; every captured abort had the same stack:
  `abort` ← `QMessageLogger::fatal` ← `libQt6Gui` (platform plugin start-up) ←
  `QGuiApplicationPrivate::createEventDispatcher` ← `QCoreApplicationPrivate::init` ←
  `QApplicationPrivate::init` ← `main` (`src/app/main.cpp:21`, `QApplication app(argc, argv)`).
  No SketchyUp code, the Sheets workspace (off without `SKETCHYUP_SHEETS_WORKSPACE=1`) or any
  worker thread is involved.
  **Fix.** The two X11 bridge lines run Xvfb with `-noreset` (`.github/workflows/native.yml`), as
  `scripts/test-keyboard-layout.sh` already does, and the test sets `QT_FORCE_STDERR_LOGGING=1`
  for the demo, so a Qt warning or fatal error is in the application log that a failure prints.
  The Wayland lines start no Xvfb; for them only the logging changes. The journald routing applies to every native
  test in CI, so D-031's message-less Wayland exit (recorded by R091.h) may also have lost a Qt
  message the same way; that is not shown.

Closed by the D-032 fix (2026-10-17, branch `fix/d032-fractional-focus`):

- **D-032** (focus rings under the 3:1 threshold at the fractional 1.5× factor). Class:
  **Minor**. **Cause.** Every focus ring is a style-sheet border (`Window::applyTheme` and the
  dialogs' own sheets), and Qt 6.11's `QRenderRule::drawBorder` (`qstylesheetstyle.cpp`)
  always turns antialiasing on before `qDrawBorder`. At 1.5× a control whose logical edge maps
  to the middle of a device pixel leaves that pixel half covered, so its painted colour is the
  ring blended with the chrome beside it (for example `#a6b8a5` for the light accent
  `#547858` over `#f7f7f2`, 1.95:1). A wider ring keeps the same part-covered outer pixel,
  which is why R091.h's 2 px accent-fill rule did not help. **Fix.** One application-wide
  filter, `src/app/focus_ring.cpp`, installed by `Window`, acts only when a control paints at a
  fractional device pixel ratio. For each shown focusable control it reads the border the
  style sheet paints, from the control rendered at 1× (colour, width, margin and straight span
  per edge, once per state: focused or not, checked or not, size), and after the control's own
  paint fills each straight edge without antialiasing over whole device pixels:
  ceil(width × ratio) pixels from the pixel holding the outer edge, with a pixel the inner edge
  leaves part-covered given the fill just inside. The colour token and the placement are the
  style sheet's own; the rounded corners are left as painted. Unfocused borders are snapped as
  well, so an accent-filled control's indicator (the change of its boundary colour) is
  measured between whole pixels. GL surfaces and controls that paint all their own pixels
  (the page canvas) are left alone. At 1× and 2× nothing runs. **Verified** in
  `sketchyup-ci-pinned:r094a` with the native.yml lines (Wayland at 1.5 as the fractional job
  runs it: `SKETCHYUP_TEST_SCALE=1 scripts/test-wayland.sh env QT_SCALE_FACTOR=1.5 …`), both
  themes in each run. Failing measurements, before → after:

  | Run | X11 1× | X11 1.5× | X11 2× | Wayland 1× | Wayland 1.5× | Wayland 2× |
  | --- | ---: | ---: | ---: | ---: | ---: | ---: |
  | Model | 0 → 0 | 143 → 0 | 0 → 0 | 0 → 0 | 135 → 0 | 0 → 0 |
  | Sheets (`--workspace=sheets`) | 0 → 0 | 88 → 0 | 0 → 0 | 0 → 0 | 90 → 0 | 0 → 0 |

  Every after cell exits 0. The host was loaded by other work (load average about 60 on 22
  cores), so the Wayland 2× cells, where the change does nothing, ran past the harness's 60 s
  Weston deadline or hit the D-021 "Focus window exposed" timeout; with the deadline raised
  to 240 s they passed with 0 failures (113 s and 67 s). The fractional-scaling CI job now
  runs both lines at 1.5 on X11 and Wayland, and `.github/ci-line-inventory.txt` is
  regenerated (`ci-line-inventory.py check`: 518 lines, 513 distinct). In the same image,
  `ctest --preset dev -E "_real$"` passes 198 of 198; `theme_input_tests` passes on X11 at 1×,
  1.5× and 2× and on Wayland at 1× and 2×; `native_accessibility_audit` and every
  `--dialogs-keyboard` group pass on X11 at 1× and 2×; and every other line of the
  fractional-scaling job (1.5× and 1.75×, X11 and Wayland) passes.

Closed by the D-048 fix (2026-10-27, PR #498, merged as `715d0ae0`; branch `fix/d048-outliner-rebuild`):

- **D-048** (an edit on a 100,000-face model holds the UI thread for 140 to 160 ms, and the
  Outliner rebuilds every row on each edit). Class: **Major** (reclassified by the parent; was
  Minor, provisional). **Mechanism.** `OrganizationPanel::refresh` cleared both trees and built one
  item per record on every `Viewport::changed`; `filter()` then set every row's hidden state. It now
  builds the rows that the trees show (`entityRows` and `tagRows`) and rebuilds a tree only when its
  rows differ from those of its last build, or when the viewport's selection differs from the tree's
  selection (a rejected click, such as a locked entity, leaves its row selected). An Outliner row holds
  the id, the parent, the name, the tooltip's tag name, the kind word, and the hidden, locked and
  context states (hidden and locked include the ancestors and the tag visibility). A Tags row holds the
  id, the parent, the name, the folder mark, the visible checkbox and the effective visibility. The
  comparison is exact, not a hash. Record pointers cannot decide it: `Document::transform` replaces the
  placement's record, so the pointer changes though no displayed field does. `filter()` runs only after
  an Outliner rebuild, because nothing it reads changes otherwise. A new document always rebuilds.
  **Measured** (Release, `edit_latency_benchmark`, offscreen, section hidden, 20 edits after one
  warm-up; the edit plus the synchronous slots; load average before and after the run):
  - 99,996 faces (8,333 placements): before p50 148.1, p95 151.0, max 151.9 ms (load 4.45 and 4.17);
    after p50 36.9, p95 43.1, max 44.0 ms (load 3.60); a second after run p50 38.2, p95 47.0, max
    53.9 ms (load 3.19 and 3.57).
  - 49,992 faces (4,166 placements): before p50 68.7, p95 77.0, max 82.8 ms (load 4.00); after p50
    16.8, p95 19.0, max 21.5 ms (load 3.19).
  Step 1 meets the budget (p95 below 100 ms at 8,333 placements, section hidden), so steps 2 and 3 are
  not implemented. The 37 ms that remain include the row comparison and the other slots (the inference
  snapshot and the Text panel, 11.5 and 7 ms before); the split was not measured.
  **Tests.** `organization_input_tests` (Debug, `QT_QPA_PLATFORM=offscreen`): a transform keeps every
  Outliner and Tags item (the unmodified sources fail this check); a rename, a tag assignment, a tag
  rename, a tag hiding its entity, hide, lock, a locked selection that the viewport rejects, a viewport
  selection, the context mark, a search result kept across a transform, and a hierarchy change each
  reach their row. `keyboard_outliner_input_tests`, `selection_input_tests`, `selection_tests`,
  `tag_tests`, `keyboard_panel_tabs_tests` and `narrow_layout_input_tests` pass. `group_input_tests`
  fails "Line splits active group geometry without merging into inactive neighbor" (1 face, not 2) on
  the unmodified sources under the same platform, so this change does not cause it; CI, which runs
  the xcb and Wayland lines, is the check for it. **Not covered by a test:** the kind word in the
  tooltip (it is in the row, but no public edit changes one row's kind without adding a row).
  **Behaviour change:** the Tags tree's row selection now survives a refresh that changes nothing in the
  Tags tab; before, every refresh cleared it.

Closed by the D-047 fix (2026-10-21, branch `fix/d047-qt68-includes`):

- **D-047** (SketchyUp 1.3.0 RC2 does not build against Qt 6.8.0). Class: **Major** (new and
  fixed), the class D-037 had: the declared minimum Qt cannot build the release package.
  **Found** by the 1.3.0 RC2 walkthrough's Qt 6.8 reference build (`makepkg --nocheck` from the
  RC2 archive `36bc0fa2…`), which stopped at step 630 of 809. **Scope.** A second build of
  the same archive on the same snapshot with `ninja -k 0` compiled all 809 targets except that
  one object and its test executable; there is no other Qt 6.8 break. **Fix.** One line:
  `#include <QJsonDocument>` in `tests/terrain_edit_input_tests.cpp`. The application sources,
  installed files and tests' behaviour are unchanged. **Why CI missed it.** Every CI job builds
  on the pinned Qt 6.11.2 image, whose headers include `QJsonDocument` transitively; the Qt 6.8
  reference is a release-candidate check only. A Qt 6.8 compile job in CI is a follow-up to
  ADR 0170.

Closed by the D-046 fix (2026-10-21, branch `fix/provider-stream-error-detail`):

- **D-046** (a ChatGPT-plan usage limit is reported as an incomplete or invalid response).
  Class: **Minor** (new and fixed). **Found** by the 1.3.0 RC1 corpus run; the cause was settled
  by two owner-approved single-request probes (the failing request, and the same request without
  the three R095.g terrain tools), which got the same `error` and `response.failed` events, so
  the terrain tools were not involved. **Mechanism.** `completedOpenAiStream`
  (`src/integrations/openai_provider.cpp`) now reads the error fields of an `error`,
  `response.failed`, `response.incomplete` or untyped event: `error.*` or the event's own `code`,
  `message` and `param`, the response `status`, `incomplete_details` and `status_details`. The
  usage-limit codes on `error` or `response.failed` throw `ProviderUsageLimit` (the existing
  "Manage ChatGPT usage" message); every other such event throws `ProviderStreamError`
  (`src/integrations/provider_transport.hpp`), whose fields are bounded (message 512 characters,
  code and param 128, details 256), have control characters replaced and credential-shaped text
  (`sk-…`, JWTs, bearer values) redacted. The transport ends the task with "Provider returned an
  error: CODE: MESSAGE"; `provider_trial` records the fields as `streamError` in the exchange.
  Raw bodies are still never shown or logged ([ADR 0034 addendum](../decisions/0034-openai-provider.md#addendum-2026-10-21-stream-error-fields-d-046)).
  **Tests.** `openai_provider_tests` replays the RC1 sequence (`response.created`,
  `response.in_progress`, `response.metadata`, `error`, `response.failed`) and a flat
  usage-unavailable `error` (both the usage limit, also through the provider, whose status names
  "Manage ChatGPT usage"), an `error` with type, code, param and a message holding a newline, a
  bearer value, an `sk-` key and a JWT (all redacted), a 2,000-character message (bounded), a
  `response.incomplete` and a `response.failed` with their details, an untyped event, and a
  `model_not_found` stream through the provider whose status is "Provider returned an error:
  model_not_found: The model does not exist.".

Closed by the D-044 fix (2026-10-19, branch `fix/d044-terrain-import-progress`):

- **D-044** (terrain import parse and triangulation show no progress and cannot be cancelled).
  Class: **Minor** (unchanged). **Mechanism.** `TerrainProgress`
  (`src/geometry/terrain_progress.hpp`, standard library only) carries a cancellation callback
  (the `canceled` convention of `Inference::sync`) and a stage report (parse, prepare,
  triangulate, tile, each 0 to 1, reported only forward). The parsers, `prepareTerrainSources`,
  `triangulateTerrain`, `tileTerrainTriangles`, `runTerrainImport`, `importTerrainSource` and
  `runTerrainEdgeRequest` take it optionally and check it at phase and loop boundaries.
  Vertex insertion, the one long phase (72–104 ms for 125,000 vertices), is cancellable through
  CDT 2.0.0's own `ICallbackHandler::isAbortCalculation`, compiled in by
  `CDT_ENABLE_CALLBACK_HANDLER` in the adapter; the vendored headers are unchanged. A cancelled
  run returns `cancelled` (code `CANCELLED`) with no mesh, record or report. **Desktop.** The
  import dialog and the roles step show the stage ("Checking: triangulating…") and a
  determinate, named progress bar that takes no focus; the import preview names the stage. Cancel,
  Escape, closing the window and a changed setting or role cancel the running dry run, and the
  destructor waits only until the next checkpoint. **CLI.** `--import-terrain` shows the stages
  on stderr when stderr is a terminal; stdout and the JSON output are unchanged.
  **Measured** (`terrain_budget_benchmark --cancel 200`, Release): cancel latency at most
  55.3 ms (points, 249,970 triangles) and 49.0 ms (contours, 249,298), p95 48.4 and 42.8 ms;
  the longest unchecked steps are CDT's super-triangle erase with the neighbour build, and the
  DXF reader's tokenization (45 ms). Dry runs 375 and 523 ms, as before. **Determinism.** An
  uncancelled run is bit-identical with or without progress; every pinned digest is unchanged.
  **Tests.** `terrain_triangulation_tests` and `terrain_import_tests` cancel at every progress
  report of their fixtures (nothing kept, sanitize preset clean); `terrain_import_dialog_tests`
  and `terrain_desktop_tests` hold a dry run through a test hook and check the progress display,
  focus, restart on a change, and Cancel or close during a run (about 1 ms, document
  unchanged). See [ADR 0168, D-044 notes](../decisions/0168-terrain-import-and-surfaces.md#implementation-notes-d-044).

Closed by the D-043 fix (2026-10-19, branch `fix/d043-terrain-history-budget`):

- **D-043** (terrain edits exceed the 64 MiB history budget). Class: **Major** by the scale
  above (a broken workflow inside the declared 250,000-triangle terrain limit, with a coarser
  or split terrain as the workaround); added and fixed on the same branch, so it is not open.
  **Reproduction** (debug build, rolling grid terrains, binary search): `createTerrain` and
  `importTerrain` applied at most 151,250 triangles; `regenerateTerrain` with every tile
  changed and `explodeTerrain` at most 76,050, below Explode's own 100,000-face modelling
  limit. A 178,802-triangle point-file `terrain.import` dry run reported success, then the
  import threw "Edit exceeds the 64 MiB history budget" (`INVALID_OPERATION` through
  `executeBatch`, sessions and the CLI; the raw message in the import dialog after Import).
  **Measurements** (`mallinfo2` heap, `ru_maxrss`): a 249,218-triangle terrain is 96.9 MiB of
  heap and charged 104.6 MiB (440 B per triangle; a strip-shaped mesh with one vertex per
  triangle charges 546 B, 250,000 disjoint triangles 673 B, and a record at the source limits
  30.1 MiB). The create entry adds no heap: history shares the immutable tile bodies with the
  document. A regeneration that replaces every tile holds the old tile set, 194 MiB in all,
  unchanged by undo and redo (peak 299 MiB resident). Through `executeBatch`, the edit also
  carries the erased tiles' lineage (775,752 entries, 74 MB charged), so a near-limit
  `terrain.set_parameters` is charged 288 MiB, and the document holds 255 MiB of heap after
  import, settings, undo and redo (peak 737 MiB resident, mostly the batch's transient
  staging). **Fix (option a).** The record, tile and lineage changes of an edit that creates,
  changes or drops a terrain record are charged to `Edit::terrainBytes` against
  `Document::terrainHistoryLimit` (512 MiB), sized for a 250,000-triangle terrain plus one
  regeneration's before and after: the worst terrain any 250,000-triangle mesh and record can
  make charges 190.7 MiB, so every terrain operation within the document limit applies as one
  undoable edit, and no history refusal remains reachable there. The oldest entries are
  dropped while either budget is exceeded (as for the 64 MiB budget); an edit over the terrain
  budget would refuse as `TerrainError` `LIMIT_EXCEEDED`, never as a raw error. **Worst case**
  (250k terrain, regenerate, undo, redo): 194 MiB of heap through the core and 255 MiB through
  commands; repeated full regenerations stay at 303 MiB of heap (the terrain and two older
  tile sets), with 511–539 MiB peak resident during a regeneration. The terrain history
  charge never exceeds 512 MiB, and heap is about 0.93 of the charge. Edits that touch no
  terrain record are charged exactly as before. `terrain_history_tests` covers create,
  import, regenerate, set_parameters (twice) and explode with undo and redo, the explode
  refusal at 250k, and the worst-case bound.

Closed by the D-042 fix (2026-10-18, branch `fix/clipper2-tessellation-hang`):

- **D-042** (unbounded allocation in face and section-cap tessellation). Class:
  **Release-blocking** by the scale above (a hang in a Core workflow); added and fixed on the
  same branch, so it is not open. Reproduction, all on the 1e-7 m grid and in a child capped
  at 2 GiB: millimetre-rounded faces in random orientations hung on 2/400 quads at 10 m,
  14–25/400 at 30–1,000 m; 0/400 L-shapes at 10 m and 5–11/400 at 30–1,000 m; and 0–4/400
  faces with a hole at 10–1,000 m. Section caps use the same triangulator on an absolute
  1e-7 m grid: 1–4/300 quad prisms of 100–1,000 m hung. The cause is the double-precision
  `SegsIntersect` in `third_party/clipper2/src/clipper.triangulation.cpp`; disabling Delaunay
  legalisation does not help. The vendored file is patched (marked `// SketchyUp patch:`): the
  segment test is exact, and edges, triangles and Delaunay legalisation steps are bounded in
  proportion to the input vertex count, so any future failure refuses the face with
  "Intersecting or degenerate face outline" (or leaves a section unfilled) instead of growing.
  On 4,000 random faces the patched triangulator returns bit-identical triangles wherever the
  old one returned a correct triangulation; the 34 that hung and 16 whose old result failed
  the area check now triangulate correctly. Regression cases in `core_tests` (quads, L-shapes,
  faces with a hole, the bound) and `section_geometry_tests` (a 100 m quad cap) run in forked,
  non-dumpable children under a 2 GiB address-space cap.

Closed by the D-023 fix (2026-10-18, branch `fix/d023-focus-contrast`):

- **D-023** (focus-ring contrast residuals next to the 3D canvas and native scroll bars).
  Class: **Minor**. The residuals were four, and (e) was already closed.
  1. **Two-tone ring where a control borders the canvas.** The tool rail's own background is
     already a halo: a rail button's ring sits 4 to 5 px inside the rail, so since the rail
     stopped being a Tab stop (R084.v) no rail ring touches the canvas; the new test measures
     that chrome band and the gap. The breadcrumb, the component scope banner and the
     Assistant container are the controls that do touch it. Each has the property
     `focusHalo` and, in `Window::applyTheme`, `margin:2px; background-clip:margin`, so the
     style sheet paints the control's own background as a halo outside its border (the ring):
     chrome surface for the breadcrumb, the Assistant container and the plain banner, and the
     information surface for the parametric banner. The padding gives up the margin, so the
     outer size and the text position are unchanged. Qt anti-aliases the margin's rounded
     outer edge, so of the 2 px one device pixel is a blend and the next is the full halo
     (1 px at 1×, 3 device pixels at 2×); with a 1 px margin the halo was only that blend
     (`#888d88` over `#202923` and the canvas), which is why the margin is 2 px. A border
     cannot hold two colours, and `outline` is not drawn for a `QLabel` in a style sheet (checked
     in Qt 6.11.2), so no custom paint was needed. **`focus_ring.cpp`**
     reads a control with `focusHalo` as two straight runs, halo then ring, and at a
     fractional factor fills the halo band as well, from the device pixel holding its outer
     edge up to the ring; the unfocused state has no halo and is read as before. In the light
     theme the same structure uses the light chrome surface. The light halo is
     1.02 to 1.06:1 against the canvas, which no chrome token can change, so there the ring
     itself is the separation (4.40:1 against the canvas).
  2. **Canvas ring.** `ThemeColors::canvasRing`, used by `Viewport` for its own 2 px ring: the
     accent in the dark palette, and `#547958` in the light palette instead of the accent
     `#547858` (one step of green). The light accent is the ring on the default document, so
     this is the ring beside dark chrome.
  3. **Scroll bars.** `QScrollBar` in `Window::applyTheme`: the panel surface for the track,
     the muted ink for the handle (4.76:1 light, 7.72:1 dark), the ink under the pointer
     (11.27:1 and 13.18:1 against the track), 10 px thick at 100% text (20 px at 200%), a 4 px
     round handle, no arrow buttons, and the corner themed. No scroll bar is among the Tab
     stops the focus sweep finds, so there is no scroll bar ring to theme.
  4. **The 14 overflowing controls.** 2 are in the Materials panel at 100% text (the swatch
     list and the side selector; the panel's scroll area does not scroll sideways and its
     two columns of buttons needed 258 px in 214), and 12 are in the Assistant preferences
     dialog at 200% text (six runs of the Store button and of a spin box, 610 and 376 px in
     456 px: the dialog was a fixed 500 × 620 and its widest row, the Store button, needs
     about 630). `MaterialsPanel` now lays its buttons out in one column when two do not fit,
     and the dialog is sized for the interface text size and at least as wide as its widest
     row. Nothing is elided. The test's scroll step also brings in the whole control and not
     only its centre.

  | Measurement | Before | After |
  | --- | ---: | ---: |
  | Dark ring against the default canvas (breadcrumb, banner, Assistant container, rail) | 1.64 | ring against halo 8.05 (banner 7.68), halo against canvas 13.19 (12.57) |
  | Light ring against halo, halo against canvas, ring against canvas | not measured | 4.65 (banner 4.48), 1.06 (1.02), 4.40 |
  | Canvas ring against dark chrome `#202923` | 2.997 (`#547858`) | 3.032 (`#547958`) |
  | Canvas ring against light chrome `#f7f7f2` and the canvas `#f0f1ec` | 4.648 and 4.400 | 4.595 and 4.350 |
  | Scroll bar handle against track, light and dark | native, not themed | 4.76 and 7.72 |
  | Controls skipped as wider than their scroll area (Model run) | 14 | 0 |

  **Verified** in `sketchyup-ci-pinned:r094a` with the native.yml lines. `visible_focus_input_tests`
  now measures each two-tone ring in both themes (the band colour is the background's own,
  ring against halo at least 3:1, and halo against the canvas at least 3:1 in the dark theme),
  the canvas ring against both chromes, and the scroll bars' handle against track at both text
  sizes. 491 controls measured, 0 failures, 0 clipped in the Model run on X11 and Wayland at
  1×, 1.5× and 2×; the Sheets run has 0 failures (on X11 the 640 × 480 screen makes the Export
  sheets dialog scroll its findings, 2 to 8 skipped, by that dialog's design).
  `theme_input_tests`, `text_size_input_tests` and `keyboard_reachability_tests` pass on X11
  and Wayland at 1× and 2×, and `text_size_input_tests` at 1.5×.
  `ctest --preset dev -E "_real$"` passes.

Closed by the D-038 fix (2026-10-17, branch `fix/d038-paint-digest`):

- **D-038** (A paint inside a definition marks its driver Changed, with "changed shape"
  wording). Class: **Minor**. **Cause.** A driver's stored `bound` digest is the SHA-256 of
  its target member's canonical record (ADR 0165 §4, `componentDriverDigest`). That record
  includes the colour, face colours, materials and texture mappings, so a paint changed the
  digest, and the driver's derived state became `changedSinceBound`. No driver kind reads
  appearance. `bound` is stored in schema 28 (`drivers[].bound`), so leaving appearance out
  of it would have made every bound driver in a 1.2.0 file read Changed on open. **Fix.**
  `component.edit` restamps a driver whose target the edit changed only in appearance: the
  target's new records, with the colour, face colours, materials and texture mappings taken
  back from before the edit, must hash to the stored digest. Drivers that were already
  Changed, and edits that also change the target's shape, keep the old behaviour. The digest
  definition, schema 28 and the stored bytes are unchanged
  ([ADR 0165 addendum](../decisions/0165-constrained-component-parameters.md#addendum-d-038-2026-10-17-a-paint-keeps-its-drivers-bound)).
  The strip's wording is unchanged and now appears only for edits that change more than
  appearance. **Tests.** `component_parameter_tests paint` (a colour on the stretched top and
  a material on the repeated shelf leave every driver bound; undo and redo; a paint with a move,
  a paint on a Changed driver and a move still mark Changed), `component_parameter_io_tests`
  (the committed `component-parameters-v28.sketchyup` opens with every driver bound; save,
  reopen and save again are byte-identical, also after a paint) and
  `component_parameter_state_input_tests` (painting the shelf in the context shows no strip and
  no Changed row). `component_acceptance_input_tests` (R094.h) now expects no strip after the
  paint, two Changed drivers after the move, and "Rebind 2 drivers".

Closed by the D-037 fix (2026-10-17, branch `fix/d037-qt68-orientation`):

- **D-037** (SketchyUp 1.2.0 does not compile against Qt 6.8.0, the declared minimum). Class:
  **Major** (a supported configuration had no working 1.2.0). Found by the 1.2.0 RC gate
  (`71a6b4f3`) on the Qt 6.8 reference (`archlinux:base-devel-20241013.0.269705`, 2024-10-15
  archive, Qt 6.8.0, GCC 14.2.1): `src/app/window_sidebar.cpp:112: error: 'Orientation' is not
  a member of 'QAccessible::Attribute'`. **Cause.** R094.a (`c3cfd6ac`) reports the sidebar
  handles' orientation through `QAccessible::Attribute::Orientation`, which first appears in
  Qt 6.11.0: qtbase's `src/gui/accessible/qaccessible_base.h` declares `Custom, Level` at
  `v6.8.0`, `v6.8.3`, `v6.9.0` and `v6.9.3`, `Custom, Level, Locale` at `v6.10.0` to `v6.10.2`,
  and `Custom, Level, Locale, Orientation` at `v6.11.0`. The installed headers agree: the
  reference's `qt6-base 6.8.0-1` has `Custom, Level`; the pinned image's `qt6-base 6.11.2-3`
  has all four. **Fix.** The handle's `QAccessibleAttributesInterface` is compiled only for
  `QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)`. On older Qt the handle still reports role
  `QAccessible::Splitter` and its name; only the orientation is omitted. `CMakeLists.txt` and
  the support matrix keep Qt 6.8. `sidebar_resize_input_tests` now also checks the handle's
  orientation attribute when built against Qt 6.11 or later, and `accessibility_bridge_test.py`
  asserts the AT-SPI vertical state when the running Qt (the bridge's toolkit version, now in
  its report as `qtVersion`) is 6.11 or later. **Verified.** The whole tree (dev preset, every
  native fixture, 1117 targets) builds on the Qt 6.8 reference with no errors, so no other
  F02 code needs a newer Qt. There `sidebar_resize_input_tests` (X11 1×) and the bridge test
  (X11 1×, orientation "none", `qtVersion` 6.8.0) pass, and the D-036 check on 1.2.0 passes:
  the sheet and measured PDFs have 0 Poppler syntax errors, readable Title and Creator, a clean
  `qpdf --check`, and the same text as 1.1.1 on Qt 6.8 (`c4812bec…`, `ef6cbd21…`). In the
  pinned image (Qt 6.11.2) the native.yml 1× lines for both tests pass on X11 and Wayland,
  with orientation "vertical". Shipping the fix needs a new 1.2.0 RC.

Closed by the 1.1.1 patch release:

- **D-036** (sheet and measured PDF exports on Qt 6.8.0 write a malformed Info dictionary).
  Class: **Minor** (document metadata only, on the oldest supported Qt). Found 2026-10-15 by
  the Qt 6.8 reference clean machine at the 1.1.0 RC `093484f9`
  ([F01-rc-1.1.0](https://github.com/zygote55/sketchyup/blob/main/docs/verification/F01-rc-1.1.0.md#failures));
  shipped in 1.1.0 by owner decision. On Qt 6.8.0
  (`archlinux:base-devel-20241013.0.269705`, 2024-10-15 archive) the installed CLI's opted-in
  sheet export wrote an Info dictionary without its closing `>>` and with the dates
  `D:00000000-1-1-1Z`; Poppler reported 48 "Dictionary key must be a name object" syntax
  errors and could not read Title, Creator, Producer or the dates. Pages, text, fonts and the
  attached report were intact. **Cause (Qt).** `QPdfEnginePrivate::writeInfo` in Qt 6.8.0 and
  6.8.1 ends the dictionary with `2` instead of `>>` (qtbase a30591603b58, fixed for 6.8.2 by
  14bd53687faa); Qt 6.8.0 also prints an invalid date when it cannot determine the system
  time zone. Every `QPdfWriter` output is affected, so the measured PDF export was malformed
  too, including in 1.0.x. **Fix.** `repairQtPdfInfo` (`src/io/pdf_info.cpp`) finds the
  trailer's `/Info` object through `startxref` and the cross-reference table and patches only
  those two patterns, at the same length, so every offset stays valid; both exports call it
  before hashing, so the report's SHA-256 still matches. Any other PDF is left byte for byte
  unchanged. `tests/pdf_info_check.hpp`, a strict reader of the `/Info` dictionary, checks the
  sheet export, the CLI sheet export and the measured export on any Qt, and a Qt 6.8.0-shaped
  copy is rejected, then repaired. Not repaired: the desktop Print path through `QPrinter`;
  on Qt 6.8.0 without a time zone the XMP dates stay empty. Shipped in 1.1.1
  ([F01.a](F01a-1.1.1-patch.md)).

Closed by the D-035 fix (2026-10-15, branch `fix/assistant-sheet-tools`):

- **D-035** (sheet tools enlarged the assistant's per-turn input; Advanced exceeded the token
  budget at the 1.1.0 RC). Class: **Major** (a supported workflow failed its acceptance gate on
  the supported provider configuration). Found by [R086.cb](https://github.com/zygote55/sketchyup/blob/docs/r086cb-rc110-corpus/docs/verification/R086cb-rc110-remote-evaluation.md)
  at the 1.1.0 RC (`251fd52d`): Advanced assemblies passed 1/3 (minimum 2). Two repeats
  followed the R086.ca command sequence but their 12th reply took usage past the
  262,144-token task budget, and the host stopped them with `BUDGET_EXCEEDED`, committing
  nothing. **Cause.** The in-app assistant advertised the shared inspection catalog and
  nested every inspection query inside `transaction.inspect`, so the eight drawing-sheet
  queries added by R090–R092 were re-sent twice on every provider turn. The ChatGPT-plan
  first request's tools grew from 55,230 to 64,972 bytes for Advanced, and the reported
  first-turn input from 11,368 to 12,974 tokens; no run used a sheet tool.
  **Fix.** One rule next to the assistant's tool builder (`assistantVisible` in
  `src/automation/assistant.cpp`) keeps every `drawing_*` operation and query out of the
  assistant's provider tool list: the top-level queries, the `transaction.inspect`
  requests and the `transaction.apply` commands. A drawing command cannot be authorized
  for an assistant task, and a drawing tool call is rejected as `UNSUPPORTED_CAPABILITY`.
  MCP, the headless session and the transaction APIs are unchanged and keep every sheet
  tool ([ADR 0164](../decisions/0164-sheet-layout-interface.md#assistant-tool-surface-110)).
  **Evidence.** `assistant_tool_surface_tests` checks that no `drawing_*` name is reachable
  in the assistant's schemas, that the surfaces for the five corpus authorizations and the
  every-command superset equal a baseline dumped from the v1.0.1 build byte for byte
  (`tests/assistant-tool-surface-baseline.json`), that a drawing query call never reaches
  the backend, and that MCP still lists the sheet queries and commands. The Advanced
  first request's tools are 55,230 bytes again, the R086.ca figure. The release corpus must
  be re-run at the next RC.

Closed by R084.v:

- **D-026** (reopened "Render with Blender" kept focus on Jobs…, so Enter opened Jobs instead of
  rendering). Reopening the sheet now focuses Render, which makes it the default again, whenever
  Render is enabled; while a job runs focus is left alone. `keyboard_reachability_tests`
  reproduces the job-start focus move, closes and reopens the sheet, and asserts that Render has
  focus and that Enter renders ([R084.v](R084v-keyboard-reachability.md)).

Closed by R084.t (#301):

- **D-025** ("Model" breadcrumb drawn over the "PERSPECTIVE / METERS" label at a 701×418
  window). [R084.t](R084t-viewport-label-overlap.md) places the projection label beside or
  below the breadcrumb; `viewport_label_overlap_tests` asserts no overlap.

Closed by R082.jj:

- **D-024** (edit feedback p95 at 10,000 placements missed the 100 ms routine-edit target for
  deep and textures). Each placement edit did whole-document work: full validation of a moved
  placement, an allocating hierarchy walk per record, a per-body viewport rebuild, per-face
  texture visibility tests and a full inference-index recomposition
  ([R082.jj](R082jj-edit-feedback.md)). On the RX 6800 XT (X11, one CPU, two campaigns) the
  p95 is now 40.3 and 37.6 ms for deep (was 209.3 ms), 39.2 and 40.6 ms for textures (108.9 ms),
  36.1–37.9 ms for repeated and 38.3–39.1 ms for far; edit and readback is 55.2–61.1 ms. Fixture
  and framebuffer hashes, mesh and inference counters are unchanged, and the full-validation
  oracle covers the new incremental path. Intel was not measured (waived).

Closed by the 2026-10-11 layers:

- **D-002** (file-operation hang). The test now fails within about a second with the dialog text
  when an undriven modal appears ([R083.g](R083g-unexpected-modal-guard.md), #282). With
  `SKETCHYUP_TEST_INJECT_SAVE_FAILURE=1` it exits 1 on X11 and Wayland, and the four normal
  cases pass. The product path was already shown correct in [R083.f](R083f-file-worker-hang.md).
- **Generic limit rejections** that named neither limit nor entity ([R082.gg](R082gg-placement-limits.md)).
- **Leading comma-decimal separator dropped from the viewport** on de/fr locales, found and fixed by
  [R085.p](R085p-keyboard-layouts.md); keypad decimal behavior is settled in [R085.q](R085q-keypad-decimal.md).
- **Far-family probe failure above 1,000 placements** ([R082.hh](R082hh-far-inference.md), #297): a benchmark
  fixture defect. The history loop called `Document::transform(id, local)` without the parent, which
  re-parented the placement. The only product caller, `scene.transform` in
  `src/automation/commands.cpp`, passes the parent explicitly (defaulting to the current one), so
  this is not recorded as a product defect. The API default is a footgun for new callers and about 90 test
  call sites use the same signature; making the parent a required argument is a possible follow-up, not a triage item.
- **Missing keyboard path to four Model-panel tabs and visible focus on most control classes**
  ([R084.s](R084s-keyboard-panel-tabs.md), [R084.q](R084q-visible-focus.md)).

## CI flake investigation (2026-10-30)

Branch `fix/flakes-x11`, for D-006, D-007 and D-008. Every run used the pinned image
`sketchyup-ci-pinned:r097a-ifc` (Qt 6.11.2, Weston 15.0.1, Mesa 26.2.3, llvmpipe), the `dev` Debug
build of the touched targets, the `native.yml` line for each suite with the CI environment, and a
container cap of 4 GB and 2 CPUs. "Under load" means two busy loops in the same container for the
whole series, the method the R084 and R085 matrices used to reproduce timing races. The runner
(`build/repro/repeat.sh`) is not committed.

### D-006: viewport camera assertion

| Line | Runs | Failures |
| --- | ---: | ---: |
| X11 1×, `viewport_tests` | 51 | 0 |
| X11 1.5× (fractional job line) | 40 | 0 |
| X11 1× under load | 50 | 0 |
| Wayland 1.5× (fractional job line) | 20 | 0 |
| X11 1× under load, after the counter change | 10 | 0 |

Cause: not established. The code path was read, not measured. A camera move that keeps the target in
the same 16-unit render cell re-uploads only the transparent batch, in `sortTransparent`, and only
when the matrix changes. Body, ground and profile uploads are driven by document or style state
(`rebuild`, `drawStyleGround`, `drawStyleProfiles`), not by the clock, so none of them explains an
intermittent geometry upload. Change: a failing run prints the frames drawn, the geometry,
transparency and body upload counts, the viewport size and visibility, and the renderer state.
Passing runs print nothing.

### D-007: transient dialogs report active before focus

Mechanism, measured. The export test's `activate()` on Wayland 1× under load, 15 runs and 165 calls:

- Before: `qWaitForWindowActive(dialog)` returned while the dialog was neither the focus window nor
  the active window, in 162 of 165 calls.
- After: `qWaitForWindowFocused(dialog)` returned in 165 of 165 calls, with the dialog focused.

The early return comes from `QWindow::isActive()`, which counts a transient child as active while
its parent is active. The Qt headers do not say what `qWaitForWindowFocused` waits for; the
measurement above shows that it waits for the dialog to hold focus.

Fix: `qWaitForWindowActive` became `qWaitForWindowFocused` at the ten dialog waits.
`component_acceptance_input_tests.cpp` (1), `component_parameter_state_input_tests.cpp` (1),
`sheet_export_input_tests.cpp` (1, in `activate()`), `terrain_import_dialog_tests.cpp` (2),
`native_accessibility_audit.cpp` (1), `native_dialog_audit.hpp` (3, one in `waitDialogFocus`) and
`native_dialog_cases.hpp` (1). The waits on the main window are unchanged: a top-level window's
`isActive()` is exact.

Evidence, every line run after the change:

| Line | Runs | Result |
| --- | ---: | --- |
| `sheet_export_input_tests`, Wayland 1× under load | 20 | 20 pass |
| `sheet_export_input_tests`, Wayland 2× | 4 | 4 pass |
| `sheet_export_input_tests`, X11 1× | 3 | 3 pass |
| `component_acceptance_input_tests`, Wayland 1× / 2× | 5 / 4 | 9 pass |
| `component_acceptance_input_tests`, X11 1× | 3 | 3 pass |
| `component_parameter_state_input_tests`, Wayland 1× / 2× | 5 / 4 | 9 pass |
| `component_parameter_state_input_tests`, X11 1× | 3 | 3 pass |
| `terrain_import_dialog_tests`, Wayland 1× | 5 | 5 pass |
| `terrain_import_dialog_tests`, Wayland 2× | 7 | 4 pass, 3 timed out |
| `terrain_import_dialog_tests`, X11 1× | 3 | 3 pass |
| `terrain_desktop_tests`, Wayland 1× | 4 | 4 pass |
| `native_accessibility_audit`, all 22 groups, Wayland 1× | 22 | 22 clean (137 dialogs) |
| `native_accessibility_audit`, all 22 groups, X11 2× | 22 | 22 clean (137 dialogs) |

The Wayland 2× terrain line hit the harness's 60 s deadline in the original test too: 4 of 7 runs
timed out against the original, and 3 of 7 against the changed test. The focus waits took 0 to 280 ms
per run. This existing deadline problem is not fixed here, because a longer deadline would only hide
it, and it is not recorded as a new defect.

Not changed: `keyboard_reachability_tests.cpp`, `activate(QWidget &)`, the R084.v re-map. It maps a
top-level window again when Weston leaves none active after a sheet hides, which is a compositor
workaround. Its waits are on the top-level window, where `isActive()` is exact.

### D-008: scene transition wait

What the wait stands for: `recallSavedScene` starts a 160 ms `QVariantAnimation`, the viewport's only
one. Its last tick applies the saved pose, and the tick comes from the Qt event loop. Under software
rendering each viewport paint runs on that loop. The fixture now waits for the animation's `finished`
signal, found as the viewport's `QVariantAnimation` child, so no product code changed. It prints
`Scene transition ended after N ms`, and the 5 s bound only stops a hang.

Measured with the new wait:

| Line | Runs | Result | Transition end (ms) |
| --- | ---: | --- | --- |
| Wayland 1× under load | 15 | 15 pass | 258–379 |
| X11 1× under load | 8 | 8 pass | 192–284 |
| Wayland 2× under load | 8 | 8 pass | 460–607 |
| Wayland 1× under load, final build | 3 | 3 pass | 292–389 |

Before the change, the pose-predicate version passed 20 of 20 Wayland 1× runs and 6 of 6 X11 1× runs
under load. The longest loaded transition ended at 607 ms, well under the 1.5 s of the CI miss. The cause
of that miss is still not established.

## Harness diagnostics (D-034)

Added 2026-10-15 with D-034, so a native-test failure in CI explains itself:

- **Deadline stacks.** `scripts/test-deadline.sh SECONDS command…` replaces `timeout` on every
  `xvfb-run` line in `native.yml` and wraps Weston in `scripts/test-wayland.sh`. At the deadline
  it prints each process the command started (thread names and states, then `gdb`'s
  `thread apply all bt`), calls `fflush(0)` in each so block-buffered `std::cout` output is not
  lost, then stops the command and kills anything it started that is still alive. The exit
  status stays 124 and `test-wayland.sh` still prints "Timed out after 60 s". CI installs `gdb`
  and runs the two native-test containers with `--cap-add=SYS_PTRACE`.
- **Crash reports.** The helper preloads `scripts/crash-trace.c` (built with `cc` on first use):
  on SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT any process in the tree, Weston and its clients
  included, prints every thread's stack through `gdb` (`backtrace()` without it), flushes stdio
  and dies with the same signal. CI containers keep no core dumps. ASan builds keep ASan's own
  report. `SKETCHYUP_CRASH_TRACE=0` turns it off.
- **Exit status.** A non-zero status or a fatal signal is named: "Exited with status N" or
  "Killed by signal SEGV" from the helper, and "Test exited with status N" or "Test killed by
  signal …" from `test-wayland.sh` for the Wayland client, whose status Weston does not return.

## Activation and exposure matrix (2026-10-17)

Branch `test/wayland-activation-exposure` (D-021, D-007), in the pinned image
`sketchyup-ci-pinned:r094a` (Qt 6.11.2, Weston 15.0.1, Mesa 26.2.3) on a 22-CPU host, with each
changed suite's exact `native.yml` dev lines. **Before** is the `main` build `3bf68899` with
`main`'s harness; **after** is this branch. The arms alternate run by run under the same
deliberate load from a separate container: one busy loop per CPU (as `stress-ng --cpu
$(nproc)`) with two test lanes at once. At that load the three longest Wayland 2× lines run past
the 60 s deadline in both arms (in a first pass at that load they passed 1, 0 and 0 of 3 runs in
each arm), so those three ran with a busy loop on half the CPUs instead (marked ½). A failure
is classed from its message and duration: **exposure** (a "… exposed" check, or "Render with
Blender opens", the sheet's exposure wait), **deadline** (the harness's 60 s, or 90 s on X11,
with every thread busy, mostly in llvmpipe; no check failed) or **focus** (a focus or activation
check).

| Line | Before | After |
| --- | --- | --- |
| Wayland 1× `keyboard_reachability_tests` | 20/20 | 19/20 (deadline 1) |
| Wayland 1× `shortcut_input_tests` | 20/20 | 20/20 |
| Wayland 1× `text_size_input_tests` | 20/20 | 20/20 |
| Wayland 1× `viewport_label_overlap_tests` | 20/20 | 20/20 |
| Wayland 1× `visible_focus_input_tests` | 10/20 (deadline 10) | 12/20 (deadline 8) |
| Wayland 1× `visible_focus_input_tests --workspace=sheets` | 19/20 (deadline 1) | 18/20 (deadline 2) |
| Wayland 2× `keyboard_reachability_tests` (½) | 19/20 (exposure 1) | 19/20 (focus 1) |
| Wayland 2× `shortcut_input_tests` | 10/20 (exposure 10) | 20/20 |
| Wayland 2× `text_size_input_tests` | 10/20 (exposure 10) | 20/20 |
| Wayland 2× `viewport_label_overlap_tests` | 12/20 (exposure 8) | 20/20 |
| Wayland 2× `visible_focus_input_tests` (½) | 16/20 (exposure 4) | 20/20 |
| Wayland 2× `visible_focus_input_tests --workspace=sheets` (½) | 20/20 | 20/20 |
| X11 1×, each of the six lines | 5/5 | 5/5 |
| X11 2×, five lines | 5/5 each | 5/5 each |
| X11 2× `visible_focus_input_tests` | 3/5 (deadline 2) | 3/5 (deadline 2) |

Wayland, 240 runs per arm: exposure failures 33 before and 0 after; deadline 11 and 11; focus 0
and 1. A further 30 runs per arm of Wayland 2× `keyboard_reachability_tests` (½): 25/30
(exposure 5) before, 30/30 after. The one focus failure after the fix is "Closing the
file-operation surface returns activation and focus to the viewport (focus none)" in `keyboard_reachability_tests`
(R091.h's prompt check, 14 s into the run): this branch changes neither that check nor the code
between it and the previous passing check, so it is recorded under D-007 as one occurrence of
an unconfirmed cause (0 in the other 49 Wayland 2× runs of the line after the fix and 0 of 20 at
1×; before the fix, the line's exposure failures ended some runs before that point). The
deadline failures are runtime under this load, alike in both arms; the deadlines are unchanged.

## Release-candidate triage (2026-10-13)

The RC is `7b01bb7a`. Every open item in the table above was re-checked against the RC
evidence: [R089.d](R089d-release-candidate.md), [R086.ca](R086ca-rc-remote-evaluation.md)
and CI run [38435734443](https://github.com/zygote55/sketchyup/actions/runs/38435734443), which
passes all four jobs. R089.d had four failures, and each is triaged:

| R089.d failure | Triage |
| --- | --- |
| `visible_focus_input_tests`, Wayland 2×, timed out three times on one CPU | New D-028 (Minor). Passes in CI and on six CPUs at the RC |
| `annotation_input_tests`, Wayland 2×, window-exposure check failed once | Existing D-021, extended. Passed on re-run and in CI |
| `interaction_tests`, Wayland 2×, exit 2 once | Existing D-021, extended. Passed on re-run |
| `verify-m6-offline.sh` stopped in its summary without `git` | New D-027 (Minor). Passes with `git` |

The RC also adds D-029 (Limitation): the PKGBUILD's unversioned dependencies, recorded
before in R087.u and R088.d but not in this table. No item changed class at the RC.

Why no class is release-blocking:

- **Release-blocking (none).** No open item loses or corrupts data, fails recovery, is a
  critical security issue, crashes or hangs a Core workflow, or fails a Core row
  ([R088.f](R088f-core-parity-report.md): 0 Open).
- **Major (none).** No open item gives a wrong result or breaks a workflow on a supported
  configuration. R086.ca at the RC shows no wrong-target mutation, false completion,
  unreported partial commit or unknown outcome.
- **Minor (8).** Each is test infrastructure (D-007–D-009, D-031, D-034, D-040), a path the
  product does not reach (D-019), or a render-statistics residual with no document effect
  (D-006). D-021, D-022, D-023, D-027 and D-028 were in this list at the RC and are fixed;
  D-044 (no progress or cancel on a terrain import that meets its time budget), added after the
  RC by R095.f, is fixed too.
- **Limitation (14).** Each is a documented boundary: external library or compositor
  defects with mitigations (D-010–D-013, D-020), evidence boundaries and format scope
  (D-014–D-018), decisions (D-001, ADR 0160), waived hardware checks (D-004, D-005,
  ADR 0161) and packaging (D-029). The release notes and support matrix disclose them.

## Release-candidate triage (1.4.0 RC1, 2026-10-28)

The RC is `9ed43c90`. Every open row was checked against it. The check read the rows' cited files
on `main` against the changes since the D-048 refresh, and the 1.4.0 records named in the table
rows and the CHANGELOG 1.4.0 section.

**Release-blocking: none.** No open row loses or corrupts data, fails recovery, is a critical
security issue, crashes or hangs a Core workflow, or fails a Core row. No Major row is open. The
two open rows, D-068 and D-069, are Minor. The 1.4.0 Major rows (D-050, D-055, D-062) are fixed on
`main`. R086.h at the RC passes 24/24 with no blockers, and no wrong-target mutation, false
completion or unreported partial commit was recorded.

**Fixed during 1.4.0 (D-048 to D-067), by PR.**
- PR #495, D-049 (IFC four-thread log race; the record says data race, not use-after-free).
- PR #496, D-064 (IFC sanitizer-only seccomp kill; main's verdict red).
- PR #498, D-048 (Outliner rebuilds only on a change).
- PR #500, D-061 (batch capture budget, 250,000-triangle terrain).
- PR #501, D-055, D-056, D-057 (presentation batch: Start, PNG bytes, request limit).
- PR #504, D-062 (catalog dialog freeze and memory).
- PR #505, D-063 (Blender stand-in build-tree grant in shared-library builds).
- PR #506, D-058, D-059, D-060 (render queue state, MPEG-4 wording, rendered camera).
- PR #510, D-050, D-051, D-052 (STEP bad faces, locations, PMI-only refusal).
- PR #511, D-053, D-054 (STEP PMI of a converting file, depth limit).
- PR #513, D-065, D-066, D-067 (`.skp` helper sandbox on `/dev/null`, quadratic hash, notice threshold).

**Open (1.4.0 records, not fixed): D-068, D-069.** Both are Minor and carried by R101.f with no
wrong result recorded.

**Added at RC1 by the full tier: D-070 (Minor).** RC1's full-tier run
[40801765717](https://github.com/zygote55/sketchyup/actions/runs/40801765717) failed one job on its
first attempt: Wayland 2×, where `skp_import_dialog_tests` missed the progress sheet's read step
during a 0.3 s stand-in helper run. Every other job passed on the first attempt, and the rerun of
the failed job passed, so the run's CI verdict is green. The fault is the test's sampling window,
not the import; the test fix goes to `main` after the release, so RC1 stands.

**Added by the RC walkthrough: D-071 (Minor).** The release notes' rollback (`pacman -U` of the
1.3.0-1 package) fails while `sketchyup-ifc` is installed, because it requires `sketchyup=1.4.0`.
Pacman refuses the transaction and changes nothing; removing `sketchyup-ifc` first works. The
installed notes are fixed at the RC, so the step goes into the GitHub release's notes and into
`RELEASE_NOTES.md` on `main` ([G-1.4.0](G-1.4.0.md#upgrade-and-rollback)).

**Not carried as defects.** These appear in the 1.4.0 records, and none is a defect with a wrong
result in a shipped build:
- R098d finding 4 (the export size note came too late above 100,000 triangles): fixed in the same
  review, before merge, so it never reached `main`.
- R098d findings 1, 2, 3, 5 and 9, and R098.h §12.8: owner decisions or 1.5 items. They are listed
  under acceptance work below.
- R099e C2 (the schema-30 refusal wording): resolved by a parent decision on the 1.3.0 CLI's
  unchangeable wording. R099e C4 (a 250,000-triangle document cannot be rendered): 1.5 follow-up 1.
- R100g F-2 and F-4: owner decisions (OD-5 and OD-7; acceptance work below). F-5 (failures that
  only show offscreen) and F-6 (a naming observation): not defects, per the record.
- R101f gaps 5 to 7 and 10 to 12: test coverage or unmeasured paths. Gaps 13 to 15 are specification
  or format notes (schedule display units, terrain names in the CSV, the CSV's `materialsApply`
  line). No wrong result is recorded for any of them.
- R102e F3 to F6: ADR wording, a stale note, test labels and test-coverage gaps. The product defects
  in that record are F1, F2 and F7, which are D-065 to D-067.
- #507 (`quantities_input_tests`, Delete after a closed modal on Wayland) fixes a Wayland test
  race in the quantities suite. No 1.4.0 record names it, so it is not added as a row.

## Release-candidate triage (1.5.0 RC1, 2026-10-30)

The RC is `1ca204fd`, the head of `main` and `release/1.5.0`. Since the 1.4.0 RC (`9ed43c90`), 25 pull
requests (#516 to #540) reached `main`, and 256 files changed.

**Release-blocking: none.** No open row loses or corrupts data, fails recovery, is a critical security
issue, crashes or hangs a Core workflow, or fails a Core row. No Major row is open. The 22 open rows are
unchanged: 8 Minor and 14 Limitation. R086.i at the RC passes 24/24 on corpus v6 with no blockers, and no
wrong-target mutation, false completion or unreported partial commit was recorded.

**Every open row was checked against the RC.** The check read each open row's cited paths against the 256
files that changed since `9ed43c90`. Three open rows cite changed files, and none of the changes alters its
evidence: `.github/workflows/native.yml` (D-011; the prior-release package pin moved to 1.4.0) and
`packaging/arch/PKGBUILD` (D-013 and D-029; `pkgver` only). The Wayland harness and the Weston mitigation
are unchanged. D-015, D-016 and D-018 gained RC1 evidence.

**Fixed during 1.5.0 (D-068 to D-081), by PR.**
- PR #518, D-071 (the release notes' rollback).
- PR #519, D-070 (the `.skp` dialog test's sampling window).
- PR #523, D-072 (the sanitized Wayland leak).
- PR #524, D-081 (the STEP export round trip; shipped in 1.4.0).
- PR #527, D-068 and D-069 (the Quantities popover warning and the compiler warnings).
- PR #528, D-073 (the catalog insert race).
- PR #530, D-076 (the 1/3 arc-second post-step tolerance).
- PR #534, D-074 (the decimation loop), D-077 (the decimation ignored the model's room), D-078 (a CRS label with no
  geographic record) and D-079 (the digest taken after the read).
- PR #536, D-080 (the keyboard audit helper).
- PR #540, D-075 (the component edit's draft limits).

**Added at this RC: D-076 to D-081, all fixed before the RC.** D-076 to D-078 and D-081 are Major, and D-079
and D-080 are Minor. D-076 to D-079 are in the GeoTIFF import, which never shipped. D-080 is in a test
helper. D-081 shipped in 1.4.0: the STEP export of the 90,954-face model could not be read back.

**Not carried as defects.** These appear in the 1.5.0 records, and none is a defect with a fix the triage
tracks:
- The refusals of 3DEP tiles as downloaded, for LZW compression and for overview images (R107.b1 and R107.b1.1,
  "Contradictions with the ADR"; R107.b1.2). The records treat each as a decision about the ADR's subset, and
  PRs #530 and #531 changed that subset.
- The tag-type overflow that fuzzing found in the GeoTIFF reader (R107.b1). The fix (commit `f682b30e`) is in
  PR #529's branch, before the merge, so the overflow never reached `main`.
- The zero-offset and origin-outside-zone refusals (R107.b3, items 5 and 6). They tighten validation, and the
  record does not show wrong output for either.
- The binary STL refusal of a UTM-scale DEM terrain (R107.c1, note 6). The record notes a precision boundary,
  and the product behaviour is unchanged.
- The render limits of R099.f ([R099e §12](R099e-presentation.md)). They are the owner's 1.5 follow-up 1.
- The statement in ADR 0172 §6 that BRepMesh is single-threaded ([R098d §15.2](R098d-step-exchange.md)). The
  record calls it wrong, which is a documentation contradiction, not a product defect.

## Release-candidate triage (1.6.0 RC1, 2026-10-31)

The RC is `58afe4a6`, the head of `main` and `release/1.6.0`. Since the 1.5.0 RC (`1ca204fd`), 21 merge commits reached `main`, and 187 files changed.

**Release-blocking: none.** No open row loses or corrupts data, fails recovery, is a critical security issue, crashes or hangs a Core workflow, or fails a Core row. No Major row is open. The 24 open rows are 8 Minor and 16 Limitation. R086.j at the RC passes 24/24 on corpus v6 with no blockers, and no wrong-target mutation, false completion or unreported partial commit was recorded.

**Every open row was checked against the RC.** The check matched each open row's cited files against the 187 files that changed since `1ca204fd`. Six changed files are cited by open rows, and none of the changes alters a row's status or evidence:
- `tests/viewport_tests.cpp` (D-006): failing runs now print their camera counters (2026-10-30). D-006 is still not reproduced.
- `tests/sheet_export_input_tests.cpp` (D-007): the keyboard-focus wait from 2026-10-30, which the D-007 note already records.
- `src/app/window.cpp` (D-019): a status-line connection for the orthophoto import. The D-019 decision is unchanged.
- `scripts/test-wayland.sh` (D-031, D-034, D-040): the 2026-10-31 change (`7ed23545`) turns off Weston's shell animations for every Wayland suite. The flakes note records it as unverified, so these rows stay open with their evidence as before.
- `.github/workflows/native.yml` (D-011): the prior-release package pin moved to 1.5.0. The Wayland decoration setting is unchanged.
- `packaging/arch/PKGBUILD` (D-013, D-029): `pkgver` only.

**D-018 gained RC1 evidence.** [R086.j](R086j-rc160-remote-evaluation.md) passes 24/24 on corpus v6 with no blockers, bounded to the frozen tasks. D-015 and D-016 were checked against the 1.6.0 changes to the presentation batch, the terrain and the reference-image code. No `.skp` file changed, and neither row's claim changes.

**Fixed during 1.6.0, rows already present.** D-082 (Major, PR #555) and D-083 to D-085 (Minor, PR #554) are on `main` and in the 1.6.0 CHANGELOG. The 2026-10-31 flakes change (`7ed23545`, branch `fix/flakes-wayland`) is in the RC and changed no row's status.

**Added at this RC: D-086 and D-087, both open.**
- **D-086 (Limitation).** ADR 0022's D-082 notes name it. The durable outcome store's 64 MiB budget holds the before and the after container, so a model whose saved container is over about 32 MiB stages and previews but cannot commit (`OUTCOME_LIMIT`). The budget is confirmed in `src/io/outcome_store.cpp` (lines 116–118). The ADR's figure is arithmetic, and raising the budget is an owner decision.
- **D-087 (Minor).** ADR 0181's R107.c3.1 notes name it as a known gap (item 3): a retained render re-rendered from an older manifest can burn a terrain's source notice for a terrain outside its view. It is not built.

**Checked, not carried as defects.** These appear in the 1.6.0 records. None is a defect that reached `main` with a fix the triage tracks:
- Three problems that the R107.e2 viewer notes list as caught while it was written: a compile error, a Tab key that the read-only text browser kept, and an audit hop that found the button by its accessible name. The notes say all three were fixed.
- The in-view frame credit and the batch checkbox (R107.c3, R107.c3.1). They are the owner's 2026-10-31 decisions. The R107.c2 gap they close was an open design question (ADR 0181, R107.b2 decision 5), not a defect.
- The order of the orthophoto import's checks (R107.b2.2, item 10). A file with two faults can now report the other fault. Each check still refuses.
- The UX §21 wording that differed from the built dialog. The R107.e2 guide notes resolve it in favour of the code, and UX §21 now states what the code does.

**Open verification, not a row.** ADR 0165's D-084 note says the peer's named cases (a group member, `0` and a scene ID) placed correctly before the fix, and that the one case that did fail was an ID the edit does not show. It asks the peer to re-run its report against the fix. That re-run is outstanding. D-084 stays fixed on the record's evidence.

## Open acceptance work that is not a known defect

These are unaccepted areas, not recorded failures, so they are not classified above.
Before the RC, the list read: release-candidate recovery revalidation and the final data-loss audit
(R083); the all-dialog keyboard workflows (N05); current/reference Arch clean-machine
package checks (R087); the remote provider's release-candidate re-run (R086); and the Core
parity report (R088).

**At the 1.4.0 RC1 (2026-10-28).** The seven 1.5 follow-ups in
[PR_ROADMAP](../PR_ROADMAP.md#15-follow-ups-from-the-140-decisions) are owner decisions, not defects, so
they are not counted above:

1. Render limits for 250,000-triangle terrains: the GLB export's 100,000-face limit and the render
   snapshot's 20,000-body limit (the batch of such a terrain cannot start a frame; [R099e](R099e-presentation.md)).
   **Carried in 1.5.0 (R099.f, PR #526):** a 249,218-triangle terrain and a 19,998-body capture now export and
   render, and a 10,000-placement capture is refused by name. The 19,998-body capture prepares in 1.70 s, over the
   1 s budget, which the M10 record lists as an accepted miss.
2. Catalog import progress and Cancel (R100.g OD-5). **Built in 1.5.0 (R100.i, PR #521):** the insert runs on a
   worker, a progress sheet opens after a second, and a Cancel before the edit is composed leaves the model and the
   library folder unchanged. A Cancel after the edit is composed is too late, and the insert commits.
3. Styles dialog gaps (R100.g OD-11). **Built in 1.5.0 (R100.h, PR #520):** Save report…, a solid, hatched and
   cross-hatched accounting bar with a legend, and refusals that open with "Nothing was saved." or "Nothing was applied."
4. STEP restart-and-exclude. **Built in 1.5.0 (R098.i, PR #522):** a STEP file whose tessellation crashes the helper or
   overruns its limits is restarted without the part it stopped on, at most three times, and the import names that part.
   A sandbox kill, or a stop before tessellation, still refuses the file whole ([R098d](R098d-step-exchange.md#7-findings-for-the-owner) finding 1).
5. STEP conversion speed (R098d finding 9). **Carried in 1.5.0 (R098.j, PR #524):** the 100-sphere import with a native
   file takes 10.17 s against 10 s (it was 14.70 s), and the 90,954-face export takes 9.96 s against 5 s (it was 16.56 s).
   Both misses are accepted limits, as the M10 record lists them. The export file is 100.6 MB, not 224 MB (D-081).
6. buildingSMART hosted validation of the IFC export: skipped for 1.4.0 (R097.f); the export claim
   rests on the local rules.
7. A STEP entity cap and its geometry threads. **Carried in 1.5.0 (R098.k, PR #525):** a STEP file with more than
   2,000,000 entities is refused before any transfer, with the count and the limit named. STEP keeps one tessellation
   thread, because the parallel path gave no speed-up on the 100-sphere model ([R098d](R098d-step-exchange.md) §15.2).

**At the 1.5.0 RC1 (2026-10-30).** The six 1.5 follow-ups that M10 lists (R098.i, R098.j, R098.k, R099.f, R100.h and
R100.i) are built or carried as accepted limits, as the items above say. R107.b2 (orthophoto placement) and R107.d
(geocoding) are not in 1.5.0, and C06 stays Investigate.

Owner decisions and limits from the 1.4.0 records that are not on that list:

- IFC import of a 10,000-body file takes about 12 s against a 10 s budget. The owner accepted it as a
  documented limit for 1.4.0 ([R097.f](R097f-ifc-exchange.md)).
- A curved STEP import stops at about 100,000 triangles, because a document holds 100,000 unique
  faces ([R098d](R098d-step-exchange.md#7-findings-for-the-owner) finding 3). The owner decides
  whether the import should merge or sample. ADR 0172 §6's 1,000,000-triangle total is not reachable
  by any document ([R098d §12.8](R098d-step-exchange.md)).
- The STEP export's triangle-warning threshold is unchanged, pending the owner
  ([R098d](R098d-step-exchange.md#7-findings-for-the-owner) finding 4).
- The edge stroke's cost on the reference hardware is unknown (R100.g F-4, OD-7).
- The `.skp` "newer than tested" label applies above 2024, as ADR 0176 §2 says. The owner has not
  separately confirmed that threshold ([R102e §12](R102e-skp-import.md#12-owner-decision-2026-10-28)).

**At the RC (2026-10-13)** each has RC evidence: the recovery, publication-fault and
transaction suites in CTest and the sanitizer job, and R086.ca recovery on 24/24 runs (R083);
the all-dialog audit, 64/64 (N05); package acceptance and three clean machines (R087); the
remote corpus (R086.ca); and the parity report ([R088.f](R088f-core-parity-report.md)). The
two R083 checks the walkthrough did not run, the coverage-guided parser fuzzing campaign and
the sanitized run of five of the 13 failure-matrix suites, were re-run at the RC in
[R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md): 13/13 sanitized suites pass, and the
campaign has no new finding beyond D-022.

Waived by the owner on 2026-10-12 ([ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)),
untested and not claimed to pass: Intel campaigns and the sharing-off hardware control (R082);
Xcb hotplug, suspend/resume, input methods and automatic X11 placement (R085, N04); physical
150% scale (N04); physical pointer capture and a real keyboard under Hyprland (N03); and the
interactive Orca session (N05). Wayland hotplug is owner-attested
([R085.r](R085r-hotplug-owner-attested.md)).

Closed by the D-022 fix (2026-10-18, branch `test/d022-fuzz-asan`):

- **D-022** (parser fuzz targets abort on their first seed under ASan). Class: **Minor** (test
  tooling). Found by R083.e, unchanged at the RC (R083.h). **Cause.** Qt's JSON parser sorts
  object members with `std::stable_sort`; libstdc++'s temporary buffer, compiled into
  `libQt6Core.so.6` (`+0x241dbc` calls `_ZnwmRKSt9nothrow_t`, `+0x241f11` calls `_ZdlPvm`),
  allocates with `operator new(size_t, std::nothrow_t const&)` and releases with
  `operator delete(void*, size_t)`, a matching pair. Clang's `-fsanitize=fuzzer` link puts
  `libclang_rt.fuzzer-x86_64.a` ahead of `libclang_rt.asan_cxx-x86_64.a`. The fuzzer archive
  carries a private libc++ (`std::__Fuzzer`) whose plain, array and aligned `new` and every
  non-nothrow `delete` are weak and exported, so they win and call `malloc`/`free`; its nothrow
  `new` is local, so the nothrow `new` resolves to ASan's interceptor, which records the block as
  `operator new`. The sized delete then reaches `free`, and ASan reports `operator new vs free`.
  A plain ASan program has no fuzzer archive and reports nothing, which is why
  `parser_fuzz_seeds` and the sanitized suites pass. Neither Qt nor SketchyUp mismatches; the
  libstdc++/libc++ and `-fsized-deallocation` angles do not apply, because both calls are in
  Qt's own GCC-built code. PR #350's shared libraries do not reach this build: it sets only
  `SKETCHYUP_SANITIZE`, which the fuzz configuration does not use. **Fix.** The fuzz targets
  link `tests/fuzz/fuzzer_allocation.cpp`, which defines the eight nothrow `new`/`delete`
  forms on top of the ordinary operators, so every allocation in the target uses one family.
  `alloc_dealloc_mismatch` stays enabled; the application's sanitized suites are unchanged.
  **Verified** ([R083.i](R083i-fuzz-asan.md)): the same target relinked without the file still
  aborts on `{"":2}` and on the `empty-document` seed; with it, all 11 targets pass every seed
  and run 60 s each under the documented options with no finding. **Trade-off.** In a libFuzzer
  link, ASan cannot see new/delete-family mismatches in any code, ours included (a `new[]`
  freed with `free` is silent there, but reported in a plain ASan build); this was already so
  before the fix, and the sanitized test suites keep that check.

Closed by the D-039 fix (2026-10-17, branch `perf/d039-variant-geometry-count`):

- **D-039** (`checkUniqueGeometry` costs O(variant placements × members) per edit). Class:
  **Minor** (a performance cost, no wrong result). Found by R094.g. **Cause.** R093.b
  (`74f6d509`) computes unique geometry (ADR 0165 §3) by adding, for every placement that shows
  a variant, its template's records and subtracting the variant's own records, and it
  recounted the variant's member records each time, on every edit. **Fix.** The immutable
  variant index (`ComponentVariantIndex`) also holds `placementUses`, the number of placements
  that show each variant object, kept by the same `walk` that fills `placements` when the
  index is built or extended. The check (`uniqueGeometryCounts` in `src/core/model.cpp`) adds
  each distinct variant's contribution once, multiplied by its placement count, and counts
  each variant's records once per check. The arithmetic is the same integer sum, so the
  limits, refusals, `LIMIT_EXCEEDED` details and messages are unchanged, and each distinct
  variant still counts once. Every document change already rebuilds or restores the index
  (apply, undo, redo, amend, load, make-unique, promote, delete, copy, override, reset and
  default changes), so the counts need no separate upkeep; `verifyIncrementalState` (the
  full-validation oracle) now also recounts them. **Verified.** `component_parameter_tests
  unique-geometry` keeps the R093.b algorithm as a test oracle and compares it with every
  check (through a test-only observer) over random edit sequences with undo, redo, amend,
  load, make-unique, promote, delete, copy, override, reset and default changes: four seeds
  with the full-validation oracle on, and one near the 100,000-vertex budget where the
  unique-geometry limit refuses edits. Counts and decisions are identical. A Release
  micro-benchmark that builds the R094.g `variant` family (a 26-sided prism stretched to
  2.001 m, placed by copy array and row copies) and times one placement move per apply
  (median of 300, pinned to one CPU, before and after interleaved on a shared host at load
  average 13–18): 10,000 variant placements went from 2.01–2.32 ms to 0.67–0.71 ms, the
  same as 10,000 default placements (0.70 ms); 1,000 went from 0.46–0.56 ms to 0.39–0.44 ms.
  **Committed check.** `variant_edit_cost` (`tests/variant_edit_cost_tests.cpp`, in the default
  `ctest` run) times single-placement moves at 10,000 variant and 10,000 default placements and
  fails if the variant edit costs more than 1.8 times the default edit.

Closed by the D-027 fix (2026-10-18, branch `test/d027-d028-tooling`):

- **D-027** (`verify-m6-offline.sh` failed in its final summary when `git` was missing). Class:
  **Minor** (test infrastructure). **Cause.** The summary's `git()` helper called
  `subprocess.run(['git', ...])` without handling `FileNotFoundError`; the M5 script catches it.
  **Fix.** The M6 helper now catches `FileNotFoundError` and returns `None`, the same as for a
  non-zero exit, and the summary reads the commit and the status once, as the M5 script does.
  `sourceCommit` is `null` and `sourceDirty` is `null` (not `false`) when `git` is absent, the
  same fields and values as `verify-m5-offline.sh`. **Verified.** The summary block, run on
  synthetic evidence with a `PATH` holding only `python3`: the previous script raises
  `FileNotFoundError: … 'git'`; the fixed script completes and writes `"sourceCommit": null`
  and `"sourceDirty": null`. With `git` on the `PATH` it writes the commit and a boolean as before.

Closed by the D-028 fix (2026-10-18, branch `test/d027-d028-tooling`):

- **D-028** (`visible_focus_input_tests` exceeded the 60 s Wayland harness limit at 2× on one
  CPU). Class: **Minor** (test infrastructure). **Cause.** `scripts/test-wayland.sh` fixed the
  deadline at 60 s, while the suite takes 14–39 s with several CPUs and over 60 s with one.
  **Fix.** A per-line deadline, not a split: `scripts/test-wayland.sh` reads an optional
  `SKETCHYUP_TEST_DEADLINE` (integer seconds, default 60, 60 to 180) and exits 2 with a message
  for anything else. `native.yml` sets `SKETCHYUP_TEST_DEADLINE=120` on the Wayland
  `visible_focus_input_tests` lines only (Model and `--workspace=sheets`, at 1×, 2× and
  fractional 1.5×). Every other line keeps 60 s. The sanitized job does not run this suite.
  **Verified.** In `sketchyup-ci-pinned:r094a`: 30, 59, 181, 999, `abc`, `1.5` and `-5` are
  rejected with exit 2. A program that sleeps 61 s times out at the 60 s deadline by default and
  passes with `SKETCHYUP_TEST_DEADLINE=120`. The suite's Wayland 1× and 2× lines pass (see the
  pull request).

## Counts

| Class | Count | IDs |
| --- | ---: | --- |
| Release-blocking | 0 | None |
| Major | 0 | None |
| Minor | 8 | D-006–D-009, D-031, D-034, D-040, D-087 |
| Limitation | 16 | D-001, D-004, D-005, D-010–D-019, D-020, D-029, D-086 |
| **Total open** | **24** | |
| Fixed since the 2026-10-11 refresh | 61 | D-088 (Major, found at the 1.6.0 RC1 and fixed before the release), D-082 (Major, found 2026-10-31 and fixed in PR #555), D-003, D-024 (added and fixed 2026-10-12), D-025 (fixed by #301), D-026 (added by R084.u and fixed by R084.v, 2026-10-13), D-035 (Major, added and fixed 2026-10-15), D-036 (Minor, added at the 1.1.0 RC and fixed by 1.1.1), D-037 (Major, added at the 1.2.0 RC and fixed 2026-10-17), D-041 (Major, added at the 1.2.0 RC2 and fixed at RC3), D-038 (Minor, added at the 1.2.0 RC and fixed 2026-10-17 after 1.2.0), D-039 (Minor, added at the 1.2.0 RC and fixed after 1.2.0), D-021 (Minor, fixed 2026-10-17 after 1.2.0), D-032 (Minor, added by R091.h and fixed 2026-10-17 after 1.2.0), D-027 and D-028 (Minor, added at the RC and fixed 2026-10-18), D-022 (Minor, fixed 2026-10-18 in the fuzz harness), D-023 (Minor, fixed 2026-10-18), D-042 (Release-blocking, added and fixed 2026-10-18), D-043 (Major, added and fixed 2026-10-19), D-045 (Minor, added by R095.f and closed 2026-10-19 by restating the budget), D-044 (Minor, added by R095.f and fixed 2026-10-19), D-046 (Minor, added at the 1.3.0 RC1 and fixed 2026-10-21), D-047 (Major, added at the 1.3.0 RC2 and fixed 2026-10-21), D-048 (Major, reclassified 2026-10-27 by the parent; fixed in PR #498, merged 2026-10-27), D-049 to D-067 (1.4.0 RC1 rows; see the list in the RC section), D-070 (Minor, added at the 1.4.0 RC1 and fixed on `main` after 1.4.0 by #519), D-071 (Minor, added at the 1.4.0 RC and fixed on `main` after 1.4.0), D-072 (Minor, added and fixed on `fix/d072-wayland-leak`, 2026-10-29), D-068 and D-069 (Minor, fixed by PR #527, 2026-10-30), D-073 (Major, added and fixed 2026-10-30, the catalog insert race), D-074 (Major, added and fixed 2026-10-30, the GeoTIFF decimation loop), D-075 (Major, added and fixed 2026-10-30, the component edit draft and orphaned definitions), D-076 to D-081 (added and fixed before the 1.5.0 RC: D-076, D-077, D-078 and D-081 Major, D-079 and D-080 Minor; see the 1.5.0 RC section), D-083 to D-085 (Minor, added and fixed 2026-10-31 by PR #554; see the counts) |

At the RC (2026-10-13): 24 open (0 release-blocking, 0 Major, 10 Minor, 14 Limitation).
R091.h (2026-10-14) added D-031 and D-032 (Minor). 2026-10-15 added D-034 (Minor). D-035
(Major, 1.1.0 RC) was added and fixed on 2026-10-15, so it is not open. D-036 (Minor, Qt 6.8
only) was added on 2026-10-15 by the Qt 6.8 reference at the 1.1.0 RC, shipped in 1.1.0 and is
fixed by 1.1.1, so it is not open.
At the 1.2.0 RC `71a6b4f3` (2026-10-17): 31 open (0 release-blocking, 1 Major, 16 Minor, 14
Limitation). The gate added D-037 (Major: 1.2.0 does not build on Qt 6.8.0, the declared
minimum); the F02 follow-ups D-038 and D-039 and the CI Weston crash D-040 are Minor. D-037 needs
an owner decision and a new RC ([F02-rc-1.2.0](F02-rc-1.2.0.md#superseded-rc1-71a6b4f3)).
At 1.2.0 RC2 `d80fb729` (2026-10-17): 31 open (0 release-blocking, 1 Major, 16 Minor, 14
Limitation). D-037 is fixed and verified on the Qt 6.8 reference; D-041 (Major) was added by the
RC2 corpus run, and its fix is pending as RC3.
At 1.2.0 RC3 `d9af0396` (2026-10-17): 30 open (0 release-blocking, 0 Major, 16 Minor, 14
Limitation). D-041 is fixed.
After the D-038 fix (2026-10-17, branch `fix/d038-paint-digest`): 29 open (0 release-blocking,
0 Major, 15 Minor, 14 Limitation).
After the D-039 fix (2026-10-17): 28 open (0 release-blocking, 0 Major, 14 Minor, 14
Limitation). D-038 and D-039 are fixed on `main`; 1.2.0 still has both.
After the D-021 fix (2026-10-17, branch `test/wayland-activation-exposure`): 27 open (0
release-blocking, 0 Major, 13 Minor, 14 Limitation).
After the D-032 fix (2026-10-17, branch `fix/d032-fractional-focus`): 26 open (0
release-blocking, 0 Major, 12 Minor, 14 Limitation).
After the D-027 and D-028 fixes (2026-10-18, branch `test/d027-d028-tooling`): 24 open (0
release-blocking, 0 Major, 10 Minor, 14 Limitation).
After the D-022 fix (2026-10-18, branch `test/d022-fuzz-asan`): 23 open (0 release-blocking,
0 Major, 9 Minor, 14 Limitation).
After the D-023 fix (2026-10-18, branch `fix/d023-focus-contrast`): 22 open (0
release-blocking, 0 Major, 8 Minor, 14 Limitation).
After the D-042 fix (2026-10-18, branch `fix/clipper2-tessellation-hang`): 22 open (0
release-blocking, 0 Major, 8 Minor, 14 Limitation). D-042 was added and fixed on the same branch.
After the D-043 fix (2026-10-19, branch `fix/d043-terrain-history-budget`): 22 open (0
release-blocking, 0 Major, 8 Minor, 14 Limitation). D-043 was added and fixed on the same branch.
After R095.f (2026-10-19, branch `docs/r095f-terrain-acceptance`): 23 open (0 release-blocking,
0 Major, 9 Minor, 14 Limitation). R095.f added D-044 and D-045 (Minor); D-045 was closed on the
same branch when the owner restated the file-growth budget as the mesh chunk.
After the D-044 fix (2026-10-19): 22 open (0 release-blocking, 0 Major, 8 Minor, 14
Limitation). This line and the table were brought up to date on 2026-10-21; the D-044 row
already said fixed.
After the D-046 fix (2026-10-21, branch `fix/provider-stream-error-detail`): 22 open (0
release-blocking, 0 Major, 8 Minor, 14 Limitation). D-046 was added and fixed on the same branch.
After the D-047 fix (2026-10-21, branch `fix/d047-qt68-includes`): 22 open (0 release-blocking,
0 Major, 8 Minor, 14 Limitation). D-047 (Major) was added and fixed on the same branch.
At the 1.3.0 RC3 `d69c4402` (2026-10-21): 22 open (0 release-blocking, 0 Major, 8 Minor, 14
Limitation); none blocks 1.3.0 ([F03-rc-1.3.0](F03-rc-1.3.0.md#triage-at-rc3)). The gate added
D-046 (Minor, RC1) and D-047 (Major, RC2); both are fixed at RC3.

After the D-048 record (2026-10-27, branch `investigate/f22-edit-latency`): 23 open (0
release-blocking, 0 Major, 9 Minor, 14 Limitation). D-048 was new, Minor and provisional.
After the D-048 fix (2026-10-27, branch `fix/d048-outliner-rebuild`, not yet merged): 22 open (0
release-blocking, 0 Major, 8 Minor, 14 Limitation). The parent reclassified D-048 Major, and the
fix on that branch closes it. PR #498 merged it on 2026-10-27.

At the 1.4.0 RC1 `9ed43c90` (2026-10-28): 24 open (0 release-blocking, 0 Major, 10 Minor, 14
Limitation). Release-blocking: none. The 1.4.0 rows D-049 to D-067 are fixed on `main`, and D-068 and
D-069 are open Minor rows. RC1's full tier then added D-070 (Minor, a test sampling window), and the RC
walkthrough added D-071 (Minor, a missing rollback step), so at the RC record (2026-10-29): 26 open
(0 release-blocking, 0 Major, 12 Minor, 14 Limitation).
After the 1.4.0 release (2026-10-29): 25 open (0 release-blocking, 0 Major, 11 Minor, 14
Limitation). D-071 is fixed on `main` and in the GitHub release's notes; 1.4.0's installed notes keep
the old text. The 1.4.0 items that are not known defects are listed under
[Open acceptance work](#open-acceptance-work-that-is-not-a-known-defect).

After the D-070 fix (2026-10-29, PR #519, branch `fix/d070-skp-dialog-sampling`): 24 open (0 release-blocking, 0 Major, 10 Minor, 14 Limitation). D-070 was added at the RC1 full tier and is fixed by the test-only change in that PR; D-071 was fixed on `main` by the 1.4.0 publication PR; D-068 and D-069 stay open.

After the D-072 fix (2026-10-29, branch `fix/d072-wayland-leak`): 24 open (0 release-blocking, 0 Major, 10 Minor, 14 Limitation). D-072 was added and fixed on the same branch.
After the D-073 fix (2026-10-30, branch `fix/d073-catalog-insert-race`): 22 open, unchanged (0 release-blocking, 0 Major, 8 Minor, 14 Limitation). D-073 (Major) was added and fixed on the same branch: the catalog insert listened to `QThread::finished` after starting the thread and could miss it.
After the D-074 fix (2026-10-30, PR #534): 22 open, unchanged. D-074 (Major) was found and fixed in R107.b3 before any release carried it.

After the D-068 and D-069 fixes (2026-10-30, PR #527, branch `fix/d068-d069-warnings`): 22 open (0 release-blocking, 0 Major, 8 Minor, 14 Limitation). Both were Minor and are fixed on that branch.

After the D-075 fix (2026-10-30, PR #540): 22 open, unchanged (0 release-blocking, 0 Major, 8 Minor, 14 Limitation). D-075 (Major) was added and fixed on the same branch: a component edit's draft counted the edited definition twice, and an explode or make-unique left the replaced definition counted.

At the 1.5.0 RC1 `1ca204fd` (2026-10-30): 22 open (0 release-blocking, 0 Major, 8 Minor, 14 Limitation). D-076 to D-081 were added and fixed before the RC, so the open count is unchanged.
After the 1.5.0 release (2026-10-31, [G-1.5.0](G-1.5.0.md)): 22 open, unchanged (0 release-blocking, 0 Major, 8 Minor, 14 Limitation). The fixes listed above are in that release; D-081 had shipped in 1.4.0.
After the 2026-10-31 flakes review (branch `fix/flakes-wayland`): 22 open (0 release-blocking, 0 Major, 7 Minor, 15 Limitation). D-019 is decided as a Limitation; D-031, D-034 and D-040 stay open as Minor, with the evidence above. No row is marked Fixed, so no CHANGELOG line is added.
After the D-082 fix (2026-10-31, [PR #555](https://github.com/zygote55/sketchyup/pull/555)): 22 open, unchanged (0 release-blocking, 0 Major, 7 Minor, 15 Limitation). D-082 (Major) was found by a peer session's report on 2026-10-31 and is fixed on that branch; the fixed count rises to 57.

After the D-083 to D-085 fixes (branch `fix/d083-command-parents`, PR #554, 2026-10-31): 22 open, unchanged (0 release-blocking, 0 Major, 7 Minor, 15 Limitation). D-083 to D-085 were added and fixed on that branch, each Minor with a workaround, so the open count is unchanged.

At the 1.6.0 RC1 `58afe4a6` (2026-10-31): 24 open (0 release-blocking, 0 Major, 8 Minor, 16 Limitation). D-086 (Limitation) and D-087 (Minor) were found by the 1.6.0 records and are open. No existing open row changed status, and the fixed count is unchanged.

After the D-088 fix (2026-10-31, branch `fix/d088-cli-version`): 24 open, unchanged (0 release-blocking, 0 Major, 8 Minor, 16 Limitation). D-088 (Major) was found by the RC1 package acceptance and fixed before the release; RC1 is replaced by RC2.

Before the RC update (2026-10-13): 21 open (0 release-blocking, 0 Major, 8 Minor, 13
Limitation). The RC update added D-027 and D-028 (Minor) and D-029 (Limitation).

Before this refresh (2026-10-11): 22 (0 release-blocking, 1 Major, 11 Minor, 10 Limitation).
D-003 is fixed. D-004 moved from Major to Limitation, and D-005 and D-020 from Minor to
Limitation, by the owner's evidence waiver. D-024 (Major) and D-025 (Minor) were added;
R082.jj then fixed D-024. **2026-10-13:** #301 fixed D-025, and R084.u added D-026 (Minor),
which R084.v then fixed.

**2026-10-30 (branch `fix/flakes-x11`).** D-006, D-007 and D-008 were investigated and stay Open; no row changed
status, so the counts above are unchanged.

No release-blocking or Major defect remains at the RC. This list is the triage evidence; the
release decision is recorded in [M9](M9.md). D-024 is
fixed on AMD, so the 10,000-placement edit-feedback miss no longer stands against the M9 exit
criterion "performance budgets pass"; the criterion is not thereby accepted. D-004, D-005 and D-020 are Limitations because the checks
that would settle them are waived, not because they were shown harmless. No known open critical
security defect was found. No data-loss defect is confirmed open. D-002 was root-caused as a
test-harness hang on a full CI disk ([R083.f](R083f-file-worker-hang.md)) and its
unexpected-modal guard is merged ([R083.g](R083g-unexpected-modal-guard.md)); no open hang or
data-path candidate remains other than D-034, a single unexplained test timeout whose next
occurrence the harness will trace.
