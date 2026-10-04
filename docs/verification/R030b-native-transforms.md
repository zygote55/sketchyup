# R030.b: native move, rotate, scale and flip

Date: 2026-10-03. All 22 development suites and the full native regression set pass. The native
transform workflow passes at DPR 1 and 2. CI validation is pending. Requires R030.a.
Owner/implementer: coding agent under the owner's full-roadmap authorization.

Move (M), Rotate (Q) and Scale (S) use the same public scoped-transform command
as the headless driver. Preselect faces, edges, guides or whole contexts, then
choose a pivot. With no preselection, Move can target an inferred endpoint;
otherwise clicking geometry selects a face or edge. Shared vertices remain
attached to incident geometry. Invalid nonplanar results reject before publication.

Move accepts click/click or drag, exact lengths, three displacement values and
absolute world destinations. Rotate accepts a pivot, baseline and destination,
or a typed angle after the pivot. Its axis is the chosen drawing-plane normal in
world mode, or local Z in local mode. Scale accepts a pivot, reference and
destination; pointer scale is the signed projection onto that reference. Numeric
scale accepts one uniform factor or three factors, including negative values.
The Edit menu flips about X/Y/Z through the world bounding-box center of the
selected geometry. Existing custom drawing planes allow other rotation planes.

The Edit menu's local-axis setting uses one context's captured frame; a multiple-
context selection uses world mode. Typed local displacements use local coordinate
units, including context scale. Pivots and bracketed coordinates always use world
coordinates. Move reuses point inference, directional locks and reference arming;
arrow locks retain their world-axis meaning. Local/world changes cancel an active
operation so its captured frame cannot change underneath the preview.

A standalone Ctrl release or the Edit checkbox toggles copy mode. Ctrl shortcuts
and pointer/focus transitions do not toggle it accidentally. Raw copies stay in
their editing context and select the newly mapped entities; whole-context copies
select the new hierarchy root. Re-entering measurements replaces the same undo
item, removes the old copy and selects the replacement's fresh IDs. A later edit,
undo/redo or document replacement invalidates amendment.

Previews remain private. Their world-space edges and construction guides are
rendered without publishing geometry. Escape cancels; camera gestures preserve the
anchor through the shared tool lifecycle. Temporary hiding, locking and editing
context restrictions apply before manipulation. Native local mode deliberately
requires one context; whole hierarchies can still transform through their root.
No automatic welding, booleans or triangulation is added by these controls.

Validation:

- The new native workflow covers shortcuts; exact units and numeric amendment;
  copy toggling, fresh copied selection and one-step undo; pivot rotation;
  reflected/nonuniform scale; singular rejection; selection-center flip;
  rejected no-op correction; private preview; inferred pointer/numeric parity;
  drag commit; Escape; rotated/scaled local axes and lock rejection.
- Additional fixtures verify connected cap motion with closed incident walls,
  inferred single-vertex movement, atomic nonplanar rejection and parent/child
  copies with correct inherited placement and replacement identities.
- An explicit drawing plane initially allowed stale inference to choose an edge
  before its endpoint. Transform clicks now acquire the current point before
  deciding the target; the endpoint/nonplanar regression passes.
- Tests preserve monotonic ID allocation floors across undo. Exact pointer tests
  target inferred vertices and midpoints rather than assuming a rounded screen
  pixel lies at an arbitrary exact coordinate.

The [X11 capture](R030b-transforms-x11.png) shows the source hierarchy and selected
copied hierarchy after numeric amendment. It was inspected by the implementation
agent. The capture mounts the host Noto fonts into the disposable Xvfb container;
the Arch package already depends on DejaVu fonts. The native smoke check reports
`glError: 0`.

Wayland validation is **not claimed for this PR**. The current desktop kept T3 Code
active while the mapped test window failed activation; repeated runs and an
explicit targeted focus request did not resolve it. The Wayland smoke run also
timed out waiting for rendered frames. The tests were not weakened to bypass
activation. Retest this workflow on an available Wayland session before accepting
the M4 platform checkpoint; earlier M3 Wayland evidence remains in its own record.

```sh
ctest --preset dev
xvfb-run -a env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 build/dev/transform_input_tests
xvfb-run -a env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 LIBGL_ALWAYS_SOFTWARE=1 build/dev/transform_input_tests
```

These are implementation-agent tests and captures, not independent human review.
