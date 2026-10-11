# R082.dd — Share placement meshes in the viewport

2026-10-10. Change `52097a22` on `build/r082dd-instanced-viewport`, based on `809bdbcf`,
merged with `origin/main` `c04aea04` (R082.cc, R084.r) and then `9769325f`.
Only the viewport's caches and draw path change. Core still expands every
placement into scene bodies; persistence, inference, construction and limits
belong to R082.cc, .ee, .ff and .gg. [Raw reports and hashes](R082dd-instanced-viewport.json);
[native and sanitizer logs](R082dd-native-matrix.txt).

**Accepted: framebuffers are not bit-identical.** Picking, selection,
transparency order, shadow casters, canonical fixture hashes and every existing
native assertion are unchanged. A shared placement mesh moves 3 or 4 pixels of
each 1920 × 1080 benchmark frame (largest channel delta 159). Turning sharing off
reproduces the pre-change frames exactly. No expected hash or tolerance was edited.
The difference is accepted: it comes from rounding a placement-space position and
its translation separately at triangle edges, it changes no semantic result, and
R082.a's "exact framebuffer" requirement concerns the unclipped frame size, not
pixel hashes.

## Design

The body cache has three levels:

- **Tessellation** (`BodyGeometry`): local triangles, edges and adjacency in body
  coordinates. Bodies that project one canonical definition member share it,
  keyed by that member's record. Records are immutable, so an edited definition
  produces a new key; tessellations are replaced, never mutated.
- **Display mesh** (`BodyMesh`): the vertex arrays and GPU buffers. A *placement
  mesh* holds positions in placement space: the world transform without its
  translation. It is keyed by the member, the nine linear entries of the world
  transform, and the hidden, inactive-context and locked state.
- **Per-body state** (`BodyCache`): world transform, bounds, section data and
  membership. World-space triangles and edges are no longer stored for any body;
  every consumer recomputes them with the same `Transform::point` arithmetic.

A body may draw a placement mesh only if its displayed content (vertices, faces,
wires, edges, edge appearance, colors, materials, texture mappings) equals the
canonical member's. Allocator floors are ignored; R082.cc allows them to differ.
Membership only selects the candidate. Lighting, solar light, reflection swaps and texture coordinates depend only on the linear part,
so placement-space vertex attributes equal the per-body ones exactly.

Each body is still drawn in the same order, with the same draw calls as before.
The vertex shader adds a per-draw `offset` uniform, computed in doubles as the
placement translation minus the camera-relative render origin. Unshared draws pass
zero. This keeps distant placements as precise as world-space vertices; the far
family (900 km offset) shares one mesh and picks identically.

On the CPU, placement-space position plus translation equals `world.point` bit for
bit, because `point` is defined as `vector` plus the translation. Transparency
sorting, solar shadow bounds and all picking paths therefore see identical doubles.

### Incremental updates

- Moving a placement updates its transform, bounds and a check of degenerate
  triangles and short edges in world space. Its mesh is neither rebuilt nor uploaded.
- Editing a shared material or definition rebuilds each affected placement mesh
  once. Other placements with the same key use it without rebuilding.
- Make Unique gives the placement a new canonical member, so it moves to that
  definition's mesh. Its tessellation is reused because the content is unchanged.
- Context recreation re-uploads each live mesh once and keeps CPU tessellations.

`bodyMeshBuilds`, `bodyWorldUpdates` and `bodyUploads` still count bodies, as
existing tests require. A body counts as uploaded when its GPU mesh was replaced.
New counters report the work itself: `tessellations`, `meshUploads`,
`placementMeshes` and `sharedPlacements`.

## Per-placement state

