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
retain the cutting tool and explicitly retain or replace the target. The next
command layer must implement that operand policy, Split region receipts,
Outer-shell publication, material inheritance, scoped component mapping, preview,
persistence and one Undo item. Native controls follow those shared commands.
