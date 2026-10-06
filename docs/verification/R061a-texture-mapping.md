# R061.a — Affine texture-coordinate foundation

2026-10-06 UTC. [Representation and limits](../decisions/0061-affine-texture-mapping.md).
Prepared above the M6 acceptance stack; M6 CI/dependency merges remain pending.

The independent kernel constructs orthogonal texture projections with explicit
position, signed repeat size, rotation and offset, or an affine projection from
three position/UV pins. It evaluates unwrapped double-precision coordinates and
re-expresses the projection through an affine coordinate change using covectors.
Document storage, commands, image decoding, rendering and export are later R061
layers. This evidence does not claim that image textures are available in the UI.

`texture_mapping_tests` covers:

- Analytic coordinates for repeat size, offset, quarter-turn rotation, independent
  U reflection, opposite face frame and a vertical projection plane.
- Three-pin interpolation with independently calculated shear, reordered pins,
  normal displacement and agreement with an orthogonal placement.
- 726 sampled points under rotation, nonuniform scale, shear and both determinant
  signs at the engineering site origin. Transferred UV matches the original within
  `1e-9` repeats. An independent exact inverse-transpose coefficient oracle catches
  using tangent-vector transformation by mistake.
- Sub-millimetre offsets at `[800000.125,-700000.25,600000.5]` m within `1e-7`
  repeats; split-point interpolation across unwrapped repeat boundaries.
- Degenerate/non-finite inputs, parallel frames, collinear pins or UV, unsupported
  repeat dimensions, unstable gradients, singular transforms and transformed
  mappings outside explicit bounds. Input records remain unchanged.

All **101/101 development CTest suites** pass in **94.82 seconds**, including
real Blender and the integrated M6 workflow. The targeted suite passes in 0.01 seconds; ASan, UBSan and leak
detection pass in 0.03 seconds. CI includes the new suite in both its ordinary
CTest run and the explicit sanitizer target/selection. The contract is installed
with the existing decision documents.