| State | Handling |
| --- | --- |
| Selection and hover highlight | Separate overlay batches, rebuilt from on-demand world triangles |
| GPU selection IDs | Pick batch rebuilt from on-demand world triangles and edges |
| CPU face/edge picking, window selection | On-demand `world.point` triangles and edges, same bounds culling |
| Body, ancestor, tag or session body hiding | Key field; hidden placements share a hidden-state mesh |
| Show hidden geometry, style, solar, policy changes | Presentation revision; dependent meshes rebuild once |
| Nested-edit dimming, inactive edge color, locks | Key fields |
| Materials, textures and texture mappings | Content equality plus material and image inputs; reference images fall back |
| Placement linear part (rotation, scale, mirror) | Key; lighting, normals and front/back swap baked per linear part |
| Smooth shading and edge appearance | Member content |
| X-ray and other transparency | Shared transparent triangles translated in doubles into the global sort |
| Solar shadows | Shadow pass draws with the same offsets; caster bounds use world doubles |
| Clip plane | Shader world position includes the offset |
| Profiles | Recomputed from world triangles and shared adjacency |

### Placements that draw their own world-space mesh

These keep the previous path unchanged:

- an active section cut affecting the body;
- a view-only opacity override other than 1;
- session hiding of an individual face, edge or guide;
- reference images, groups, and bodies outside a component placement;
- a body whose displayed content differs from its canonical member;
- a triangle that is degenerate, or an edge shorter than tolerance, in world space but
  not in placement space, or the reverse.

The unique-geometry family has no placements and still matches the old framebuffer
exactly. Its body vectors also drop from 101,456,000 B to 89,088,000 B because
world-space copies are gone.

## Pixel identity

The difference comes from rounding. The old path rounds once, as
`float(world − origin)`. The shared path rounds the placement-space position and
the offset separately, then adds them in the shader. Some coordinates then differ
by one float unit, which can move a rasterized edge or shadow-map sample.
Exact equality would need per-placement vertex data, which this layer removes.
A model-view matrix composed in doubles has the same per-vertex rounding problem.

| Comparison (software llvmpipe) | Frames | Differing pixels | Largest channel delta |
| --- | ---: | ---: | ---: |
| Benchmark repeated, deep, far: warm and final | 6 | 3 each | 132 |
| Benchmark textures: warm and final | 2 | 4 each | 159 |
| Pre-change build vs sharing off (four families) and unique (no placements) | 10 | 0 | 0 |
| Placement fixture, 18 states, X11 scale 1 | 18 | 0–65 | 192 |
| Placement fixture, 18 states, X11 scale 2 | 18 | 0–64 | 255 |

The new placement fixture compares shared and unshared drawing in 18 states. They
cover styles, shadows, hiding, tags, nested context, face hiding, opacity, clipping,
sections, linear and mirrored placements, definition edits, Make Unique, distant
placements and textures. Every state has identical CPU picks, edge picks and GPU
selections. Pixels differ only on edges and shadow boundaries; X-ray, wireframe,
section and orthographic states match exactly. The fixture reports pixel counts but
does not assert them, pending the decision. One line, `placementSharing_{true}`,
restores the old path.

## Memory and software timing

Debug build, llvmpipe, X11, one CPU and 4 GiB in the CI-like container, 1,000
placements per family. These are single runs on a shared host, not hardware evidence.

| Family | Body vectors B | GPU payload B | Peak RSS B | Viewport p95 ms | Edit + readback p95 ms | Framebuffers |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| repeated | 101,456,000 → 89,266 | 34,656,000 → 34,656 | 619,331,584 → 479,211,520 | 294.8 → 194.1 | 1,395.2 → 572.7 | 3 pixels differ |
| unique | 101,456,000 → 89,088,000 | 34,656,000 → 34,656,000 | 611,315,712 → 603,279,360 | 381.9 → 221.1 | 983.2 → 989.9 | identical |
| deep | 101,456,000 → 89,266 | 34,656,000 → 34,656 | 610,816,000 → 468,844,544 | 383.7 → 191.7 | 1,118.4 → 1,084.9 | 3 pixels differ |
| far | 101,456,000 → 89,266 | 34,656,000 → 34,656 | 610,246,656 → 469,389,312 | 206.1 → 182.5 | 654.8 → 558.2 | 3 pixels differ |
| textures | 101,456,000 → 89,266 | 34,656,000 → 34,656 | 884,760,576 → 743,559,168 | 389.4 → 351.0 | 1,173.8 → 1,174.0 | 4 pixels differ |

