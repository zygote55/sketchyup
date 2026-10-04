# R035.a: entity measurements and validated edits

Date: 2026-10-04. Local checks and both CI jobs passed; merged in
[PR #43](https://github.com/zygote55/sketchyup/pull/43).
Requires the native organization work in [PR #42](https://github.com/zygote55/sketchyup/pull/42).

`entity.inspect` reports names, ownership/type, tag/color, typed semantic fields,
counts, world/parent/intrinsic bounds, origin, edge length, area and solid status.
Face and edge queries restrict measurement to the requested subentity. Whole
contexts include descendants and hidden content. Unavailable volume is null,
never a placeholder zero. The [measurement decision](../decisions/0009-entity-measurements.md)
records coordinate frames, counting, tolerances, solid prerequisites and limits.

Three commands extend the catalog to 52 entries: `entity.position`,
`entity.dimensions`, and `entity.properties`. Position and dimension values are
meters in explicit world or parent coordinates. Geometry and transforms change
through existing atomic operations; they are not display overrides. Semantic
fields retain boolean/number/string types and existing persistence limits.
Native unit-aware field editing is recorded in [R035.b](R035b-native-entity-info.md).

## Validation

- Development: 32/32 suites passed. ASan/UBSan: 26/26 suites passed.
- Solid fixtures cover a box, concave extrusion, through-hole shell, mirrored
  nonuniform scaling, entirely reversed winding and translation near coordinate
  limits. Open boundaries, mixed winding, loose vertices, disconnected shells,
  point-contact vertex fans and a crossing closed triangular shell have no volume.
- A high-valence shell exercises bounded analysis rejection without a numeric
  volume claim. Surface and topology validation remain prerequisites.
- Entity tests compare world, parent and intrinsic bounds/areas/lengths/volume,
  group-descendant accounting, face/edge measurement, guide infinity, parent and
  world origin edits, rotated-member dimension readback, no-op repeated dimensions,
  undo, locks, semantic field limits and explicit shared component metadata.
- Public-command checks cover catalog discovery, field validation, query immutability,
  null volume for open geometry, and rollback of a position edit when a later
  dimension edit would collapse geometry.
- Native organization and component workflows plus application smoke pass on X11.
- CI exposed a Wayland test-focus race around rapid modal dismissal. A window
  could report active while its native surface had not yet regained keyboard
  focus. The initial activation check passed local repeats but was insufficient
  in CI. The test now waits for the exact native focus surface before focusing
  the tree for every shortcut, including Space immediately after a form. Form
  submission waits for the actual dialog and its native focus instead of assuming
  a fixed delay. Six consecutive runs at each Weston scale passed after this
  correction; production rename behavior is unchanged.
- `examples/entity-info.json` saves and reopens a 4 × 6 × 8 m block at (1, 2, 0).
  Its query returns 208 m² area, 72 m edge length, 192 m³ volume and exact typed
  semantic fields. CI executes and reopens this recipe.

R035 completed when the native Entity info adapter in PR #44 also passed both CI jobs. This is implementation evidence, not independent human acceptance.
