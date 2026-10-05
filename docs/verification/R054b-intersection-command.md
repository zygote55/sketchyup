# R054.b — Context-scoped intersection command

Date: 2026-10-05 UTC. Depends on R054.a; native acceptance remains outstanding.
[Scope and numerical contract](../decisions/0045-face-intersections.md).

`intersection_command_tests` checks selected/context/model modes, target/reference
separation, preview/publication mapping parity, one history item, stale revision,
late-batch rollback, existing-seam no-op, source area and appearance, descendant
IDs, Undo/Redo and native persistence. Coplanar containment creates the expected
inner boundary without merging source bodies. Holed targets retain their void,
with intersection edges ending at each hole boundary.

Two crossing planes inside one body become four faces sharing one authoritative
four-incident edge, preserving the model's allowed non-manifold topology. Nested,
rotated, mirrored and nonuniform placements produce independently expected world
endpoints. Context mode excludes an outside group reference; model mode reads it
without changing its locked record. Hidden references and wrong-context edits
are handled explicitly.

A unique component-instance model intersection reads an external scene face at
the correct world placement and retains the sibling, source definition and
external reference. Existing component-scope regression and both new/adjacent
scope suites pass ASan/UBSan/leak checks.

Seven targeted suites (`intersection_commands`, `commands`, `component_scope`,
`transaction_dispatch`, `automation_session`, `mcp`, `assistant`) pass in 3.55 s;
the subsequent same-body radial-incidence fixture also passes. Installed
transaction/session/MCP discovery is regenerated and matches the live registry.
The complete published-command fixture includes executable sweep and intersection
cases. `examples/face-intersection.json` saves and reopens with two target faces
and the independent reference body retained. No native or live-provider quality
claim is made by this command slice.