Before is `809bdbcf` with only the benchmark's framebuffer hashes added. After is
this change before its last edit, which stopped comparing allocator floors; the
merged runs below use the final code. Each family is one run of each build. The repeated, deep, far and
textures families each draw 1,000 placements from one placement mesh, with about
1,000 tessellations (group roots and the member) instead of 2,000 meshes. On the merged tree the same
families report the same body-cache and payload bytes, one placement mesh and the
same 3–4 pixel differences. With sharing off, the merged build's warm frames still
equal the pre-change frames. Fixture hashes there differ from the table only because
R082.cc changed the container schema; sharing on and off agree.

Canonical fixture hashes are unchanged in all five families. Peak RSS falls by
about 140 MB in the four placement families. Timing changes are within this host's
run-to-run variation: the unique family, which takes the old path, also changed. Draw calls are unchanged,
with one extra `glUniform3f` per draw.

## Test matrix

All runs use the exact `native.yml` invocations in the one-CPU container.
Counts are for the merged tree unless marked.

| Check | Result |
| --- | --- |
| `ctest --preset dev` (host) | 185 of 185 pass; Blender, DXF, PDF and other real-consumer tests skipped as usual |
| X11, scales 1 and 2: 49 viewport-related fixtures | 98 of 98 pass |
| Placement A/B fixture, X11 and Wayland, scales 1 and 2 | 4 of 4 pass: identical picks in all 18 states |
| Wayland scale 2, eleven fixtures that failed window exposure earlier | All pass; texture viewport and sun on the third attempt, transform on the second |
| Pre-change `viewport_tests`, Wayland scale 2 | 4 of 4 pass, including one pre-merge run |
| Five benchmark families, sharing on; four with sharing off | 9 of 9 pass, including capacity, reuse and zero-GL-error checks |
| ASan/UBSan with leak detection: 11 core suites | 11 of 11 pass |
| ASan/UBSan: 12 viewport fixtures, X11 and Wayland at scales 1 and 2 | 46 of 48 pass; the placement fixture exceeds Wayland's fixed 60 s limit under ASan at both scales, and passes on X11 |
| Pre-merge: Wayland scales 1 and 2, 26 fixtures, plus fractional 1.5 and 1.75 | Scale 1 and fractional pass, except an earlier, larger placement fixture that hit the 60 s limit; scale-2 failures were window exposure, see above |
| Pre-merge: ASan/UBSan, same set | 51 of 59 pass; seven Wayland scale-2 exposure failures and one Wayland timeout |

After `origin/main` moved again (`9769325f`: R082.ee, R082.ff, R084.q), a second
merge conflicted only in this roadmap line. On that tree `ctest --preset dev`
passes 188 of 188, and 16 component and viewport fixtures pass at X11 scales 1 and 2
(32 of 32), including identical placement picks. The repeated and far benchmarks pass
with sharing on and off: one placement mesh, 89,266 B of vectors, 34,656 B of payload,
3 differing pixels per frame, and sharing-off frames equal to the pre-change frames.

No sanitizer reported an error or leak. The new
`placement_viewport_tests` fixture takes about 30 s on the host, but more than 60 s
under ASan with Wayland's fixed weston limit. It runs in the non-sanitized X11 and
Wayland 1×/2× lists of `native.yml`, not the sanitized list.

## Caveats

Hardware runs, a Release build and the 10,000-placement fixture are not covered;
whether core can now construct 10,000 placements was not tested here. Wayland scale-2
failures were window-exposure timeouts in the one-CPU container, matching R084.r's
note. Each passed on retry; the pre-change control did not fail in its runs, so the
failures are not shown to predate this change. One pre-merge X11 scale-2
`diagnostics_input_tests` failure (a dialog button state after 250 ms) passed three
reruns and every merged run. Timings come from a shared, loaded host.
