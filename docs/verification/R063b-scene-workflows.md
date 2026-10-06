# R063.b — Saved scene commands and native workflows

2026-10-06. Implementation `94367b8` plus the packaged example/schema formatting,
stacked above R063.a storage and the verified native style-dialog focus fix.
R063 delivery still requires dependency checks and the M6 gate.

Contract: [Scene authoring, inspection and native recall](../decisions/0071-scene-workflows.md).

- All **112 CTest suites passed in 114.01 seconds**, including the real Blender
  smoke test. Logs: `build/r063b-complete-{build,ctest}.log`.
- Six ASan/UBSan suites passed in **129.36 seconds**: saved scene commands, shared
  commands, bounded inspection, inspection sessions, staging and transaction
  coordination. Four native sanitizer checks passed on Wayland scale 2 in
  **27.369 seconds**: scene input, style rendering, assistant preview and native
  MCP. Leak detection remains enabled without suppressions.
  [Native sanitizer results](R063b-sanitize-native.json).
- Shared-command tests cover create IDs, atomic staging/rollback, every scene
  operation, selective model recall in one Undo step, no-op rejection, immutable
  snapshots, shared-component boundaries and missing-reference reporting.
  Temporary visibility tests retain uncaptured hiding and editor locks while
  restoring only the captured body scopes.
- Native scene input exercises all four property groups, selective camera-only
  recall, unchanged/invalid/stale drafts, rename without recapture, update property
  ownership, order/delete/Undo, keyboard tabs, clicking the selected tab again,
  reduced motion, transition completion/interruption, persistence, missing
  references and clearing tabs for a new document. OpenGL reports no errors.
- Independent JSON Schema validation accepts **102 valid command cases** and
  rejects **174 malformed cases** across three published command catalogs. Three
  query catalogs each accept three valid requests and reject three malformed
  ones. [Schema results](R063b-schema-validation.json).
- The installed four-command example creates two stable scene IDs. Display-free
  CLI acceptance covers paginated summaries, detailed selective state, visibility
  rows, every scene command, preserved geometry/records on recall, rename without
  recapture, no-op and atomic failure file preservation, and byte-exact relocation.
  All seven installed API catalogs, the decision document and desktop binary
  match their build inputs. [Installed results](R063b-installed-smoke.json).
- The source archive includes all **108 explicit install inputs byte-for-byte**
  and excludes build output and Git metadata. [Archive results](R063b-source-package.json).
- Complete, focused and sanitizer builds emitted no compiler warnings.

Native checks use isolated Xvfb/Weston and software Mesa in the test container.
They do not change the user's application, preferences or credentials. Camera
animation export remains outside this layer, as explicitly documented in the
contract; the native transition is navigation, not an exported timeline.

All **32 native checks passed in 110.838 seconds**, covering scene input,
selection, navigation, style input/rendering, viewport rendering, assistant preview
and native MCP on X11 and Wayland at scales 1 and 2.
[Native matrix](R063b-native-matrix.json). Raw Wayland scale-2 captures show the
[scene tabs/panel](R063b-scenes-window.png) and [selective editor](R063b-scenes-editor.png);
both were visually checked for readable controls, layout and viewport overlap.
