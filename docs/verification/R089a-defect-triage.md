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
([F02-rc-1.2.0](F02-rc-1.2.0.md#triage-at-the-rc)) adds D-037 (Major, found by the gate): 1.2.0
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
**Updated 2026-10-17 (D-032 fixed; branch `fix/d032-fractional-focus`).** Focus rings at the
fractional 1.5× factor are snapped to whole device pixels by one application-wide filter, and
`visible_focus_input_tests` (Model and Sheets) runs at 1.5 on X11 and Wayland in the
fractional-scaling CI job; see "Closed by the D-032 fix" below and the counts.
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
| D-006 | Intermittent X11 viewport assertion "Camera movement only reorders transparency" | [R085.i](R085i-assisted-output-transitions.md#retained-attempts-and-validation), [R085k X11 log](R085i-r085k-xcb.txt) | N01, P02 | **Unverified.** R085.i retains it as "intermittent … remains unexplained". The assertion (`tests/viewport_tests.cpp`) is unchanged, and no later record explains it. | Minor | The failed check is a render-statistics assertion: either geometry was re-uploaded or transparency was not re-sorted after a camera move. The worst case is an extra upload or a transient blend-order glitch, with no document effect. | [R085](../PR_ROADMAP.md#r085) (viewport cache, with [R082](../PR_ROADMAP.md#r082)): investigation; possibly a code fix |
| D-007 | Wayland native-test focus and activation races | [R015](R015-topology.md), [R035.a](R035a-entity-measurements.md), [R084.p](R084p-dialog-keyboard.md), [R087.t](R087t-native-ci-reproduction.md), [PR #273](https://github.com/zygote55/sketchyup/pull/273), [main run 38011483053](https://github.com/zygote55/sketchyup/actions/runs/38011483053), [PR #328 run 38747320047](https://github.com/zygote55/sketchyup/actions/runs/38747320047) | N03 | **Unverified.** Recurring intermittent fixture failures where a window reports active before its native surface has focus. Examples: the R015 Extrude shortcut, the R087.t "Tab reaches shortcut control without assigning focus", and the entity-info Wayland 2× focus timeout on `main` before #273. Each received a fixture wait or re-request; none has a confirmed root cause. Three `main` runs have passed since #273. **2026-10-15: one instance root-caused and fixed.** PR #328 (`4767b538`) failed `SKETCHYUP_TEST_SCALE=1 scripts/test-wayland.sh build/dev/sheet_export_input_tests` with "Tab stays in the dialog looking for sheetExportAllowChanged; focus none, active window none" (the check message is printed; this is not D-031). It is the R091.h Qt Wayland client race ([R091.h round 3](R091h-sheet-layout.md#round-3-focus-after-choose-scene-on-wayland)) on the export dialog's PDF file chooser, which the test accepts as soon as it shows: in the traced failures Weston sends `wl_keyboard.enter` for the chooser and the client asks for a `wl_display.sync`, the chooser is destroyed about 4 ms later, before `done`, and Qt then queues "no focus window" over the export dialog's own `enter`. Reproduced at `68ff9ae2` in the CI image, Wayland 1×, four lanes, `WAYLAND_DEBUG=client`: 7 of 64 runs, every one with this message. The chooser and the print dialog are now `Settled<QFileDialog>` and `Settled<QPrintDialog>` (`sheet_export_dialog.cpp`): after, 0 of 100 traced runs under the same load, and 39 of 40 untraced runs passed at Wayland and X11 1× and 2× (the one failure lost its Xvfb when two local lanes started `xvfb-run -a` at once). The other D-007 instances keep no confirmed cause. | Minor | These are test-infrastructure races in isolated Weston. No product focus defect has been shown. | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only) |
| D-008 | Scene-transition native test timed out in CI under delayed events | [R085.d](R085d-scene-animation-fixture.md) | P03 | **Unverified.** CI waited 1.5 s for the saved pose and failed. The wait is now 5 s, and the cause is recorded as "not a confirmed root cause". Product timing is unchanged. | Minor | Eventual-correctness timing in a test fixture. Final-pose equality is still enforced. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-009 | X11 hardware fixture cannot place its window on a requested output without a compositor helper | [R085.h](R085h-physical-outputs.md), [R085.i](R085i-assisted-output-transitions.md) | N04 | **Open.** Both Qt-only X11 placement attempts fail the requested-output assertion. X11 transitions qualify only through the Hyprland-specific helper. | Minor | Test infrastructure: the placement is of the fixture's own window, as Qt documents for `setScreen`. Rendering on both outputs passes once placed. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-019 | Qt Wayland protocol error when re-showing a window hidden during a file operation | [R083.f](R083f-file-worker-hang.md) | N01 | **Open.** Observed in 6 of 8 probe runs at 1×: the probe hid the window, ran a file operation, then showed the window again. The application never hides its main window during a file operation. | Minor | The path is not reachable from current product code. Recorded so a future hide/restore feature retests it. | [R085](../PR_ROADMAP.md#r085) |
| D-021 | Wayland test-window exposure timeouts under host load | [R083.g](R083g-unexpected-modal-guard.md), [R084.r](R084r-display-precision.md), [R084.s](R084s-keyboard-panel-tabs.md), [R085.q](R085q-keypad-decimal.md), [R089.d](R089d-release-candidate.md#failures) | N03, N01, P05 (test harness) | **Unverified.** In local bounded-container Wayland 2× runs, fixtures fail in `qWaitForWindowExposed` before any test step: 2 of 4 `units_input_tests` runs (load average about 28), 3 of 6 `file_operation_input_tests` runs (load above 30; 6/6 passed at about 15), five Wayland 2× cases in the R084.s matrix (style, solar, reference image, annotation, audit) and one `numeric_input_tests` run. Identical reruns pass without a changed wait. These records attribute none to CI. The cause is not established beyond host load. **At the RC (2026-10-13)**, on the one-CPU runner: `annotation_input_tests` Wayland 2× failed its "Annotation window exposed" check once, and `interaction_tests` Wayland 2× exited 2 once (window not exposed and active within 5 s; the "active" part overlaps D-007). Both passed on re-run. CI run 38435734443 passed the annotation line; CI runs `interaction_tests` on X11 only. **At the 1.2.0 RC (2026-10-17)**: the F02 integration saw the same "window exposed" timeout in CI at roughly 1% of Wayland lines (a figure from the F02 integration's CI history, not counted line by line here); each passed on re-run. CI run 39022075019 at the RC's pre-merge head passed every line, and the 47 local non-CI cells at the RC are recorded in [F02-rc-1.2.0](F02-rc-1.2.0.md). | Minor | Test infrastructure. The failure precedes any product code in the test and is not a focus defect, so it is distinct from D-007 (focus lost after exposure). | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only): load gating or a bounded retry |
| D-022 | Parser fuzz targets abort on their first seed under ASan with Qt 6.11.2 JSON parsing (F1) | [R083.e](R083e-parser-fuzzing.md) | D02, D06 (test tooling) | **Unverified.** Six of ten libFuzzer targets (native JSON, container, GLB, template, component, extension) abort on an unmodified valid seed with an alloc-dealloc mismatch whose stacks lie inside `libQt6Core.so.6` under `QJsonDocument::fromJson`. A three-line program that calls only `fromJson` reproduces it. `parser_fuzz_seeds` passes every seed through the instrumented parsers, and the six targets show no crash, leak or hang with that one check disabled. The record states that the root cause is not established and the finding stays open. **At the RC (2026-10-13)** the campaign was re-run with schema 26 seeds ([R083.h](R083h-rc-fuzzing-and-sanitized-matrix.md#f1-at-the-rc)): the same six targets stop on an unmodified seed with the same Qt-internal stacks, R083.e's minimized `{"":2}` reproduces, and with only that check disabled all six run to time with no other finding. | Minor | The evidence points to a Qt/libFuzzer/ASan interaction, not application code, but the six targets do not meet the documented fuzz options and the campaign is not in CI. | [R083](../PR_ROADMAP.md#r083): resolve F1 or document the option; continuous fuzzing |
| D-023 | Focus-ring contrast residuals next to the 3D canvas and native scroll bars | [R084.q](R084q-visible-focus.md), [R084.s](R084s-keyboard-panel-tabs.md) | N05 | **Open.** R084.q records that the dark-theme ring has 1.64:1 contrast against the light default canvas where the tool rail, breadcrumb and assistant container touch it; the canvas's own ring is 2.997:1 against dark chrome; native scroll bars are not themed; 14 controls per run are wider than their scroll area at 200% text. R084.s leaves one or two tab-bar scroll buttons in the Tab order between the tab bar and its page; [R084.v](R084v-keyboard-reachability.md) removes them from the Tab order. | Minor | Cosmetic and keyboard-friction issues. Every ring keeps at least 3.93:1 against the colour directly inside it, and no control is unreachable. A mid-tone ring cannot satisfy all cases, so the next step is a design decision. | [R084](../PR_ROADMAP.md#r084) (UX decision) |
| D-027 | `verify-m6-offline.sh` fails in its final summary when `git` is missing | [R089.d](R089d-release-candidate.md#failures) | E04–E07, O03 (study tooling) | **Open.** At the RC, attempt 1 completed every step, including 190/190 regressions, then raised `FileNotFoundError: … 'git'` in the summary. `verify-m5-offline.sh` handles a missing `git`; the M6 script does not. Attempt 2, with `git` installed from the same pinned archive, passed. No code was changed. | Minor | Test infrastructure only: every study check passed before the summary failed, and the study passes when `git` is present. | Study tooling: record a null source commit when `git` is absent, as the M5 script does |
| D-028 | `visible_focus_input_tests` exceeds the 60 s Wayland harness limit at 2× on one CPU | [R089.d](R089d-release-candidate.md#failures) | N05 (test harness) | **Open.** Three attempts on the one-CPU runner ended at 60.2–60.3 s in `test-wayland.sh` with no failed check. The same build passed on six CPUs (Wayland 1× 14.4 s, 2× 28.4 s), and CI run 38435734443 passed the same line. | Minor | Test infrastructure: a harness timeout under a one-CPU quota. No check failed, and the suite passes with more CPU and in CI. | Test-only: a per-suite limit for this suite, or a split |
| D-031 | `keyboard_reachability_tests` exits silently on Wayland with a Weston "finalizing a layer with views still on it" BUG line | [R091.h](R091h-sheet-layout.md#defect-note-d-031), [PR #320 run 38605636463, attempt 1](https://github.com/zygote55/sketchyup/actions/runs/38605636463/attempts/1) (job 115995661310) | N03 (test harness) | **Unverified.** Once, in CI on PR #320 (R091.d, `5bf306b8`), attempt 1: the Wayland 1× line `SKETCHYUP_TEST_SCALE=1 scripts/test-wayland.sh build/dev/keyboard_reachability_tests` printed "Side tabs: … passed", then, 7 s after Weston started, the only further output was Weston's `BUG: finalizing a layer with views still on it.` and the step failed with exit 1. No check message was printed. Attempt 2 on the same commit passed every job, and R091.h's local matrix passed the suite on Wayland. R091.d changed no desktop code. The failure is in the window phase that follows "Side tabs" (Render with Blender reopening), with no application message, so the cause (a client crash, a compositor shutdown race, or the harness ending Weston early) is not established. | Minor | Test infrastructure: no check failed and no product code path is implicated; distinct from D-021 (exposure timeout with a named check) and D-007 (focus lost after exposure, with a named check). | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only): keep the Weston log and a core dump on failure |
| D-032 | Focus ring under the threshold at fractional 1.5× | [R091.h](R091h-sheet-layout.md#native-matrix) | N05 | **Fixed 2026-10-17** (see "Closed by the D-032 fix"). Was: `visible_focus_input_tests` at `QT_SCALE_FACTOR=1.5` failed on X11 and Wayland (143 and 135 measurements in the Model run, 88 and 90 in the Sheets run, in `sketchyup-ci-pinned:r094a`): tool buttons, panel tab bars, the breadcrumb and controls in the material, solar, drawing-plane and assistant-preference dialogs. Qt's style sheet style paints every border antialiased, so a ring edge in the middle of a device pixel left that pixel a blend of the ring and the chrome beside it, under 3:1. A 2 px ring (R091.h) has the same part-covered edge pixel. Now 0 failures at 1×, 1.5× and 2× on both platforms, both themes. | Minor | A rendering residual of a 1 px indicator at a fractional factor; the ring is still drawn and keyboard focus is unaffected. Every 1× and 2× cell passes. | [R084](../PR_ROADMAP.md#r084): a 2 px ring or device-pixel-aligned focus drawing, with the suite at 1.5 in CI |
| D-034 | `render_input_tests` timed out on Wayland 2× with no output | [main run 38742605671](https://github.com/zygote55/sketchyup/actions/runs/38742605671) (job 116447893819, `68ff9ae2`, after #327) | N03 (test harness) | **Unverified.** Once on `main`: the section-fixture line `SKETCHYUP_TEST_SCALE=2 SKETCHYUP_RENDER_SECTION_FIXTURE=1 SKETCHYUP_RENDER_MODEL=…/section-nested.sketchyup scripts/test-wayland.sh build/dev/render_input_tests` printed nothing for 60 s and Weston was stopped. Blender is not involved: Wayland lines set no `SKETCHYUP_BLENDER_TEST`, so the fake worker runs. The silence does not place the hang: the suite prints one line, at its end, to `std::cout`, which is block-buffered into the CI pipe, so a hang anywhere in its 8–11 s flow, or at exit, prints nothing; the four "could not load cursor" lines are Weston's own keyboard and shell clients, not the test. No other `render_input_tests` timeout is in the 80 failed Native build runs since 2026-10-07, while the line has run on every run since 2026-10-06. Locally, at `68ff9ae2` in the CI image without Blender: 0 of 740 runs of the exact line hung (540 with six parallel lanes, 300 of them through the new harness; 200 with two). One six-lane run failed a 5 s "First native queued worker starts" wait under that load, which is not this signature. | Minor | One CI occurrence with no check failed and no frame known; treated as test infrastructure until a stack shows otherwise. | The harness now prints every thread's stack and flushes stdio at the deadline ([Harness diagnostics](#harness-diagnostics-d-034)); a recurrence names the frame, and the fix goes to its owner |
| D-041 | The ChatGPT-plan decoder rejects replies that batch `transaction.inspect` with `transaction.diff` | [F02-rc-1.2.0](F02-rc-1.2.0.md#rc2-provider-corpus-d-041) | A04, A05 | **Fixed at RC3 `d9af0396`** (found by the 1.2.0 RC2 gate; fix [PR #344](https://github.com/zygote55/sketchyup/pull/344)). In the owner-approved corpus v4 run at RC2 (`d80fb729`, profile `chatgpt-astra`), resize, advanced and site scored 0/3 each: `OpenAiConversation::decode` (`src/integrations/openai_provider.cpp`) rejected multi-call replies that combine `transaction.inspect` with `transaction.diff`, so those turns failed. The private run evidence is kept outside the repository. | Major | A wrong result in the supported provider workflow (three corpus tasks cannot complete); no document is changed by a rejected reply, and no data is lost | [R086](../PR_ROADMAP.md#r086): fixed by PR #344 (RC3): `transaction.diff` and `transaction.describe` may share a read-only batch; `openai_provider_tests` covers it |
| D-038 | A paint inside a definition marks its driver Changed, with "changed shape" wording | [R094.h Findings](R094h-configurable-components.md#findings) | F02 | **Open.** A driver's `bound` digest covers its target member's whole record, colour included (ADR 0165 §4). Painting the top makes Stretch Top Changed, and the amber strip says the driver "now starts from a changed shape" although only the colour changed. `component_acceptance_input_tests` expects it (three Changed drivers, "Rebind 3 drivers"). | Minor | Specified behaviour with misleading wording: Rebind or Undo clears it, and no geometry or document is wrong | Owner decision (whether a colour change counts, or the sentence changes); [R094](../PR_ROADMAP.md#r094) |
| D-039 | `checkUniqueGeometry` costs O(variant placements × members) per edit | [R094.g](R094g-variant-instancing.md#the-22-edit-feedback-gap-in-the-first-report) | F02, N01 | **Open.** Every `Document::apply` walks each variant placement's member records while any placement shows a variant: 1.1–1.5 ms per placement move with 10,000 variant placements against 0.4–0.6 ms with default placements (software, R094.g). Per-variant placement counts in the variant index would make it O(distinct variants). | Minor | A performance cost about 1 ms at 10,000 placements, well inside the edit-feedback budget; disclosed in the 1.2.0 release notes | [R093](../PR_ROADMAP.md#r093) / [R094](../PR_ROADMAP.md#r094): keep per-variant counts |
| D-040 | Weston 15.0.1 segfaulted in `weston_view_animation_destroy` during a CI Wayland 2× line | [main run 38920762782](https://github.com/zygote55/sketchyup/actions/runs/38920762782) (job 117047789178, `9d3a786c`) | N03 (test harness) | **Unverified.** Once on `main` (2026-10-16): after `section_viewport_tests` passed, the next line (`style_input_tests`, Wayland 2×) ended when the isolated Weston received SIGSEGV; the D-034 trace shows `wl_list_remove` ← `weston_view_animation_destroy` ← `wl_event_loop_dispatch_idle` in Weston's main thread. No SketchyUp check failed. Later `main` runs passed the line. | Minor | An external compositor crash in the isolated test session, like D-020 and D-031; no product frame is involved | [R085](../PR_ROADMAP.md#r085) (test-only): track Weston; keep the core dump on recurrence |
| D-010 | System Wayland 1.26 client leaks a destroyed-proxy reference | [R062.b proxy](R062b-wayland-proxy.md), [R058.c](R058c-native-diagnostics.md) | N01 | **Open (external).** The leak is reproduced standalone without Qt or SketchyUp. Only sanitized CI uses the patched private client (`scripts/ci-wayland-client.sh`); users get the system library. | Limitation | A 96-byte leak per affected dialog event, in a third-party library. Disclosed in R062.b as "a documented external limitation until a fixed distribution library is available". | [R085](../PR_ROADMAP.md#r085): track the distribution fix |
| D-011 | Qt Wayland client-decoration framebuffer leak | [R050](R050-native-render-ui.md) | N01 | **Open (external).** Sanitized Wayland steps still set `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` (`.github/workflows/native.yml`). | Limitation | Upstream Qt allocation, with no application frame in the stack. Disclosed in R050 as a test-environment workaround. | [R085](../PR_ROADMAP.md#r085): track upstream |
| D-012 | Compositor crash with RGB565/odd-stride surfaces at fractional scale | [R085.a](R085a-current-platform-validation.md), [R085.b](R085b-native-surface-color.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | N01, N04 | **Mitigated.** The application now requests eight-bit surfaces and all 20 fractional cases pass. The external Weston/Mesa defect is not shown to be fixed. | Limitation | External defect, avoided by the surface-color policy ([ADR 0157](../decisions/0157-native-surface-color.md)). Disclosed in the support matrix. | [R085](../PR_ROADMAP.md#r085) |
| D-013 | Qt text shaping crashes with no installed fonts | [CI-PR172](CI-PR172-schema-cache.md) | N02, P05 | **Mitigated.** A Qt-only control program reproduces the crash. `packaging/arch/PKGBUILD` hard-depends on `ttf-dejavu`. | Limitation | Occurs only when a declared package dependency is removed. | [R087](../PR_ROADMAP.md#r087) |
| D-014 | Recovery and publication faults are injected, not physical power-loss crashes | [R083.b](R083b-integrated-publication-faults.md), [R083.d](R083d-local-failure-matrix.md) | D04, D02 | **Open by design.** R083.b "does not claim physical filesystem crash recovery". No data-loss defect is recorded in the fault matrix. | Limitation | The evidence boundary is stated; the final data-loss audit remains R083 work. | [R083](../PR_ROADMAP.md#r083) |
| D-015 | Interchange is lossy conversion; no native SKP/DWG | [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), ADRs [0111](../decisions/0111-native-gltf-import-workflow.md), [0115](../decisions/0115-native-obj-interchange-workflow.md), [0119](../decisions/0119-native-stl-interchange-workflow.md), [0123](../decisions/0123-native-dxf-interchange-workflow.md) | X02–X04 | Documented boundary. | Limitation | Losses are reported per conversion. SKP/DWG are gated investigations, not Core. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-016 | Blender handoff is one-way; animation exports PNG frames without movie encoding or resume | [user guide](../USER_GUIDE.md), [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), [M7](M7-accepted.md) | P03, P08, P09 | Documented boundary. | Limitation | P09 requires the one-way behavior to be documented, and it is. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-017 | Finite planar/sweep geometry limits; retained internal faces may be non-manifold | [M3](M3.md), [M4](M4.md), [M2](M2.md) | G01, E01, E05 | Documented boundary. | Limitation | Explicit enforced limits with bounded rejection. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-018 | Remote assistant quality is probabilistic, and acceptance covers one configuration | [M5](M5.md), [R086.b](R086b-remote-evaluation.md), [R086.as](R086as-openai-inspection-batches.md) | A04, A05 | Documented boundary. Earlier failed campaigns (R086.ah, R086.ak) stay recorded; the current host passes 24/24. At the RC, [R086.ca](R086ca-rc-remote-evaluation.md) passes 24/24 with no blockers, a bounded result for the frozen tasks only. | Limitation | Disclosed in M5 and the support matrix. | [R086](../PR_ROADMAP.md#r086) |
| D-001 | Local provider configurations do not pass the frozen corpus (experimental by decision) | [R086.c](R086c-local-evaluation.md), [R086.ak](R086ak-remote-local-guidance.md), [R086.as](R086as-openai-inspection-batches.md), [M5](M5.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | A05 (also A08) | **Resolved by scope decision (2026-10-11).** The owner decided that a passing local configuration is not a 1.0 requirement ([ADR 0160](../decisions/0160-provider-scope-for-1-0.md)). The failures remain true and recorded: The frozen Ollama `qwen3:4b-instruct` profile scores 0/3 on all seven live tasks (20 of 21 reach the 300 s deadline). The later Qwen3-Coder campaign stops after 10 reports with room and resize failing. R086.as states that A05 still needs an accepted local configuration. No later record supersedes these. | Limitation | A05 now requires a passing remote provider only, and the remote OpenAI ChatGPT-plan profile passes the frozen corpus (24/24, [R086.b](R086b-remote-evaluation.md)). The local profile is experimental and labeled so. No safety blocker or false completion was observed. | [R086](../PR_ROADMAP.md#r086): the remote re-run at the RC passed ([R086.ca](R086ca-rc-remote-evaluation.md)); a local profile can be promoted only by a later decision |
| D-004 | Intel integrated reference GPU misses the 16.7 ms complete-frame target on 100k-triangle scenes under one CPU; not remeasured | [R082.i](R082i-complete-frame.md), [R082.j](R082j-viewport-reuse.md), [R082.an](R082an-public-memory.md), [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) | N01, D06 | **Waived, untested on Intel at the current revision (2026-10-12).** The latest Intel public campaign (R082.an, one CPU, Intel Arc/Wayland) missed the target in all five families: p95 17.3–19.6 ms. R082.j had passed all 20 Intel runs with an eight-CPU quota. The Intel rerun scheduled in [M9 hardware session](../M9_HARDWARE_SESSION.md) section 1 was not run. AMD passes every 100k target with a wide margin (viewport p95 2.1–2.6 ms, [R082.ii](R082ii-amd-hardware-session.md)). | Limitation | A known, recorded miss on the integrated GPU under a one-CPU quota, documented as untested for the current revision by the owner's waiver. Workflows function; only frame time is affected. The support matrix and release notes must not claim Intel performance. Reopen as Major if daily use on the Intel laptop shows slow frames. | [R082](../PR_ROADMAP.md#r082): none for 1.0; remeasure if Intel is promoted to a supported claim |
| D-005 | 282 ms opening heartbeat gap on Intel/Wayland not remeasured after the progress fix | [R082.m](R082m-native-file-timing.md), [R082.bb](R082bb-public-tooltip-progress.md), [R082.ak](R082ak-public-progress.md), [R082.ii](R082ii-amd-hardware-session.md), [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) | D02, N01 | **Unverified; Intel/Wayland rerun waived (2026-10-12).** R082.m retains 186–282 ms maximum open gaps on Intel Arc/Wayland. R082.bb's fix brought all 18 AMD/X11 gaps below 50 ms (worst 33.0 ms), and the 2026-10-11 AMD/X11 file campaign agrees: open gap 19.7–29.2 ms, save gaps up to 32.4 ms, every operation under 400 ms. No Intel/Wayland rerun exists. | Limitation | UI latency only, untested on Intel/Wayland at the current revision. Every open and save is within the five-second target. | [R082](../PR_ROADMAP.md#r082): none for 1.0 |
| D-020 | Weston 15.0.1 segfaults when a progress popup is created for a minimized window; Hyprland check waived | [R083.f](R083f-file-worker-hang.md), [ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md) | N01 | **Open (external); Hyprland check waived (2026-10-12).** Seen only on Weston 15.0.1, which is the isolated test compositor, not the primary Hyprland target. Other compositors were not tested. The check on Hyprland (a long open or save started, then the window minimized, before the progress popup appears; [M9 hardware session](../M9_HARDWARE_SESSION.md) section 8) was not run. | Limitation | An external compositor crash, not shown on the primary target, and untested there by the owner's waiver. The owner will see it in use if it occurs. | [R085](../PR_ROADMAP.md#r085) |
| D-029 | PKGBUILD dependencies carry no versions | [R087.u](R087u-clean-machine-packages.md), [R088.d N02](R088d-core-parity-gaps.md#row-status), [release notes](../RELEASE_NOTES.md#install-and-upgrade) | N02 | **Open by design for 1.0.** pacman installs a package built against newer Qt or glibc on an older system, where both executables fail to start (`Qt_6.11` and `GLIBC_2.43` symbol versions not found in R087.u). The release notes tell users to install a package built for their repository snapshot. | Limitation | Documented in the release notes. Arch does not support partial upgrades, so it matters only when a package is installed outside its snapshot. The RC package installs and runs on the pinned and fresh images, and a package built from the RC archive runs on the Qt 6.8.0 reference. | [R087](../PR_ROADMAP.md#r087): versioned lower bounds for Qt and glibc after 1.0 |

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
- **Minor (10).** Each is test infrastructure (D-007–D-009, D-021, D-022, D-027, D-028), a
  path the product does not reach (D-019), or a cosmetic or render-statistics residual
  with no document effect (D-006, D-023).
- **Limitation (14).** Each is a documented boundary: external library or compositor
  defects with mitigations (D-010–D-013, D-020), evidence boundaries and format scope
  (D-014–D-018), decisions (D-001, ADR 0160), waived hardware checks (D-004, D-005,
  ADR 0161) and packaging (D-029). The release notes and support matrix disclose them.

## Open acceptance work that is not a known defect

These are unaccepted areas, not recorded failures, so they are not classified above.
Before the RC, the list read: release-candidate recovery revalidation and the final data-loss audit
(R083); the all-dialog keyboard workflows (N05); current/reference Arch clean-machine
package checks (R087); the remote provider's release-candidate re-run (R086); and the Core
parity report (R088).

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

## Counts

| Class | Count | IDs |
| --- | ---: | --- |
| Release-blocking | 0 | None |
| Major | 0 | None |
| Minor | 15 | D-006–D-009, D-019, D-021–D-023, D-027, D-028, D-031, D-034, D-038–D-040 |
| Limitation | 14 | D-001, D-004, D-005, D-010–D-018, D-020, D-029 |
| **Total open** | **29** | |
| Fixed since the 2026-10-11 refresh | 9 | D-003, D-024 (added and fixed 2026-10-12), D-025 (fixed by #301), D-026 (added by R084.u and fixed by R084.v, 2026-10-13), D-035 (Major, added and fixed 2026-10-15), D-036 (Minor, added at the 1.1.0 RC and fixed by 1.1.1), D-037 (Major, added at the 1.2.0 RC and fixed 2026-10-17), D-041 (Major, added at the 1.2.0 RC2 and fixed at RC3), D-032 (Minor, added by R091.h and fixed 2026-10-17) |

At the RC (2026-10-13): 24 open (0 release-blocking, 0 Major, 10 Minor, 14 Limitation).
R091.h (2026-10-14) added D-031 and D-032 (Minor). 2026-10-15 added D-034 (Minor). D-035
(Major, 1.1.0 RC) was added and fixed on 2026-10-15, so it is not open. D-036 (Minor, Qt 6.8
only) was added on 2026-10-15 by the Qt 6.8 reference at the 1.1.0 RC, shipped in 1.1.0 and is
fixed by 1.1.1, so it is not open.
At the 1.2.0 RC `71a6b4f3` (2026-10-17): 31 open (0 release-blocking, 1 Major, 16 Minor, 14
Limitation). The gate added D-037 (Major: 1.2.0 does not build on Qt 6.8.0, the declared
minimum); the F02 follow-ups D-038 and D-039 and the CI Weston crash D-040 are Minor. D-037 needs
an owner decision and a new RC ([F02-rc-1.2.0](F02-rc-1.2.0.md#triage-at-the-rc)).
At 1.2.0 RC2 `d80fb729` (2026-10-17): 31 open (0 release-blocking, 1 Major, 16 Minor, 14
Limitation). D-037 is fixed and verified on the Qt 6.8 reference; D-041 (Major) was added by the
RC2 corpus run, and its fix is pending as RC3.
At 1.2.0 RC3 `d9af0396` (2026-10-17): 30 open (0 release-blocking, 0 Major, 16 Minor, 14
Limitation). D-041 is fixed.
After the D-032 fix (2026-10-17): 29 open (0 release-blocking, 0 Major, 15 Minor, 14
Limitation).

Before the RC update (2026-10-13): 21 open (0 release-blocking, 0 Major, 8 Minor, 13
Limitation). The RC update added D-027 and D-028 (Minor) and D-029 (Limitation).

Before this refresh (2026-10-11): 22 (0 release-blocking, 1 Major, 11 Minor, 10 Limitation).
D-003 is fixed. D-004 moved from Major to Limitation, and D-005 and D-020 from Minor to
Limitation, by the owner's evidence waiver. D-024 (Major) and D-025 (Minor) were added;
R082.jj then fixed D-024. **2026-10-13:** #301 fixed D-025, and R084.u added D-026 (Minor),
which R084.v then fixed.

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
