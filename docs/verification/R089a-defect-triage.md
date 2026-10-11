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
This is a working triage list of known defects against current `main`. It is not
the release-candidate triage, and it is not a release sign-off: it does not accept
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
| D-003 | 10,000-instance / 1,000,000-triangle fixture: accepted by the public path in software; hardware acceptance of the R082.w v2 budget pending | [R082.gg](R082gg-placement-limits.md), [R082.w v2](R082w-memory-budget-v2.md), [R082.hh](R082hh-far-inference.md), [R082.cc](R082cc-instanced-storage.md), [R082.dd](R082dd-instanced-viewport.md), [R082.ee](R082ee-instanced-inference.md), [R082.ff](R082ff-incremental-placement.md); earlier [R082.a](R082a-real-model-baseline.md), [R082.an](R082an-public-memory.md), [R085.f](R085f-discrete-gpu.md), [ADR 0141](../decisions/0141-real-model-performance-fixtures.md), [support matrix](../SUPPORT_MATRIX.md#release-work-still-required) | D06, O02, N01 | **Fixed in software; hardware acceptance pending.** The rejection recorded by R082.an (`Component placement exceeds document editing limits` at placement 1,924) is gone: R082.cc/dd/ee/ff (#284, #289, #287, #285) made placements instanced and R082.gg (#298) split the limits into unique geometry, 20,000 placements and materialized totals. `document_limits` places the full 10,000-prism fixture on the public path. Software peak RSS is 613 MB (repeated 10k, five cycles), 624 MB (deep) and 884 MB (textures), against the 1.25 GiB class declared in R082.w v2. **Still open:** (1) Intel and AMD reruns ([M9 hardware session](../M9_HARDWARE_SESSION.md) sections 1d and 2); no v2 ceiling is accepted until then, and v1 hardware RSS ran about 150 MB above software at 1,000 placements; (2) far at 10,000 placements, which R082.hh made measurable but which has no recorded run; (3) the Core still expands about 18 KB per placement, so load time (3.25 s) and inference updates (100 ms p95) grow with placements. | Minor | Major no longer applies. Models of this size can be built, saved and reloaded, and limit rejections are bounded and name the entity (D06). The open part is hardware evidence for a declared budget, with no known failing run. Reclassify to Major if a hardware rerun exceeds a v2 ceiling or the fixture is rejected on a supported configuration. | [R082](../PR_ROADMAP.md#r082): Intel and AMD reruns against R082.w v2 (evidence) |
| D-004 | Intel integrated reference GPU misses the 16.7 ms complete-frame target on 100k-triangle scenes | [R082.i](R082i-complete-frame.md), [R082.j](R082j-viewport-reuse.md), [R082.an](R082an-public-memory.md) | N01, D06 | **Open.** The latest public campaign (R082.an, one CPU, Intel Arc/Wayland) misses the target in all five families: p95 17.3–19.6 ms. R082.j had passed all 20 Intel runs with an eight-CPU quota. AMD passes every target. R082.gg adds only software measurements (viewport p95 211–410 ms in llvmpipe at 1,000 placements), which say nothing about this miss. The rerun is scheduled in [M9 hardware session](../M9_HARDWARE_SESSION.md) section 1; no result is recorded. | Major | This is a frozen performance budget missed on a reference hardware configuration. Workflows function and only frame time is affected. It is Minor if the release declares the eight-CPU configuration as the reference. | [R082](../PR_ROADMAP.md#r082): optimization or a reference-configuration decision, plus a controlled rerun (code and evidence) |
| D-005 | 282 ms opening heartbeat gap on Intel/Wayland not remeasured after the progress fix | [R082.m](R082m-native-file-timing.md), [R082.bb](R082bb-public-tooltip-progress.md), [R082.ak](R082ak-public-progress.md) | D02, N01 | **Unverified.** R082.m retains 186–282 ms maximum open gaps on Intel Arc/Wayland. R082.bb's fix brings all 18 AMD/X11 gaps below 50 ms (worst 33.0 ms). No Intel/Wayland rerun is recorded; it is scheduled in [M9 hardware session](../M9_HARDWARE_SESSION.md) section 1. | Minor | UI latency only. Every open and save is within the five-second target, and the fix is proven on one platform. | [R082](../PR_ROADMAP.md#r082): Intel/Wayland rerun (evidence) |
| D-006 | Intermittent X11 viewport assertion "Camera movement only reorders transparency" | [R085.i](R085i-assisted-output-transitions.md#retained-attempts-and-validation), [R085k X11 log](R085i-r085k-xcb.txt) | N01, P02 | **Unverified.** R085.i retains it as "intermittent … remains unexplained". The assertion (`tests/viewport_tests.cpp`) is unchanged, and no later record explains it. | Minor | The failed check is a render-statistics assertion: either geometry was re-uploaded or transparency was not re-sorted after a camera move. The worst case is an extra upload or a transient blend-order glitch, with no document effect. | [R085](../PR_ROADMAP.md#r085) (viewport cache, with [R082](../PR_ROADMAP.md#r082)): investigation; possibly a code fix |
| D-007 | Wayland native-test focus and activation races | [R015](R015-topology.md), [R035.a](R035a-entity-measurements.md), [R084.p](R084p-dialog-keyboard.md), [R087.t](R087t-native-ci-reproduction.md), [PR #273](https://github.com/zygote55/sketchyup/pull/273), [main run 38011483053](https://github.com/zygote55/sketchyup/actions/runs/38011483053) | N03 | **Unverified.** Recurring intermittent fixture failures where a window reports active before its native surface has focus. Examples: the R015 Extrude shortcut, the R087.t "Tab reaches shortcut control without assigning focus", and the entity-info Wayland 2× focus timeout on `main` before #273. Each received a fixture wait or re-request; none has a confirmed root cause. Three `main` runs have passed since #273. | Minor | These are test-infrastructure races in isolated Weston. No product focus defect has been shown. | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only) |
| D-008 | Scene-transition native test timed out in CI under delayed events | [R085.d](R085d-scene-animation-fixture.md) | P03 | **Unverified.** CI waited 1.5 s for the saved pose and failed. The wait is now 5 s, and the cause is recorded as "not a confirmed root cause". Product timing is unchanged. | Minor | Eventual-correctness timing in a test fixture. Final-pose equality is still enforced. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-009 | X11 hardware fixture cannot place its window on a requested output without a compositor helper | [R085.h](R085h-physical-outputs.md), [R085.i](R085i-assisted-output-transitions.md) | N04 | **Open.** Both Qt-only X11 placement attempts fail the requested-output assertion. X11 transitions qualify only through the Hyprland-specific helper. | Minor | Test infrastructure: the placement is of the fixture's own window, as Qt documents for `setScreen`. Rendering on both outputs passes once placed. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-019 | Qt Wayland protocol error when re-showing a window hidden during a file operation | [R083.f](R083f-file-worker-hang.md) | N01 | **Open.** Observed in 6 of 8 probe runs at 1×: the probe hid the window, ran a file operation, then showed the window again. The application never hides its main window during a file operation. | Minor | The path is not reachable from current product code. Recorded so a future hide/restore feature retests it. | [R085](../PR_ROADMAP.md#r085) |
| D-020 | Weston 15.0.1 segfaults when a progress popup is created for a minimized window | [R083.f](R083f-file-worker-hang.md) | N01 | **Open (external).** Seen only on Weston 15.0.1, which is the isolated test compositor, not the primary Hyprland target. Other compositors were not tested. | Minor | An external compositor crash. It needs a Hyprland check: a long open or save started, then the window minimized, before the progress popup appears. | [R085](../PR_ROADMAP.md#r085) |
| D-021 | Wayland test-window exposure timeouts under host load | [R083.g](R083g-unexpected-modal-guard.md), [R084.r](R084r-display-precision.md), [R084.s](R084s-keyboard-panel-tabs.md), [R085.q](R085q-keypad-decimal.md) | N03, N01 (test harness) | **Unverified.** In local bounded-container Wayland 2× runs, fixtures fail in `qWaitForWindowExposed` before any test step: 2 of 4 `units_input_tests` runs (load average about 28), 3 of 6 `file_operation_input_tests` runs (load above 30; 6/6 passed at about 15), five Wayland 2× cases in the R084.s matrix (style, solar, reference image, annotation, audit) and one `numeric_input_tests` run. Identical reruns pass without a changed wait. These records attribute none to CI. The cause is not established beyond host load. | Minor | Test infrastructure. The failure precedes any product code in the test and is not a focus defect, so it is distinct from D-007 (focus lost after exposure). | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only): load gating or a bounded retry |
| D-022 | Parser fuzz targets abort on their first seed under ASan with Qt 6.11.2 JSON parsing (F1) | [R083.e](R083e-parser-fuzzing.md) | D02, D06 (test tooling) | **Unverified.** Six of ten libFuzzer targets (native JSON, container, GLB, template, component, extension) abort on an unmodified valid seed with an alloc-dealloc mismatch whose stacks lie inside `libQt6Core.so.6` under `QJsonDocument::fromJson`. A three-line program that calls only `fromJson` reproduces it. `parser_fuzz_seeds` passes every seed through the instrumented parsers, and the six targets show no crash, leak or hang with that one check disabled. The record states that the root cause is not established and the finding stays open. | Minor | The evidence points to a Qt/libFuzzer/ASan interaction, not application code, but the six targets do not meet the documented fuzz options and the campaign is not in CI. | [R083](../PR_ROADMAP.md#r083): resolve F1 or document the option; continuous fuzzing |
| D-023 | Focus-ring contrast residuals next to the 3D canvas and native scroll bars | [R084.q](R084q-visible-focus.md), [R084.s](R084s-keyboard-panel-tabs.md) | N05 | **Open.** R084.q records that the dark-theme ring has 1.64:1 contrast against the light default canvas where the tool rail, breadcrumb and assistant container touch it; the canvas's own ring is 2.997:1 against dark chrome; native scroll bars are not themed; 14 controls per run are wider than their scroll area at 200% text. R084.s leaves one or two tab-bar scroll buttons in the Tab order between the tab bar and its page. | Minor | Cosmetic and keyboard-friction issues. Every ring keeps at least 3.93:1 against the colour directly inside it, and no control is unreachable. A mid-tone ring cannot satisfy all cases, so the next step is a design decision. | [R084](../PR_ROADMAP.md#r084) (UX decision) |
| D-010 | System Wayland 1.26 client leaks a destroyed-proxy reference | [R062.b proxy](R062b-wayland-proxy.md), [R058.c](R058c-native-diagnostics.md) | N01 | **Open (external).** The leak is reproduced standalone without Qt or SketchyUp. Only sanitized CI uses the patched private client (`scripts/ci-wayland-client.sh`); users get the system library. | Limitation | A 96-byte leak per affected dialog event, in a third-party library. Disclosed in R062.b as "a documented external limitation until a fixed distribution library is available". | [R085](../PR_ROADMAP.md#r085): track the distribution fix |
| D-011 | Qt Wayland client-decoration framebuffer leak | [R050](R050-native-render-ui.md) | N01 | **Open (external).** Sanitized Wayland steps still set `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` (`.github/workflows/native.yml`). | Limitation | Upstream Qt allocation, with no application frame in the stack. Disclosed in R050 as a test-environment workaround. | [R085](../PR_ROADMAP.md#r085): track upstream |
| D-012 | Compositor crash with RGB565/odd-stride surfaces at fractional scale | [R085.a](R085a-current-platform-validation.md), [R085.b](R085b-native-surface-color.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | N01, N04 | **Mitigated.** The application now requests eight-bit surfaces and all 20 fractional cases pass. The external Weston/Mesa defect is not shown to be fixed. | Limitation | External defect, avoided by the surface-color policy ([ADR 0157](../decisions/0157-native-surface-color.md)). Disclosed in the support matrix. | [R085](../PR_ROADMAP.md#r085) |
| D-013 | Qt text shaping crashes with no installed fonts | [CI-PR172](CI-PR172-schema-cache.md) | N02, P05 | **Mitigated.** A Qt-only control program reproduces the crash. `packaging/arch/PKGBUILD` hard-depends on `ttf-dejavu`. | Limitation | Occurs only when a declared package dependency is removed. | [R087](../PR_ROADMAP.md#r087) |
| D-014 | Recovery and publication faults are injected, not physical power-loss crashes | [R083.b](R083b-integrated-publication-faults.md), [R083.d](R083d-local-failure-matrix.md) | D04, D02 | **Open by design.** R083.b "does not claim physical filesystem crash recovery". No data-loss defect is recorded in the fault matrix. | Limitation | The evidence boundary is stated; the final data-loss audit remains R083 work. | [R083](../PR_ROADMAP.md#r083) |
| D-015 | Interchange is lossy conversion; no native SKP/DWG | [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), ADRs [0111](../decisions/0111-native-gltf-import-workflow.md), [0115](../decisions/0115-native-obj-interchange-workflow.md), [0119](../decisions/0119-native-stl-interchange-workflow.md), [0123](../decisions/0123-native-dxf-interchange-workflow.md) | X02–X04 | Documented boundary. | Limitation | Losses are reported per conversion. SKP/DWG are gated investigations, not Core. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-016 | Blender handoff is one-way; animation exports PNG frames without movie encoding or resume | [user guide](../USER_GUIDE.md), [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), [M7](M7-accepted.md) | P03, P08, P09 | Documented boundary. | Limitation | P09 requires the one-way behavior to be documented, and it is. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-017 | Finite planar/sweep geometry limits; retained internal faces may be non-manifold | [M3](M3.md), [M4](M4.md), [M2](M2.md) | G01, E01, E05 | Documented boundary. | Limitation | Explicit enforced limits with bounded rejection. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-018 | Remote assistant quality is probabilistic, and acceptance covers one configuration | [M5](M5.md), [R086.b](R086b-remote-evaluation.md), [R086.as](R086as-openai-inspection-batches.md) | A04, A05 | Documented boundary. Earlier failed campaigns (R086.ah, R086.ak) stay recorded; the current host passes 24/24. | Limitation | Disclosed in M5 and the support matrix. Fresh current-head CI and release review remain. | [R086](../PR_ROADMAP.md#r086) |
| D-001 | Local provider configurations do not pass the frozen corpus (experimental by decision) | [R086.c](R086c-local-evaluation.md), [R086.ak](R086ak-remote-local-guidance.md), [R086.as](R086as-openai-inspection-batches.md), [M5](M5.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | A05 (also A08) | **Resolved by scope decision (2026-10-11).** The owner decided that a passing local configuration is not a 1.0 requirement ([ADR 0160](../decisions/0160-provider-scope-for-1-0.md)). The failures remain true and recorded: The frozen Ollama `qwen3:4b-instruct` profile scores 0/3 on all seven live tasks (20 of 21 reach the 300 s deadline). The later Qwen3-Coder campaign stops after 10 reports with room and resize failing. R086.as states that A05 still needs an accepted local configuration. No later record supersedes these. | Limitation | A05 now requires a passing remote provider only, and the remote OpenAI ChatGPT-plan profile passes the frozen corpus (24/24, [R086.b](R086b-remote-evaluation.md)). The local profile is experimental and labeled so. No safety blocker or false completion was observed. | [R086](../PR_ROADMAP.md#r086): re-run the remote profile at the release candidate; a local profile can be promoted only by a later decision

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

## Open acceptance work that is not a known defect

These are untested or unaccepted areas, not recorded failures, so they are not
classified above: hotplug, suspend/resume and input methods (N04); controlled
reference-hardware campaigns and the R082.w v2 memory-budget reruns on Intel and AMD (R082); physical pointer capture and a real keyboard under Hyprland (N03); release-candidate recovery revalidation and the
final data-loss audit (R083); all-dialog and screen-reader workflows (N05);
current/reference Arch clean-machine package checks (R087); and the Core parity
report (R088).

## Counts

| Class | Count | IDs |
| --- | ---: | --- |
| Release-blocking | 0 | None |
| Major | 1 | D-004 |
| Minor | 11 | D-003, D-005–D-009, D-019, D-020, D-021–D-023 |
| Limitation | 10 | D-001, D-010–D-018 |
| **Total** | **22** | |

Before the refresh: 20 (0 release-blocking, 2 Major, 8 Minor, 10 Limitation). D-003
moved from Major to Minor (fixed in software, hardware acceptance pending), D-002 was
closed, and D-021–D-023 were added.

No release-blocking defect remains: D-001 was reclassified to a Limitation by the 2026-10-11 owner scope decision ([ADR 0160](../decisions/0160-provider-scope-for-1-0.md)); the classification is otherwise unchanged and this is still not a release sign-off. No known open critical security defect was found. No data-loss defect is
confirmed open. D-002 was root-caused as a test-harness hang on a full CI disk ([R083.f](R083f-file-worker-hang.md)) and its unexpected-modal guard is merged ([R083.g](R083g-unexpected-modal-guard.md)); no open hang or data-path candidate remains. D-003 is not closed: it stays listed until the Intel and AMD reruns accept the R082.w v2 budget, and D-004 and D-005 are unchanged because they are hardware questions that no merged layer measured.
