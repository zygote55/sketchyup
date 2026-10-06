# R058.c — Native diagnostic report and explicit repair

Date: 2026-10-06 UTC. Depends on R058.b in the review stack.
[Diagnostic and repair contract](../decisions/0053-geometry-diagnostics.md).

Edit → Geometry diagnostics reads the selected body or current editing context.
The report reuses the Formline report-sheet presentation, states that open sheets
and wires may be intentional, and excludes child records explicitly. Findings retain
exact/lower-bound counts and typed bounded samples. Select listed respects editing
context, visibility and locks; Frame listed also locates isolated vertices without
fabricating an unsupported vertex selection.

Native interaction fixtures verify:

- An open sheet reports four boundary edges and its face. Select/Frame leave model
  bytes and history untouched; open geometry does not infer a destructive repair.
- Stale revision, changed context, locks and replacement document sessions cannot
  act on obsolete references. Refresh renews a report in the same document only.
- Isolated vertices frame at the camera center, inside the perspective and
  orthographic clip planes, and remain non-selectable. Framing respects the existing
  minimum zoom distance.
- Complete inverted shells stage an immutable orientation preview. Escape leaves
  bytes/history unchanged; Apply produces one Undo item and preserves unrelated
  geometry; Undo restores the exact original body.
- Adjacent winding repair requires an explicitly chosen reference whose direction
  remains unchanged. The resulting cube has no diagnostic findings.
- Shared repairs update reflected/nonuniform component placements. Make Unique
  isolates the repair; native save/reopen retains the exact result.
- An active assistant preview remains intact and blocks competing repair staging.
  A 72-face inverted finding with only 64 references never offers whole-shell repair.
- The report closes its native surface before transferring focus. Model/context
  guards are rechecked afterward; edits or Escape during the pending handoff prevent
  preview creation. A retained hidden dialog exposed a Wayland focus loss during
  development; closing it before the owner-thread handoff resolves the transition.

All four native configurations pass: X11 and Wayland, each at DPR 1 and 2. The
Wayland DPR 2 fixture also passes ASan, UBSan and leak detection. Existing Formline
import interaction still passes, including corrupt/cancel preservation, unsaved-copy
lifecycle, source-safe save, face selection and Undo. CI includes all four diagnostic
variants and the native sanitizer run.

The owned isolated X11 recording covers the report and repair interactions:
`build/evidence/r058c/native-diagnostics.mp4`. FFmpeg decodes it without errors.
Committed images show the [report](images/R058c-geometry-report.png) and the
[orientation preview](images/R058c-repair-preview.png). The preview is captured from
the native OpenGL framebuffer so its arrows and selected geometry are included.

SHA-256:

- Recording: `bf92dbb301ff2fe704f852dfcfcd38d4f768278dd6e10be86bdfff202edfeba3`.
- Report: `2d994bde6914dcd52458431d5e5424715cc887dcaebe138804a47a83d8b27af2`.
- Preview: `a29f1060db405e41c419bc7373102243df69dbf62b76dc5ad25ec0ebb2106936`.

The full development build and **84/84 CTest tests** pass in **54.84 seconds**,
including the real Blender worker. Native orientation and responsive shell regression
suites also pass on X11. A fresh temporary installation reopens the installed
orientation example with no findings and the independently expected 8 m³ volume,
and includes the native report/repair contract. Owned temporary files are removed.

## CI focus/lifetime follow-up

CI exposed a report-to-model focus handoff failure and intermittent Wayland native
surface leaks in the interaction fixture. The handoff now requests activation while
the report is still alive, processes pending window-system events, closes the report,
and waits for actual destruction before checking stable native model focus and
creating the preview. Activation retries remain bounded by the existing five-second
handoff limit. Staleness, active-preview and Escape guards remain authoritative;
a weak lifetime guard protects the controller across event synchronization.

The fixture now requires the report's own `QGuiApplication::focusWindow()` and exact
active widget, rather than accepting a transient window as active before its native
focus delivery. Close checks verify destruction and drain pending window-system
events; teardown synchronizes before destroying the final model surface.

A deterministic delayed-deletion case fails against the original implementation:
it creates an orientation preview while the hidden report remains alive. The revised
implementation waits for actual deletion and then completes the same preview.
The complete revised fixture passes X11 and Wayland at DPR 1 and 2. Three consecutive
Wayland DPR 2 runs pass AddressSanitizer, UndefinedBehaviorSanitizer and leak detection,
with no sanitizer suppression or disabled check. Fresh exact-head CI remains required
before merging this layer and its dependents. Existing visual captures are unchanged.
