# R088.e — Viewport accessibility limits in the user guide

2026-10-10. Closes the "documented viewport accessibility limits" clause of Core
row N05 that [R088.d](R088d-core-parity-gaps.md) found missing from the installed
guide. Documentation only: no product behavior changed and no new test ran beyond
the link check below. R088, R084 and M9 remain open.

The new [Accessibility and keyboard use](../USER_GUIDE.md#accessibility-and-keyboard-use)
section replaces the single design-document statement ([UX_DESIGN §10](../UX_DESIGN.md)).
The [support matrix](../SUPPORT_MATRIX.md) accessibility row now points to it and
states that the viewport is a rendered canvas.

## Evidence for each claim

| Claim in the guide | Evidence |
| --- | --- |
| Accessible name "3D model viewport" | `src/app/viewport.cpp` `setAccessibleName` |
| Description is the selection count summary and updates on selection change | `Viewport::selectionChanged`, `Selection::summary` (`src/core/selection.cpp`); asserted in `tests/selection_input_tests.cpp` ("Viewport exposes an accessible selection summary") |
| Assistant preview replaces the description with world bounds | `src/app/viewport_assistant.cpp` `setAssistantPreviewFocus` |
| Canvas exposes no geometry, positions or hover targets | `grep QAccessible src` finds no custom accessible interface; the viewport uses Qt's default widget interface |
| "Selection summary" label description adds name, context and face area | `Window::sync` (`src/app/window.cpp`); [R084.l](R084l-linux-accessibility-bridge.md), [0155](../decisions/0155-linux-accessibility-bridge.md) read it through AT-SPI |
| Entity info bounds, area, volume read via AT-SPI (8 × 6 × 0.15 m, 100.2 m², 7.2 m³) | R084.l four-case matrix |
| F6 / Shift+F6 region order | `Window::focusRegion`, `focusRegions_` (`src/app/window.cpp`); [R084.a](R084a-local-accessibility-inventory.md) F6 transitions |
| Ctrl+K searches commands, objects, recent files, `face N/M` | `Window::palette` |
| Tool letters, typed digits move to Measurements, `[x,y,z]`, Enter, Escape | `addTool` calls, `Viewport::keyPressEvent`; [R084.d](R084d-keyboard-modeling.md) |
| Right/Left/Up axis locks, typed length follows the lock | `Viewport::constraintKey`; `tests/constraint_input_tests.cpp` ("Arrow locks red axis", "Typed length obeys locked direction", "Up locks world Z") |
| Tab / Shift+Tab, Enter, Escape in the Select tool; Ctrl+A | `Viewport::selectionKey`; `tests/selection_input_tests.cpp` (Tab, Shift-Tab, Enter, Escape). Ctrl+A is the `edit.selectAll` action; no key-level test was checked |
| Outliner F2, Space, Ctrl+Shift+L, Enter, Escape, undo/redo | [R084.k](R084k-keyboard-outliner.md), [0154](../decisions/0154-keyboard-outliner-workflow.md) |
| Edit → Keyboard shortcuts…, text size 75–200%, themes, reduced camera motion | `src/app/window.cpp` actions; [R084.f](R084f-native-shortcut-editor.md), [R084.h](R084h-interface-text-size.md), [R084.c](R084c-preference-compatibility.md) |
| Contrast above 4.5:1 for input, selected, button, hint and link text | [R084.g](R084g-selected-text-contrast.md), [R084.i](R084i-context-link-contrast.md) |
| Focus indicators for text fields, buttons, lists, trees, tool rail, viewport only | `:focus` rules at `src/app/window.cpp` `applyTheme`; viewport ring in `Viewport::paintGL` (`hasFocus()`); the gap is the one R088.d records |
| No orbit/pan/zoom keys; standard views, Fit, Walk, Look around keys | `addTool` hints and `view.*` actions; `src/app/viewport_walk.cpp`; `tests/navigation_input_tests.cpp` (W walk, Look around) |
| Snapping and parallel/perpendicular depend on pointer hover | `Viewport::event` Tab cycling uses `inferenceCount()`; `constraintKey` Down: "Hover an edge to use parallel or perpendicular" |
| Push/pull by typed distance | R084.d fixture |
| Move/Rotate/Scale accept typed pivot, displacement, angle, factors | `Viewport::transformMeasurements`; not covered by a keyboard-only fixture, which the guide says |
| Follow Me, Intersect, Solid tools, Face orientation apply with Enter | `Viewport::keyPressEvent` |
| Paint requires a click and disables Measurements | `Window` `toolChanged` handler; `edit.paint` action |
| Freehand has no keyboard path | `addTool("Freehand", "")` has no shortcut; stroke is created by press/release in `mousePressEvent`/`mouseReleaseEvent` only |
| Dialog Tab traversal for four dialogs | [R084.p](R084p-dialog-keyboard.md) |

## Left out because it could not be verified

- Keyboard switching between model-panel tabs (for example to Info). The audit
  and bridge fixtures switch tabs with `setCurrentIndex` or an AT-SPI Press action.
- Whether any screen reader announces a selection change. The status line and
  description update, but there is no interactive Orca, speech or braille run
  and the code raises no accessibility alert events.
- Visible focus on combo boxes, spin boxes, check boxes and tab bars after PR #286
  ([R084.q](https://github.com/zygote55/sketchyup/pull/286)). It is not on `main`, so the guide describes the
  current gap and needs a one-line update when that layer merges.
- The behavior of Enter on a selected face in the Select tool (boundary selection).

## Checks

A link script confirmed that every relative link in the guide, the support matrix
and this record resolves, and that each pinned `blob/<sha>` link names a path present
at that commit. No test compares the installed guide with a recorded hash; as in earlier
guide edits ([R084.r](R084r-display-precision.md)), only the source is changed. A
docs-only CMake configure and install copies `USER_GUIDE.md` and `SUPPORT_MATRIX.md`
byte for byte (`cmp` against the sources).
