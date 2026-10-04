# R027.a: guide records, measurements and persistence

Date: 2026-10-03. PR #27 merged as `b9188a0`; both CI jobs passed. Local verification passed.

Guide points and infinite guide lines are immutable context records separate
from surface vertices, edges, faces and analytic curves. Line directions are
unit vectors; point origins and world-transformed origins obey the existing
coordinate bounds. Guides share the monotonic surface/curve ID allocator but
cannot collide with either entity kind. Cleanup leaves editing contexts and
model topology intact. Guide-only contexts are valid.

The document core supports creation, individual deletion, context cleanup and
whole-document cleanup. Each operation uses the existing atomic revision/undo
path. Change reports include created/deleted/modified guide IDs; guarded
amendments retire replaced IDs. History accounting includes guide storage.
Limits are 1,024 guides per context and 10,000 per document, enforced on edits
and decode. Offsets require a line lying in the supplied plane and a nonzero
signed distance. Angled guides use a canonical plane and a signed angle within
one revolution. Distance and signed-angle measurements do not mutate documents.

Six shared commands expose point/line creation, angled construction, offset,
delete and cleanup. Local/world conversion preserves world offsets under
mirrored nonuniform transforms. `geometry.inspect` includes guide records;
`geometry.measure_distance` and `geometry.measure_angle` return meters/radians.
Preview and commit use the same operations and report the same guide changes.

The raw document schema is version 5, with an explicit guide array per body.
Container version 2 remains unchanged; its document chunk uses `json-v5` and
requires `guides-v1` alongside scene/topology/curve features. Older raw schemas
and container encodings still load without inventing guides. Actual version-4
curve files were captured from commit `25594d6` before the codec change; migration
preserves their analytic parameters, topology and IDs. Unknown kinds/fields,
malformed vectors, non-unit directions, duplicate/colliding IDs and invalid
allocator floors reject rather than silently dropping data.

`examples/guides.json` creates a baseline, a parallel sill guide at 0.9 m, a
point on the sill, and a 45-degree line in the wall plane. Saved/reopened
[inspection evidence](R027a-guides.json) reports four guides, zero model vertices/edges/faces, and revision 1
for the entire recipe. This slice does not yet render or acquire guides in the
native viewport. R027.b supplies that UI and the full measured drawing fixture;
R027 and the M3 gate remain incomplete.

Validation:

- All 20 development suites pass, including guide geometry/identity tests,
  every published command's schema/dispatch/undo cases, atomic previews/batches,
  and actual version-1 through version-4 migration fixtures.
- All 15 ASan/UBSan suites pass. The guide suite was rerun in both builds after
  adding aggregate-limit coverage for individually valid contexts.
- Fixtures cover the 0.9 m sill, signed angles on a wall plane, zero/invalid
  vectors, guide/topology/curve ID collisions, both resource limits, context and
  global cleanup, exact undo, guarded amendment and mirrored world offsets.
- Persistence tests cover exact guide roundtrip, cleanup/reopen allocator floors,
  malformed/unknown guide fields, duplicate IDs and coordinate bounds. Existing
  durable-save and fault-injection tests remain green.
- Wayland constraint and curve interaction regressions pass at scale 1.6. The
  native graphics/picking/text smoke check passes with no OpenGL errors.
- The CLI guide recipe saves and reopens successfully; CI now executes it too.

```sh
ctest --preset dev
ctest --preset sanitize
build/dev/sketchyup-cli --script examples/guides.json --output /tmp/guides.sketchyup
build/dev/sketchyup-cli --input /tmp/guides.sketchyup --query geometry.inspect --context 1
```

