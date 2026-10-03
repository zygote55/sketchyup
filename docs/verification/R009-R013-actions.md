# Command, history and shell foundation

Date: 2026-10-03. R009, R010 and R013; pending PR merge. M1 remains open for
viewport cache granularity, durable save envelope and clean package acceptance.

## Command/query registration and history

The catalog publishes each supported command's stable name, human label,
category, JSON parameter schema, validation boundary and batch undo behavior.
Required and unknown fields are checked against the catalog before dispatch;
typed decoding and authoritative model validation remain mandatory. The CLI adds
`--describe-command` and read-only `--query` routes. Registered queries are
`document.describe`, `commands.describe` and `capabilities`; they do not change
revision/history. Idempotency is explicitly reserved and unavailable until the
durable outcome ledger, rather than silently promising safe remote retries.

`command_tests` executes every catalog entry, removes every required parameter,
adds unknown parameters, checks failed requests leave exact state untouched, and
checks one-step undo. Color bounds are checked as doubles before conversion to
float so a slightly out-of-range value cannot round into validity.

The history corpus now exceeds the 64 MiB retained budget, verifies that oldest
undo entries are evicted, redoes every retained edit exactly, and invalidates a
redo branch within the budget. Existing atomic batch/stale revision tests remain.

A regression exposed vertex/face ID reuse after undoing an extrusion. Per-surface
allocator floors now survive undo/redo, including undoing/recreating the context
itself. New unrelated faces/vertices cannot use retired IDs through direct change
sets. Live records carry their allocator floor into explicit save/load. Undo
restores geometry/metadata but deliberately does not rewind allocator metadata.
Floors for permanently retired contexts are pruned when unreachable from live
records/history. Durable identities across discarded, unsaved sessions still
require the later journal/session protocol; this is not a remote retry guarantee.

## Native public actions and layout

Every QAction has an explicit stable ID and category; duplicate IDs/shortcuts
fail during registration. Selection-bound Move/Paint/Delete actions disable when
there is no selected object, and expose command/form metadata. Existing numeric
move and native color forms use the same validated core operations as automation.
The palette contains human actions, object lookup, scoped `face BODY/FACE` lookup,
and up to ten recent documents. It does not list transaction internals. Recent
paths are recorded only after successful open/save; missing paths fail normally
without replacing current work. Theme preference is retained across launches.

F6/Shift+F6 cycles visible Search, Tool rail, Viewport, Model list and Measurements
regions with focus indication. Hidden regions are skipped. The Model panel can be
toggled even in a narrow window. An internal Assistant container stays hidden
until its feature is implemented; no unavailable assistant buttons are shown.
Tests use temporary preference directories, leaving real user settings alone.

The 640/900/1200/1600 native layout corpus still verifies Measurements containment
and useful viewport space. Theme, camera, selection, focus and palette operations
do not mutate document content. Native tests now select an actual scoped face
through the palette, verify context enablement and F6 order, and find a saved file
through recent-document search.

## Reproduction and result

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
QT_QPA_PLATFORM=wayland timeout 40s build/dev/interaction_tests
QT_QPA_PLATFORM=wayland timeout 30s build/dev/dialog_tests
QT_QPA_PLATFORM=wayland timeout 40s build/dev/shell_tests
QT_QPA_PLATFORM=wayland timeout 40s build/dev/viewport_tests
build/dev/sketchyup-cli --describe-command geometry.translate
build/dev/sketchyup-cli --query capabilities
```

Core, scene/history, arrangement, command and persistence tests pass. Native
Wayland suites and X11 fallback interaction/dialog suites pass; ASan/UBSan core
checks pass. CI bounds graphical tests with timeouts so a modal regression cannot
leave a build waiting indefinitely. Final PR CI records remain the merge gate.
