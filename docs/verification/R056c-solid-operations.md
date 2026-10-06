# R056.c — Split and Outer Shell kernels

Date: 2026-10-05 UTC. Depends on R056.b; shared commands/native controls remain
outstanding. [Semantics and bounded work](../decisions/0048-split-outer-shell.md).

`solid_operation_tests` checks independently expected native volumes, output
component counts, face provenance, source-plane membership and orientation:

- Two overlapping 2 m cubes split into 4/4/4 m³ target-only/tool-only/overlap
  regions. Intersecting those returned regions pairwise confirms disjoint material
  interiors. Tool-minus-target provenance names the original operands correctly.
- Identical, disjoint, face/edge/corner-contacting and nested solids retain the
  specified empty regions and disconnected parts. Input surfaces stay immutable.
- A joinery cutter splits into two target remainders totaling 6 m³, a 6 m³ tool
  remainder with a through-hole, and 2 m³ shared material.
- Outer shell fills a 7 m³ hollow body to 8 m³ and removes a covered material island,
  leaving six exterior faces with surviving target provenance. Two enclosed voids
  are also filled. Disconnected bodies remain separate.
- A solid inside an exterior through-hole remains separate; outer shell preserves
  the hole and returns 6.125 m³ rather than filling or discarding it.
- Six oblique large-origin mirrored/nonuniform transformations retain analytical
  split and filled-shell volumes. Repeated outer-shell calls retain deterministic
  geometry and provenance.
- Invalid open operands retain their original operand index and boundary defect
  classification, before any document mutation.

The new suite passes ASan, UBSan and leak detection. Four targeted CTest suites
(solid operations, Boolean kernel, shell containment and solid classification)
pass in 1.67 s. CI's core discovery includes `solid_operations` in regular and
sanitizer builds, including the build without Qt. No command, native UI or live
provider acceptance is claimed by this immutable geometry slice. Trim's geometry
is already subtraction; its distinct tool-retention policy belongs to the next
shared command layer.
