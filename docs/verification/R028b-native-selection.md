# R028.b: native selection and shared view policy

Date: 2026-10-03. Local validation and both CI jobs passed. PR #30 merged as `06d563d`.
Owner/implementer: coding agent under the owner's full-roadmap authorization.

Viewport selection identifies faces, stable edges and guides. Hover uses the
same target as clicking. Ctrl adds, Shift toggles, and drag direction chooses
fully enclosed window selection or touched crossing selection. Both use a
cropped depth-tested ID framebuffer, so holes and foreground occlusion match
rendered geometry rather than projected bounding boxes. Guides retain their
existing overlay convention. Infinite guide lines participate in crossing
selection but cannot be fully enclosed by a finite window.

Faces show a stippled fill and boundary; edges show a screen-space highlight;
whole contexts show bounds and highlighted guides. Double-clicking a face
selects its outer and hole boundaries; a third click selects connected geometry.
Context double-click/Enter in the Outliner opens the context, while viewport face
double-click never opens its container. Groups/components remain R032 work.

Tab/Shift-Tab traverses visible typed entities without moving keyboard focus out
of the viewport. Ctrl+A, Outliner multi-selection, command-palette selection,
counts and accessible summaries share the selection model. Escape clears
selection and then leaves one editing level. Retired IDs clear instead of silently
turning into a whole-context selection. A document change cancels a pending box
selection before it can commit stale IDs.

Temporary hide/reveal, hidden-geometry display and context locks are session-only.
Hidden mode does not bypass locks or active editing contexts. Visible inactive or
locked faces still occlude eligible geometry. Inference uses the same view policy:
hidden faces no longer occlude, hidden edge endpoints and entirely hidden curve
centers are excluded, and locked/inactive candidates cannot be snapped to. Explicit
and automatically chosen drawing planes also respect editing context eligibility.

Native Delete dispatches the public `geometry.erase_selection` command with typed
IDs. Strict nested validation rejects missing/duplicate/invalid entities before
mutation. Face/edge/guide deletion and context subtree deletion publish one edit,
with one undo. The core limit of 100 selected subentities still applies; whole
contexts support bulk deletion within existing document/history limits.

Validation:

- All 21 development suites and 16 ASan/UBSan suites pass.
- Command fixtures cover every published command, malformed nested selection,
  duplicate/retired IDs, atomic rejection, mixed deletion and one-step undo.
- Core inference fixtures distinguish hidden from locked occluders and verify
  hidden curve/vertex suppression without rebuilding immutable geometry caches.
- Native selection fixtures cover hover/click agreement, modifiers, holes,
  foreground occlusion, full/partial window and crossing selection, face boundary
  and connected expansion, lock/hidden/context guards, drawing inference guards,
  keyboard traversal, typed-only drawing in the active context, Outliner
  synchronization, framebuffer highlighting and
  mixed deletion/undo.
- The selection fixture passed on Wayland/Intel Mesa at DPR 1.6 and pinned
  Xvfb/software Mesa at DPR 1 and 2. The native application smoke check passed
  with `glError: 0`.
- The complete Xvfb native regression set passes: guides, constraints, inference,
  curves, drawing, numeric input, tool lifecycle, interaction, topology viewport,
  dialogs and responsive shell.

Selection is based on rasterized visibility at the current device pixel ratio;
subpixel geometry with no visible sample is not acquired by the ID pass. View
state is not persisted, formal groups are not introduced, and this slice does
not claim the M9 large-scene performance gate or independent human review.

[Wayland selection capture](R028b-selection-wayland.png), inspected by the
implementation agent, shows selected face stippling, visible edge highlighting,
and the foreground ring occluding the rear face except through its hole.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/selection_input_tests
# CI also runs selection_input_tests with Xvfb/Mesa at scales 1 and 2.
```
