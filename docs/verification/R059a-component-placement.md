# R059.a — Immutable face-aligned component placement

Date: 2026-10-06 UTC. Depends on R058.c in the review stack.
[Placement contract](../decisions/0054-hosted-components.md).

`component_placement_tests` verifies:

- Interior, outer-boundary and hole-boundary anchors, tolerance-only plane snapping,
  rejection outside material/inside holes/off plane, winding-independent holes and
  unchanged source geometry/identities.
- Independent expected positions and basis vectors for a translated, rotated,
  reflected and nonuniform host with an offset/rotated component glue plane,
  explicit in-plane rotation and signed component scale.
- Inverse-transpose physical normals under shear and reflection, removal of the
  tangent's normal component before shear, and orthonormal component placement
  without inherited host scale.
- Oblique faces with 1e-4, 1 and 1,000 unit extents around a large coordinate origin,
  mirrored host transforms and explicit reversal of the selected face direction.
- Missing faces/vertices, nonplanar faces, invalid source axes, NaN source axes,
  parallel tangents, zero scale, infinite angles, singular transforms and a face
  exceeding the 4,096-corner preflight limit.

The placement, components, transform-selection and drawing CTest suites pass
(4/4, 0.10 seconds). The new suite also passes AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection. Normal, Qt-free and sanitizer CI
discover the new test through the core CMake target. No native UI, persistent host
binding, opening updates or new live-provider acceptance are claimed by this slice.
