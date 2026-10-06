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

CI exposed two report-to-model lifetime problems. The original handoff could stage
an orientation preview while a hidden report still existed. Activation now starts
while the report is alive, and repair waits for actual destruction and stable native
model focus. Activation retries remain bounded by the five-second handoff limit;
staleness, active-preview and Escape guards remain authoritative.

A later CI run reproduced a 96-byte Wayland surface proxy leak despite passing
local repetition. A protocol trace correlated the leaked proxy with a report surface
that was destroyed before its queued keyboard-leave event was dispatched. Merely
hiding and calling `QGuiApplication::sync()` also failed the new regression: Qt's
Wayland backend does not implement the synchronization capability used by that API.

The report now hides while retaining its native surface, then awaits an asynchronous
`wl_display_sync` callback on Qt's default event queue. The callback defers final
`QDialog::done` until native dispatch returns. External close events are held pending
so `QWindow::close` cannot destroy the platform surface early. Duplicate close
requests retain the original result; callback cleanup is tied to dialog lifetime.
Canceled and stale repairs keep the handoff fence until report destruction too.
The public Qt Wayland application interface is available at the minimum Qt 6.8;
the desktop explicitly links `wayland-client`, with Arch and CI dependencies listed.
Core-only configuration passes without acquiring this dependency. The relevant
upstream behavior is documented in the [Qt Wayland window implementation](https://raw.githubusercontent.com/qt/qtbase/6.11/src/plugins/platforms/wayland/qwaylandwindow.cpp)
and [Qt GUI synchronization implementation](https://raw.githubusercontent.com/qt/qtbase/6.11/src/gui/kernel/qguiapplication.cpp).

The native fixture requires each report's actual `QGuiApplication::focusWindow()`,
checks focus has left before `finished` permits deletion, and covers the close
button, Escape, repeated closes, stale/canceled repairs and retained hidden reports.
The retained-report regression fails against the original handoff; the focus-release
regression fails against the hide-plus-Qt-sync variant and passes with the compositor
acknowledgement. No sanitizer suppression or disabled check is used.

Seven final Wayland DPR 2 runs pass AddressSanitizer, UndefinedBehaviorSanitizer and
leak detection: an initial run, five consecutive repetitions, and one protocol trace.
The final trace's 28 keyboard-leave events all retain their live surface argument.
The complete final fixture also passes X11 and Wayland, each at DPR 1 and 2.
Fresh exact-head CI remains required before merging this layer and its dependents.
Existing visual captures are unchanged.


## Main-window shutdown follow-up

Fresh CI subsequently exposed the same proxy lifetime problem on the **main**
window, after all report interactions passed. The allocation stack points to the
fixture's initial `window.show()`. A local protocol trace reproduced the leak:
`wl_surface#17.destroy()` preceded a queued `wl_keyboard.enter(..., nil, ...)`.
The report's focus-release checks continued to pass; this is a separate shutdown
path exposed by the report returning focus immediately before application exit.

The main window now unmaps and awaits the default-queue compositor acknowledgement
inside its original close event. The local event loop keeps native dispatch alive;
Qt retains that event's original visibility and applies its normal last-window signal
and automatic quit behavior after the handler returns. A first deferred-reclose
variant failed the automatic-exit regression because hiding synchronizes both QWidget
and QWindow visibility. Duplicate close requests cannot skip the barrier. A document
change during the native drain cancels shutdown and remaps the window for review.
Callback lifetime is bounded by the local wait, including external event-loop exit.
The regression fixture exercises both the intervening-edit case and the actual
application event loop, requiring exactly one last-window signal and normal exit.
The follow-up passes all four native display variants and four Wayland DPR 2
ASan/UBSan/leak runs: a protocol trace and three consecutive repetitions. All 58
keyboard enter/leave events in the final trace retain live surface arguments, and
the fixture verifies both canceled shutdown after an edit and normal automatic exit.
Existing M4 save/recovery/canceled-close and Formline import workflows also pass on
Wayland DPR 2. No sanitizer suppression is used. Fresh CI remains required before merging.
