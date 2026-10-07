# R084.f — Native shortcut editor and keyboard routing

2026-10-07. Initial implementation `cf13656`; plain assistant routing `c1c91a9`;
explicit destination activation `bb36346`. Contract: [0149](../decisions/0149-native-shortcut-editor.md).

The [matrix](R084f-platform-matrix.json) passes **8/8** on Wayland/X11 at 1×/2×:
normal **14.789 s**, ASan/UBSan **21.721 s**, with leak detection and halt-on-error.
It exercises staged assignment, Cancel preserving exact saved bytes, rejection of
unassigned edits on Save, actual rebound tool keys, conflict rejection and explicit
reassignment, field typing, live command labels, two window lifetimes, reset of
routing, unknown-setting preservation and stale-save rejection. Dialog operations
use keyboard-activated buttons and verify actual activation; command selection and
sequence setup use Qt fixture APIs. This is not a claim that the test itself is
entirely free of programmatic focus/selection setup.

The floating assistant checks a rebound modifier combination, then a plain-letter
binding that must leave composer typing intact. Native X11 initially left focus in
the other window; explicit user toggling now activates the assistant or model before
routing focus. The fixture uses an isolated, noncontacted local-model configuration
to enable the composer; no prompt is submitted and no actual credentials are read.
An earlier unconfigured fixture correctly focused Preferences instead of the
unavailable composer and was corrected without changing that application behavior.

Early synthetic button events raced focus transitions. The test now waits for
focus and verifies a clicked signal before inspecting results; final matrix entries
are from the corrected fixture. Every run preserves document content and history.

The [versioned policy](R084e-shortcut-bindings.md) separately covers bounded and
malformed records, unsupported versions, new-default conflicts and unknown values.
Text scaling, full dialog/Outliner access and assistive-technology coverage remain
required before R084/M9 acceptance. Remote CI and ordered integration remain open.

The existing keyboard-only measured-modeling and legacy-preference suites also
pass on native Wayland 2× against the final shortcut implementation, retaining
rectangle/push-pull/undo and saved-preference behavior.
