# R015: persistent editing-context topology

Date: 2026-10-03. Local implementation checks passed; PR merge pending.

Each body is currently one editing context. Vertex/face IDs share the existing
surface allocator; edge IDs have their own context-scoped monotonic allocator.
Persistent edge records cover ordered face-loop boundaries and explicit loose
wires exactly. Oriented loop uses, vertex adjacency and arbitrary radial face
incidence derive from those records. Open, wire-only and non-manifold contexts
are permitted; closed-solid manifold validation belongs to later operations.

Core commits freeze and validate edge records with geometry. Unchanged endpoint
pairs keep their identity, splits retire the old edge, undo restores prior records,
and allocator floors survive undo/redo and explicit save. A new edge cannot reuse
a retired ID or reassign an existing ID to unrelated endpoints. Reports expose
created/deleted/modified vertex, edge and face IDs; edge split/merge ancestry is
based on overlapping collinear spans of newly created edges. Face ancestry is
explicitly supplied by topology operations and checked against committed records.

`geometry.wire` adds an explicit edge to a body (`"0"` creates a new context).
`geometry.split_edge` splits a persistent edge at a fraction strictly inside (0,1),
updating every incident face loop and wire atomically. `geometry.inspect` returns
context IDs, points, persistent edges, radial incidence and oriented loops. Batch
results include topology change reports, and a batch remains one undo item.
The existing GUI continues to render these records; direct edge drawing/picking UI
is scheduled in R017/R021 rather than simulated here.

Document schema v3 and feature `topology-v1` persist edge records and allocator
floors. The envelope remains version 2. Readers migrate original raw v1/v2 and the
actual R014 schema-v2 container fixture in memory. Prior readers reject the required
feature instead of silently losing identities. This is not durable remote identity
across unsaved/crashed sessions; epoch rotation/journaling remains R038/R041.

Initial limits: 300000 total persistent edges, alongside existing model budgets.
Retired-to-new edge ancestry comparison is capped at one million candidate pairs;
larger replacement edits reject before publication until a spatial matcher replaces
this bounded implementation. Changed-body copies and derived adjacency are not an
M9 large-model performance claim.

Checks include three faces incident on one edge, propagation to every loop, exact
undo/redo connectivity, branched allocator floors, wire-only contexts, explicit face
split lineage, invalid lineage/edge input and 120 seeded split/undo/redo sequences.
Persistence covers historical fixtures, edge-ID survival after save/undo/reopen,
invalid allocators and current container roundtrips. Command tests exercise both new
handlers and the read-only query. All seven CTest targets and four ASan/UBSan core targets pass. Native interaction
passes on Wayland and X11; Wayland viewport depth/picking/cache/context checks pass.
Two initial Wayland runs stopped at the Extrude shortcut assertion; the prior
baseline and repeated instrumented new-build runs passed. More precise focus/pick
assertions remain in the harness to diagnose recurrence; no product fix is claimed
for that intermittent host-window behavior. CI separately tests Xvfb input.

The [codec rerun](R015-codecs.json) includes the added persistent edge records.
As in R012, envelope decode includes validation/re-encoding for canonical comparison.
The CLI wire recipe was saved, reopened and inspected with exact edge IDs 2/3 and
allocator floor 4 after its original edge 1 was split within one atomic batch.

```sh
cmake --build --preset dev --parallel 4
ctest --preset dev
cmake --build --preset sanitize --parallel 4
ctest --preset sanitize
QT_QPA_PLATFORM=wayland timeout 40s build/dev/interaction_tests
QT_QPA_PLATFORM=xcb timeout 40s build/dev/interaction_tests
QT_QPA_PLATFORM=wayland timeout 40s build/dev/viewport_tests
build/dev/sketchyup-cli --script examples/split-wire.json --output /tmp/wire.sketchyup
build/dev/sketchyup-cli --input /tmp/wire.sketchyup --query geometry.inspect --context 1
```
