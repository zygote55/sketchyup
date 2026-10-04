# R029: camera navigation and push/pull interaction

Date: 2026-10-03. Local validation passed; CI/merge pending.
Owner/implementer: coding agent under the owner's full-roadmap authorization.

The camera now has perspective/orthographic projection, top/front/right/back/left/
bottom/isometric presets and a 5–120 degree vertical field of view. Exact pole
views use a stable up vector. Projection changes preserve scale on the target
plane; fit accounts for aspect ratio and FOV. Picking, inference, clipping and
rendering share the same matrix. The navigation preference and FOV are stored in
application settings, without changing the document.

Mouse navigation retains middle/Alt-left orbit, Shift/right pan, wheel zoom and
explicit Orbit/Pan tools. An explicit Zoom tool works with left drag. Trackpad
mode maps scrolling to pan, Alt-scroll to orbit and Ctrl-scroll to zoom. Native
pinch, pan and rotate events preserve the current tool anchor and terminate any
pending button commit. Pointer-centered zoom anchors the camera target plane,
not an inferred surface depth.

Gesture values follow the incremental scale, angle and pixel semantics of
[Qt's native gesture API](https://doc.qt.io/qt-6/qnativegestureevent.html).
Tests synthesize Qt input on native Wayland and X11 windows; this does not claim
physical trackpad hardware coverage beyond the existing device matrix.

Push/pull measurements and previews use world distances, converted to the core's
signed local-normal distance for scaled contexts. Numeric re-entry amends the
same undo item and selects the resulting cap. Double-click and the Edit action
repeat the last distance as a fresh edit, guarded by the current document session.
A standalone Ctrl release toggles create-new-face mode; Ctrl shortcuts do not.

The public `geometry.push_pull` command accepts an optional strict boolean
`newFace`. With it enabled, a connected cap's original vertices, face and walls
stay in place while a new cap and sides are created. Identity maps include both
retained and new caps. Existing isolated-face sweeps already retain their base.
Internal retained faces can create non-manifold radial boundaries deliberately;
this mode is not a solid-union claim. Retaining a face at an opposing-face opening
rejects atomically; ordinary mode keeps the established opening behavior.

Validation:

- All 21 development suites and 16 ASan/UBSan suites pass.
- Core retained-cap fixtures cover unchanged original vertices and walls, new
  cap/side identities, a second extension, exact undo and opposing-face rejection.
  Command tests verify strict boolean validation and retained cap identity.
- The native workflow passes with Xvfb/Mesa at DPR 1 and 2: all eight camera
  presets pick the model correctly; FOV validation preserves the camera on error;
  scroll/native gestures keep anchored tools; scroll cannot commit a drawing drag;
  explicit Zoom works without a middle button.
- Native push/pull tests cover 2 m → 250 cm amendment, a fresh repeat to 5 m,
  one-step undo, Ctrl retained-face mode, 50 → 75 cm amendment, Ctrl+Z without mode
  toggling, and 150 cm world displacement in a context scaled 2× along its normal.
- A 6 × 4 m room shell with 20 cm walls is drawn on a tilted plane and pushed to
  2.8 m. Every shell edge has two incident faces; durable save/reopen preserves
  the encoded document exactly.
- The complete native regression set passes: selection, guides, constraints,
  inference, curves, drawing, numeric input, tool lifecycle, interaction,
  viewport/topology, dialogs and shell. The rendering check caught an unnecessary
  overlay upload on camera movement; only clearing an actual hover now invalidates
  the overlay, and buffer reuse/context recreation checks pass.

- The same navigation/room fixture passed on Wayland/Intel Mesa at DPR 1.6;
  the native application smoke test reported `glError: 0`.

[Wayland tilted-room capture](R029-room-wayland.png) was inspected by the
implementation agent. No independent human review or physical trackpad session
is claimed. CI runs the navigation fixture at software-rendering scales 1 and 2.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/navigation_input_tests
```
