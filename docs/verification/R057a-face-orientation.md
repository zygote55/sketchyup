# R057.a — Immutable face orientation

Date: 2026-10-05 UTC. Depends on R056.e in the review stack. Document commands,
front/back material publication and native controls follow this kernel slice.
[Orientation contract](../decisions/0049-face-orientation.md).

`orientation_tests` independently checks:

- All 64 combinations of reversed faces on a cube. Orient preserves the chosen
  seed, yields the corresponding fully outward or inward shell, reports only
  actual reversals and retains the independently expected 8 m³ volume.
- Exact double-reversal round-trip; unchanged vertex positions, wires, counts,
  edge records, IDs and allocators; reversed normals and unchanged face areas.
- Deterministic repeated calls and explicit no-change results for consistent
  outward and inward seeds.
- An open sheet repaired across its shared edge while a separate sheet connected
  only by a loose wire stays unchanged.
- All loops of a holed prism face reverse together and repair exactly. A cavity
  boundary repairs independently of the outer shell and restores 7 m³ material volume.
- A three-face non-manifold junction reports its native edge/face; an unrelated
  independent sheet remains orientable.
- A triangulated Möbius strip rejects with a contradictory-cycle classification
  and native face/edge IDs, without a partial mutation.
- Missing/empty selections and bounded face-count rejection; exact repairs after
  large-origin, oblique, reflected/nonuniform and centimetre-scale transforms.

The new kernel suite passes ASan, UBSan and leak detection. Five targeted CTest
suites (orientation, Boolean kernel, solid operations, shells and solids) pass in
1.82 s. CI core discovery includes the new suite in normal, Qt-free and sanitizer
builds. No publication, native UI or live-provider acceptance is claimed here.
