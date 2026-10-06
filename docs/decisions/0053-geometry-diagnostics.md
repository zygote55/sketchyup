# 0053 — Bounded findings precede explicit geometry repair

Status: R058.a diagnostic kernel locally verified. R058.b shared inspection
is locally verified; native report/repair controls follow.

Diagnostics are read-only derived data over an authoritative surface and edge
topology. They identify solid prerequisites and risky geometry; an open sheet or
loose wire may be intentional modeling geometry. No finding deletes, welds or
reorients anything automatically.

## Findings and completeness

The kernel scans all edge incidence to collect open boundaries, non-manifold edges
and inconsistent adjacent winding together. Explicit loose wire edges and orphan
vertices are separate categories. A face with `2 * netArea / perimeter` at or below
four geometry tolerances receives a near-degeneracy warning. Net area subtracts
holes independently of winding using origin-relative vectors. This warning does
not redefine the document's geometry-validity threshold or silently remove thin
features.

Topological findings prevent deeper closed-solid analysis and leave
`analysisComplete` false. Once those prerequisites pass, the existing bounded shell
analysis detects disconnected vertex fans, crossings/overlaps, unstable volume,
ambiguous containment and invalid cavity directions. These deeper checks stop at
the first defect: their affected-reference count is explicitly `countExact=false`.
It is a reported lower bound, not a claimed total of every defect in the body.

Validated shell hierarchies supply material volume and complete analysis. Native
outer/material-island shells should have positive signed volume and cavity shells
negative volume. A globally inverted but otherwise valid hierarchy therefore gets
an `inverted_shells` finding covering all wrong-way faces. Correct inward cavity
boundaries are not defects. Disconnected valid material parts receive an
informational finding because they are not one Boolean operand.

## Output and work bounds

At most 16 categories and 64 unique typed references per category are retained.
Counts for the full topological scan, thin-face scan and validated shell hierarchy
remain exact even when their reference samples are truncated. Each reference names
a native face, edge or vertex; related faces/edges make edge and vertex defects
frameable in the editor. Samples are deterministic and sorted, and `truncated`
explicitly identifies omitted references.

Input checks enforce at most 100,000 vertices/faces, 300,000 edges, 10,000 vertices
per loop and two million total corner references before adjacency allocation.
Existing shell, contact, triangulation and containment budgets still apply. A work
limit becomes an explicit `analysis_limit` finding with incomplete analysis and no
volume; it must never be displayed as clean geometry.

`reverseShells` is only a repair *eligibility* hint for a complete set of faces in
geometrically validated inverted shells. Truncation disables it. It never executes
an edit or authorizes reversing a partial sample. Later native controls must stage
an explicit shared command, retain source revision/session guards, preview the
change and publish one Undo item. Open/non-manifold/ambiguous findings do not infer
which unrelated geometry a user wants removed.
