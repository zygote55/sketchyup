# R089.a — Open-defect triage list

2026-10-10. Source revision `8534bb67bd1f003c140b6f5f7df6d6385fbc5dc7` (`main`).
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
| D-001 | No documented local provider configuration passes the frozen corpus | [R086.c](R086c-local-evaluation.md), [R086.ak](R086ak-remote-local-guidance.md), [R086.as](R086as-openai-inspection-batches.md), [M5](M5.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | A05 (also A08) | **Open.** The frozen Ollama `qwen3:4b-instruct` profile scores 0/3 on all seven live tasks (20 of 21 reach the 300 s deadline). The later Qwen3-Coder campaign stops after 10 reports with room and resize failing. R086.as states that A05 still needs an accepted local configuration. No later record supersedes these. | Release-blocking | A05 is Core and requires "one documented local configuration" to pass the published suite; none does. No safety blocker or false completion was observed. | [R086](../PR_ROADMAP.md#r086): a new frozen local configuration and evaluation (evidence, and possibly adapter/guidance code), or an explicit scope decision on A05 |
| D-003 | 10,000-instance / 1,000,000-triangle benchmark fixture is rejected by public editing limits | [R082.a](R082a-real-model-baseline.md), [R082.b](R082b-component-construction.md), [R082.e](R082e-performance-scenarios.md), [R082.an](R082an-public-memory.md), [R085.f](R085f-discrete-gpu.md), [ADR 0141](../decisions/0141-real-model-performance-fixtures.md), [support matrix](../SUPPORT_MATRIX.md#release-work-still-required), [evidence index](R088b-core-evidence-index.md) | D06, O02, N01 | **Open.** The R082.an public campaigns on both GPUs stop at the first 10,000-placement attempt with `Component placement exceeds document editing limits`. ADR 0141 requires the fixture to fit public limits. Private 1M runs used raised private limits and do not count. | Major | The rejection is bounded, with no data loss, so D06's bounded-error criterion holds. Models of this size cannot be built; the workaround is smaller models. This blocks R082's Verify clause. | [R082](../PR_ROADMAP.md#r082): document limits / real instancing (code fix) |
| D-004 | Intel integrated reference GPU misses the 16.7 ms complete-frame target on 100k-triangle scenes | [R082.i](R082i-complete-frame.md), [R082.j](R082j-viewport-reuse.md), [R082.an](R082an-public-memory.md) | N01, D06 | **Open.** The latest public campaign (R082.an, one CPU, Intel Arc/Wayland) misses the target in all five families: p95 17.3–19.6 ms. R082.j had passed all 20 Intel runs with an eight-CPU quota. AMD passes every target. | Major | This is a frozen performance budget missed on a reference hardware configuration. Workflows function and only frame time is affected. It is Minor if the release declares the eight-CPU configuration as the reference. | [R082](../PR_ROADMAP.md#r082): optimization or a reference-configuration decision, plus a controlled rerun (code and evidence) |
| D-002 | Intermittent hang in the sanitized Wayland `file_operation_input_tests` fixture | [R083.f](R083f-file-worker-hang.md), [CI-PR240 reproduction](CI-PR240-native-reproduction.md) | N01, D02 (test harness) | **Root-caused.** A save failed with `ENOSPC` on the nearly full single-job CI disk, and the test left the modal "Save failed" dialog open until the 60 s timeout. Reproduced deterministically with a size-limited `/tmp`. A probe shows that users get a working dialog, with the edit kept and the file unchanged. All 75 runs since the CI disk fixes passed. | Minor | Test harness only. The test should fail fast on an unexpected dialog instead of timing out. | [R083](../PR_ROADMAP.md#r083) (test-only guard) |
| D-005 | 282 ms opening heartbeat gap on Intel/Wayland not remeasured after the progress fix | [R082.m](R082m-native-file-timing.md), [R082.bb](R082bb-public-tooltip-progress.md), [R082.ak](R082ak-public-progress.md) | D02, N01 | **Unverified.** R082.m retains 186–282 ms maximum open gaps on Intel Arc/Wayland. R082.bb's fix brings all 18 AMD/X11 gaps below 50 ms (worst 33.0 ms). No Intel/Wayland rerun is recorded. | Minor | UI latency only. Every open and save is within the five-second target, and the fix is proven on one platform. | [R082](../PR_ROADMAP.md#r082): Intel/Wayland rerun (evidence) |
| D-006 | Intermittent X11 viewport assertion "Camera movement only reorders transparency" | [R085.i](R085i-assisted-output-transitions.md#retained-attempts-and-validation), [R085k X11 log](R085i-r085k-xcb.txt) | N01, P02 | **Unverified.** R085.i retains it as "intermittent … remains unexplained". The assertion (`tests/viewport_tests.cpp`) is unchanged, and no later record explains it. | Minor | The failed check is a render-statistics assertion: either geometry was re-uploaded or transparency was not re-sorted after a camera move. The worst case is an extra upload or a transient blend-order glitch, with no document effect. | [R085](../PR_ROADMAP.md#r085) (viewport cache, with [R082](../PR_ROADMAP.md#r082)): investigation; possibly a code fix |
| D-007 | Wayland native-test focus and activation races | [R015](R015-topology.md), [R035.a](R035a-entity-measurements.md), [R084.p](R084p-dialog-keyboard.md), [R087.t](R087t-native-ci-reproduction.md), [PR #273](https://github.com/zygote55/sketchyup/pull/273), [main run 38011483053](https://github.com/zygote55/sketchyup/actions/runs/38011483053) | N03 | **Unverified.** Recurring intermittent fixture failures where a window reports active before its native surface has focus. Examples: the R015 Extrude shortcut, the R087.t "Tab reaches shortcut control without assigning focus", and the entity-info Wayland 2× focus timeout on `main` before #273. Each received a fixture wait or re-request; none has a confirmed root cause. Three `main` runs have passed since #273. | Minor | These are test-infrastructure races in isolated Weston. No product focus defect has been shown. | [R085](../PR_ROADMAP.md#r085) / [R084](../PR_ROADMAP.md#r084) (test-only) |
| D-008 | Scene-transition native test timed out in CI under delayed events | [R085.d](R085d-scene-animation-fixture.md) | P03 | **Unverified.** CI waited 1.5 s for the saved pose and failed. The wait is now 5 s, and the cause is recorded as "not a confirmed root cause". Product timing is unchanged. | Minor | Eventual-correctness timing in a test fixture. Final-pose equality is still enforced. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-009 | X11 hardware fixture cannot place its window on a requested output without a compositor helper | [R085.h](R085h-physical-outputs.md), [R085.i](R085i-assisted-output-transitions.md) | N04 | **Open.** Both Qt-only X11 placement attempts fail the requested-output assertion. X11 transitions qualify only through the Hyprland-specific helper. | Minor | Test infrastructure: the placement is of the fixture's own window, as Qt documents for `setScreen`. Rendering on both outputs passes once placed. | [R085](../PR_ROADMAP.md#r085) (test-only) |
| D-019 | Qt Wayland protocol error when re-showing a window hidden during a file operation | [R083.f](R083f-file-worker-hang.md) | N01 | **Open.** Observed in 6 of 8 probe runs at 1×: the probe hid the window, ran a file operation, then showed the window again. The application never hides its main window during a file operation. | Minor | The path is not reachable from current product code. Recorded so a future hide/restore feature retests it. | [R085](../PR_ROADMAP.md#r085) |
| D-020 | Weston 15.0.1 segfaults when a progress popup is created for a minimized window | [R083.f](R083f-file-worker-hang.md) | N01 | **Open (external).** Seen only on Weston 15.0.1, which is the isolated test compositor, not the primary Hyprland target. Other compositors were not tested. | Minor | An external compositor crash. It needs a Hyprland check: a long open or save started, then the window minimized, before the progress popup appears. | [R085](../PR_ROADMAP.md#r085) |
| D-010 | System Wayland 1.26 client leaks a destroyed-proxy reference | [R062.b proxy](R062b-wayland-proxy.md), [R058.c](R058c-native-diagnostics.md) | N01 | **Open (external).** The leak is reproduced standalone without Qt or SketchyUp. Only sanitized CI uses the patched private client (`scripts/ci-wayland-client.sh`); users get the system library. | Limitation | A 96-byte leak per affected dialog event, in a third-party library. Disclosed in R062.b as "a documented external limitation until a fixed distribution library is available". | [R085](../PR_ROADMAP.md#r085): track the distribution fix |
| D-011 | Qt Wayland client-decoration framebuffer leak | [R050](R050-native-render-ui.md) | N01 | **Open (external).** Sanitized Wayland steps still set `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` (`.github/workflows/native.yml`). | Limitation | Upstream Qt allocation, with no application frame in the stack. Disclosed in R050 as a test-environment workaround. | [R085](../PR_ROADMAP.md#r085): track upstream |
| D-012 | Compositor crash with RGB565/odd-stride surfaces at fractional scale | [R085.a](R085a-current-platform-validation.md), [R085.b](R085b-native-surface-color.md), [support matrix](../SUPPORT_MATRIX.md#runtime-and-services) | N01, N04 | **Mitigated.** The application now requests eight-bit surfaces and all 20 fractional cases pass. The external Weston/Mesa defect is not shown to be fixed. | Limitation | External defect, avoided by the surface-color policy ([ADR 0157](../decisions/0157-native-surface-color.md)). Disclosed in the support matrix. | [R085](../PR_ROADMAP.md#r085) |
| D-013 | Qt text shaping crashes with no installed fonts | [CI-PR172](CI-PR172-schema-cache.md) | N02, P05 | **Mitigated.** A Qt-only control program reproduces the crash. `packaging/arch/PKGBUILD` hard-depends on `ttf-dejavu`. | Limitation | Occurs only when a declared package dependency is removed. | [R087](../PR_ROADMAP.md#r087) |
| D-014 | Recovery and publication faults are injected, not physical power-loss crashes | [R083.b](R083b-integrated-publication-faults.md), [R083.d](R083d-local-failure-matrix.md) | D04, D02 | **Open by design.** R083.b "does not claim physical filesystem crash recovery". No data-loss defect is recorded in the fault matrix. | Limitation | The evidence boundary is stated; the final data-loss audit remains R083 work. | [R083](../PR_ROADMAP.md#r083) |
| D-015 | Interchange is lossy conversion; no native SKP/DWG | [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), ADRs [0111](../decisions/0111-native-gltf-import-workflow.md), [0115](../decisions/0115-native-obj-interchange-workflow.md), [0119](../decisions/0119-native-stl-interchange-workflow.md), [0123](../decisions/0123-native-dxf-interchange-workflow.md) | X02–X04 | Documented boundary. | Limitation | Losses are reported per conversion. SKP/DWG are gated investigations, not Core. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-016 | Blender handoff is one-way; animation exports PNG frames without movie encoding or resume | [user guide](../USER_GUIDE.md), [support matrix](../SUPPORT_MATRIX.md#exchange-boundaries), [M7](M7-accepted.md) | P03, P08, P09 | Documented boundary. | Limitation | P09 requires the one-way behavior to be documented, and it is. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-017 | Finite planar/sweep geometry limits; retained internal faces may be non-manifold | [M3](M3.md), [M4](M4.md), [M2](M2.md) | G01, E01, E05 | Documented boundary. | Limitation | Explicit enforced limits with bounded rejection. | [R088](../PR_ROADMAP.md#r088) (documentation) |
| D-018 | Remote assistant quality is probabilistic, and acceptance covers one configuration | [M5](M5.md), [R086.b](R086b-remote-evaluation.md), [R086.as](R086as-openai-inspection-batches.md) | A04, A05 | Documented boundary. Earlier failed campaigns (R086.ah, R086.ak) stay recorded; the current host passes 24/24. | Limitation | Disclosed in M5 and the support matrix. Fresh current-head CI and release review remain. | [R086](../PR_ROADMAP.md#r086) |

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

## Open acceptance work that is not a known defect

These are untested or unaccepted areas, not recorded failures, so they are not
classified above: hotplug, suspend/resume and input methods (N04); controlled
reference-hardware campaigns (R082); release-candidate recovery revalidation and the
final data-loss audit (R083); all-dialog and screen-reader workflows (N05);
current/reference Arch clean-machine package checks (R087); and the Core parity
report (R088).

## Counts

| Class | Count | IDs |
| --- | ---: | --- |
| Release-blocking | 1 | D-001 |
| Major | 2 | D-003, D-004 |
| Minor | 8 | D-002, D-005–D-009, D-019, D-020 |
| Limitation | 9 | D-010–D-018 |
| **Total** | **20** | |

No known open critical security defect was found. No data-loss defect is
confirmed open. D-002 was root-caused as a test-harness hang on a full CI disk ([R083.f](R083f-file-worker-hang.md)); no open hang or data-path candidate remains.
