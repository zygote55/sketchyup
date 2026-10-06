# Stable geometric anchors for annotations

R065.a, 2026-10-06. This foundational layer supplies validated, immutable-value
anchors and explicit remapping helpers. Persistent annotation records, automatic
edit integration, commands and native controls follow in subsequent R065 layers.
It does not claim dimensions are delivered to users yet.

An anchor is a fixed world point, a native vertex, an edge fraction measured from
the topology record's canonical endpoint order, or a point supported by three
native face vertices and normalized barycentric weights. Context/entity IDs stay
in their established namespaces. Face creation checks actual tessellated coverage,
including holes, and rejects off-plane/outside points. Face support follows
geometry deformation; all attached points follow the body's complete hierarchy,
including reflection and nonuniform scaling. World values remain metres.

Remapping consumes a before/after document of the same identity and its explicit
topology report. Vertex references follow unique descendants. Edge/face points
follow surviving or uniquely remapped support vertices, then select the unique
geometric descendant containing the resulting point. This handles a combined
split and deformation without snapping back to the old location. An attachment
exactly on a shared split boundary remains ambiguous rather than choosing an
arbitrary child. Missing support or removed geometry becomes missing. Context
reparenting/movement updates the resolved world location; appearance-only edits
preserve support coordinates exactly.

Resolved, missing and ambiguous are explicit states. Broken anchors retain their
last unambiguous world position so later native drawing can show a useful marker.
They never silently reconnect when some later geometry happens to use a matching
ID. Undo must restore the original anchor value together with geometry; explicit
rebind uses the validated constructors. Resolution itself cannot mutate a model.

Anchor fields reject irrelevant IDs/support/fractions, invalid enums, nonfinite
values, duplicate support vertices and unnormalized weights. A face lookup is
bounded to 32,768 triangles, lineage to 32,768 descendants and its descendant search to 262,144
triangle visits. Invalid or over-budget input rejects explicitly; it does not
quietly reinterpret an anchor as a fixed point. Cross-context consolidation with
no published cross-context geometric lineage remains a broken reference, pending
an explicit future remapping contract.
