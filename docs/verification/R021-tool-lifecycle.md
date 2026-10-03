# R021: tool lifecycle and camera interleaving

Date: 2026-10-03. Local verification passed; PR merge pending.

A view-only ToolSession records Ready, Anchored, Preview and Committed states. Its
requests bind document identity and revision at the first point/face. Preview runs
the shared command validator on a private document copy; commit publishes one
batch/undo item. Another edit or document replacement invalidates the session.
Escape cancels an active operation without history or dirty-state changes; another
Escape returns to Select. Tools remain active after a successful commit.

Line, rectangle, circle and push/pull share this lifecycle. Click–move–click and
press–drag–release use the same commands. Dashed prospective edges remain view-only;
invalid geometry shows an explanation near the cursor and in the status bar. The
initial Line tool inserts one planar segment. Chaining and arbitrary drawing
planes remain R023 work, and units/amendment remain R022 work.

Temporary middle/right/Alt camera gestures preserve the world anchor and active
tool. Pointer tracking uses Qt's implicit button grab and a separate navigation
state. Focus moving to Measurements ends a held-button gesture but keeps the
anchor; a late release cannot commit. Window deactivation, hide, touch cancellation
and Escape cancel uncommitted geometry. Push/pull projects the pointer onto the
selected normal in world space while preserving local signed-distance semantics;
near-parallel views ask for an orbit or numeric input. Initial distance snapping
remains 0.1 m pending the shared inference/numeric work.

Validation: `tool_lifecycle_tests` passes on native Wayland and X11. It checks
session states, exact nonmutation during preview/cancel, one-step undo, stale edits,
click/drag equivalence, anchored camera movement, Measurements focus, pointer
cancellation, rejection recovery, Line commands and pointer push/pull. The existing
Wayland interaction, responsive shell and viewport suites pass; all 13 development
CTest suites pass. CI runs the new native lifecycle executable under Xvfb.

```sh
QT_QPA_PLATFORM=wayland build/dev/tool_lifecycle_tests
QT_QPA_PLATFORM=xcb build/dev/tool_lifecycle_tests
QT_QPA_PLATFORM=wayland build/dev/interaction_tests
QT_QPA_PLATFORM=wayland build/dev/shell_tests
QT_QPA_PLATFORM=wayland build/dev/viewport_tests
ctest --preset dev
```

These tests use native windows with generated Qt input. They do not replace the
later physical-device gesture/accessibility acceptance matrix. Preview generation
is synchronous and bounded by command limits; large-scene interactive latency
remains part of the performance work.
