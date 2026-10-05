# 0048 — Split regions and filled outer shells

Status: immutable kernel fixtures and sanitizers pass locally; CI/dependency merges
pending. Depends on R056.b.

Split partitions two validated material solids into target-minus-tool,
tool-minus-target and their intersection. Each region has its own disconnected
parts and native material volume. These regions have disjoint material interiors;
shared boundaries remain separate editable identities. Tool-minus-target remaps
operand provenance and operand-error indices back to the original target/tool.
Every face retains its source face and orientation reversal. Empty regions remain
empty. The three bounded Boolean calls are immutable and publication is a later
command concern.

Outer shell first computes union, then retains only each material part's outer
connected boundary. Enclosed cavity faces and unused vertices are removed while
surviving native face identities and provenance remain. Material islands that
become covered by a filled outer boundary are removed from the output; independent
positive bodies remain separate. Through-holes belong to the connected exterior
boundary and are retained. A small solid sitting inside such a through-hole stays
separate, even though its bounds lie inside the larger body's bounds.

Containment is established by native shell analysis, not bounds alone. A temporary
probe reverses the candidate inner boundary solely for the alternating-winding
validator; neither original candidate nor returned material geometry is reversed
by that probe. Bounds reject impossible containment pairs. Eligible ambiguous or
touching candidate pairs reject explicitly; there is no guessed deletion. Ordinary
disjoint and independent box corner/edge contacts remain supported.

Both operations retain the Boolean adapter's input, precision and native validation
limits. Aggregate output across all Split regions is limited to 64 parts, 16,384
vertices and 32,768 triangles. Split uses exactly three bounded Boolean calls.
Outer-shell candidate analysis has a shared four-million work budget, charging
squared combined triangle count plus four times that count per candidate. Every
retained filled boundary independently validates as a native solid.

Trim uses the existing subtraction geometry. Its distinction is publication:
retain the cutting tool and explicitly retain or replace the target.

R056.d adds three shared commands. All require different editable raw `body` and
`tool` operands in an explicit `context`:

- `geometry.trim` requires `keepTarget`. The cutting tool always survives as its
  exact original record. An empty result with `keepTarget: false` deletes only
  the target; an empty retained result is a no-change rejection.
- `geometry.split` requires `keepOperands`. Target-minus-tool, tool-minus-target
  and overlap are published as separate bodies, including disconnected parts.
  Empty regions generate no bodies.
- `geometry.outer_shell` requires `keepOperands`. Filled union parts become
  separate bodies, with enclosed islands removed and through-holes preserved.

Publication runs in world coordinates, then converts each part back to its owner
frame. Split's tool-only parts inherit the tool's parent, transform, tag and
body appearance; target-only and overlap parts inherit the target's. Trim and
Outer Shell inherit the target. Every face inherits its original source color
and physical front/back materials, reversing material sides exactly when its
physical orientation reverses. Reflected placements preserve that convention.
Generated bodies do not inherit stale recipe properties.

The additive `solidOperations` receipt array names `sourceBody`, `toolBody`,
`operation`, `keepTarget`, `keepTool` and `parts`. Each part contains `body`,
`portion`, `generatedVolume` and face provenance (`face`, `sourceBody`,
`sourceFace`, `reversed`). Split portions are `target`, `tool`, `overlap`;
other operations use `result`. Volumes are operation-time world measurements.
Later commands prune erased parts/faces from receipts. Full, changes-only and
created-ID responses retain these records; existing `booleans` stays compatible.

Scoped component commands resolve consumed source identities from the pre-edit
member map and generated identities from the resulting member map. Top-level
receipts name scene bodies; `componentOperations[].solidOperations` keeps the
canonical records. Preview uses the same staged publication as commit. All parts
and explicitly consumed operands publish as one edit, with normal stale-revision
checks, rollback, Undo/Redo and native-container persistence. Native controls
follow these shared commands.
