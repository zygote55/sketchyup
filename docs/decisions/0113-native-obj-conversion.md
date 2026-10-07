# Editable native OBJ conversion

R075.b, 2026-10-07. `loadObj(path, options)` captures bounded source and sidecar
bytes, constructs a new unsaved document in one edit, and validates its full native
serialization before returning it. The source and existing editor document are
never modified. Units and Y-up/Z-up are explicit; no scale is guessed.

Each OBJ object becomes a native group. Geometry is partitioned by the sorted
combination of group memberships; overlapping membership cannot be native shared
ownership, so combinations receive a notice and retain each original name in
`obj.group.N` metadata. Material and smoothing changes do not split geometry.
Position indices are shared within each geometry body. Ordered concave polygons
remain faces when their UV mapping is affine. Non-affine UV polygons become
editable triangles with per-triangle mappings and an explicit notice. Collapsed UV
mappings, self-intersections, nonplanarity and invalid native topology reject the
import. Loose lines remain wires. Unused positions are counted and omitted.

Normals are recomputed from native faces. Shared positive smoothing groups smooth
two-face edges; differing indexed corner normals retain hard boundaries. Imported
normals are not claimed exact. Two-sided native faces receive the OBJ material on
both sides. Kd linear reflectance converts to native sRGB; d/Tr retains opacity.
MTL scale/offset transforms apply before converting texture V to native top-left.
Faces lacking UVs use an untextured material copy with a notice. Missing materials
use neutral diffuse color. Missing textures remain explicit managed placeholders;
unsupported or invalid texture bytes retain color fallback with an explicit notice.
Only supported PNG/JPEG bytes are decoded. Other MTL shading is reported by parser.

MTL paths resolve relative to the OBJ directory, textures relative to their MTL.
Every declared texture path is checked, including unused or duplicate definitions.
Absolute paths, URL-like colon paths, backslashes, NUL and lexical or canonical
symlink escape reject. A nested MTL may use `../` only when the resolved path stays
within the canonical OBJ directory. Missing resources produce notices; code and
network lookup never run. Across MTL files the first definition wins, with a
reported duplicate count. Names are unique within each parsed library.

Limits supplement the parsers: 16 MiB total MTL, 128 MiB total captured package,
1,024 material definitions/native materials/images, 16 MiB per image and 64 MiB
managed image bytes. Each decoded image is at most 4096²/64 MiB and aggregate
decoded bytes at most 256 MiB. Expanded geometry is bounded to 100,000 vertices,
faces and wire segments, and 10,000 body records; native topology limits also
apply. Reports contain source hashes, counts, losses and notices without host paths.

Tests cover concavity, millimetres/Y-up, negative indices, group membership,
affine/transformed and non-affine UVs, managed images, missing resources, contained
nested MTL, symlink/parent escape, smoothing boundaries, wire-only models, atomic
undo/redo and source preservation. An independent Blender 5.2.1 exporter produces
textured ordinary/mirrored triangles at known world coordinates; import verifies
area, winding, units, group separation, UV and contained texture resolution.
Export packaging and native/CLI workflow remain subsequent R075 layers.
