# R052.b — Shared offset command

Date: 2026-10-05 UTC. Local command validation passes; CI and native tool pending.

`geometry.offset` accepts `body`, `face`, signed `distance` in meters, and optional
`space` (`local` by default, or `world`). It adds all surviving offset boundaries
to the selected body's planar arrangement. It preserves existing material
coverage and explicit holes. Insets partition faces; outsets may add exterior
faces. Contracted hole outlines inside an existing void remain editable wires;
the command does not silently heal that void. This boundary-insertion policy is
distinct from replacing a face with the kernel's computed offset region.

All surviving islands contribute outlines. Complete erosion rejects with
`OFFSET_COLLAPSED` and preserves document state. Zero is a core no-op; a batch with
no changes rejects under the existing command contract and creates no history.
New faces inherit
the selected face's appearance; descendants retain their own source appearance.
The existing arrangement limits and actionable intersection failures also apply.
The arrangement runs only in the selected body; intersected coplanar faces in
that body may subdivide, with lineage and appearance preserved.

World distance is computed on world-transformed loops before converting the
result back to local coordinates. This preserves perpendicular distance under
mirrored, nonuniform and nested transforms. Component edits retain the existing
explicit shared-definition versus unique-instance scope contracts.

Preview, transactions, revision guards, component scope and history all use the
same registered command. Native assistant discovery includes the command; this
slice makes no live-provider quality claim. Native pointer/keyboard controls and
interaction evidence remain R052.c; R052 and M6 are not yet delivered.

`offset_command_tests` verifies read-only preview and matching publication
lineage, one-entry Undo/Redo, source face colors, unchanged unrelated contexts,
container round trip, stale revision rejection, collapse rollback, late-batch
rollback, locked-body rejection and zero-distance handling. Analytical fixtures
check inset and outset coverage, preservation of an original hole, editable
outlines inside that void, closed incidence and unchanged volume of a solid,
and a unique-instance world offset through a mirrored nonuniform transform with
the original definition and sibling unchanged.

The six selected suites (`offset`, `offset_commands`, `commands`,
`component_scope`, `staging`, `assistant`) pass in 3.63 s. The offset command suite
also passes ASan/UBSan with leak detection. The checked-in
[`planar-offset.json`](../../examples/planar-offset.json) runs through the actual
CLI, saves and reopens a native model: three faces have analytic areas 17, 38 and
9 m², retaining the original 64 m² coverage and central void. No native interaction
or live-provider success is inferred from these command tests.
