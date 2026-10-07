# R084.j — Panel-local shortcut conflicts

2026-10-07. Failing native regression `1aefc5e`; protection `5592c6b`, include cleanup
`cd95eb8`; default collision correction `4fa28ee` and native text-key check `644b6a6`. Contract: [0153](../decisions/0153-panel-shortcut-reservations.md).

The first native run reproduced assignment of Ctrl+Shift+M to global command
search even though Outliner already owns that combination. The corrected editor
rejects the assignment, explains the panel owner and disables Reassign for that
reservation. A viewport-only modeling action may use the same key in its separate
scope. Restoring a previously saved global collision leaves the raw preference
bytes intact, disables only that global binding and shows a notice. Choosing a
nonconflicting key restores it; the Outliner action remains unchanged.

A fresh-preference bridge inspection also exposed the existing Ctrl+Shift+T
collision between Model panel and Create 3D text. The global panel toggle retains
that key; local text creation now uses Ctrl+Alt+Shift+T and displays it in its
tooltip. The final fixture asserts every declared public default remains active
and no default-conflict notice appears. A native Wayland 2× text workflow opens
Create 3D text with the actual new key, then passes its generation, cancellation,
stale-result, font portability and baking cases.

The [native matrix](R084j-platform-matrix.json) passes **8/8** across Wayland/X11
at 1×/2×: normal **16.875 s**, ASan/UBSan **24.578 s**, leak detection and
halt-on-error. Existing draft/Save/Cancel, live keys, typing, restart, reset,
unknown-setting preservation, floating-assistant routing and stale-editor cases
remain in the fixture. A separate native organization interaction regression
passes at Wayland 2×.

The policy suite passes normal **1/1 in 0.03 s** and ASan/UBSan **1/1 in 0.06 s**.
It checks atomic rejection even with explicit reassignment, raw unknown-entry
preservation, inactive saved reservations, disjoint scopes and successful repair.
Settings remain private; no provider request or actual credential is used.

This protects the current model-panel QAction tree. It does not add panel binding
editing or establish all compositor/third-party shortcut interactions. Full R084/M9
acceptance remains open.
