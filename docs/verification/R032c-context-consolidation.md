# R032.c: context consolidation and appearance-preserving explode

Date: 2026-10-04. Merged in [PR #37](https://github.com/zygote55/sketchyup/pull/37); both CI jobs passed.
Requires merged R032.b, PR #36.

`geometry.merge_context` combines eligible raw records in the model (`context:
"0"`) or one open group. The Edit menu exposes the same operation. Explode now
removes group boundaries and consolidates promoted raw geometry in one undo item;
`group.explode` accepts `merge: false` for boundary-only automation. Native explode
uses a private hierarchy preview and an explicit eligible member list, preserving
temporary editor locks and hiding as well as persistent document policy.

Consolidation retains the destination's frame and existing IDs, allocates fresh
incoming geometry IDs, and returns typed `transfers` maps for vertices, edges,
faces, curves and guides. Coincident vertices and edges weld at the destination's
local tolerance. Faces remain separate; this is not a boolean or general face
intersection operation. Disconnected geometry may share one editing context.
Drawing across a former record seam subsequently operates on the joined topology.

Nested groups remain protected. Hidden and locked records are excluded, and an
explicit ineligible member rejects the whole transaction. Native records with
partially hidden geometry are also excluded. Empty source frames with descendants
survive so nested/locked children keep their hierarchy and world transforms.
Source records without descendants retire. Names/properties on retired source
containers are not merged; their geometry's appearance and identity transfers are
preserved. Per-context curve/guide budgets are checked before geometry allocation.

Sparse per-face RGB overrides preserve differently colored source records without
changing the surface mesh. Splits, copies, grouping and push/pull inherit colors;
whole-record paint replaces the overrides. Healing differently colored faces
rejects until they are painted alike. Color-only changes update GPU appearance
without retriangulation. This is the color foundation, not the later material and
texture system.

Document schema 7 and required container feature `face-colors-v1` persist the
assignments. Schemas 1–6 migrate with no invented overrides; a historical v6
nested-group fixture covers migration. Missing face IDs, malformed colors and
unsupported required features reject. Undo retains allocator high-water marks.

## Validation

- Development: 26/26 suites passed; sanitizer: 21/21 suites passed.
- Core tests cover adjacent colored records, subsequent cross-seam splitting,
  typed transfer maps, undo/redo, transformed/mirrored placement, retained parent
  frames, locked nested groups, hidden members, affine curves and guides.
- Native group/consolidation tests pass on isolated Weston Wayland and Xvfb X11
  at DPR 1 and 2, including colored framebuffer samples, color-only cache updates,
  temporary hiding/locks, nested explode selection and one-step undo.
- Full X11 native regression passes: selection, arrays, transforms, navigation,
  guides, constraints, inference, curves, drawing, numeric entry, tool lifecycle,
  interaction, viewport/cache, dialogs, responsive shell and application smoke.
  Rendering reports zero GL errors.
- The [colored explode recipe](../../examples/colored-explode.json) executes and
  reopens its schema-7 container successfully. The public command suite executes
  all 37 commands and rejects malformed/ineligible merge requests.
- [Native X11 capture](R032c-consolidation-x11.png): the red and blue faces at left
  belong to one consolidated record; the locked, partially hidden and grouped
  neighbors remain separate. The corresponding DPR-2 Wayland capture was inspected.

These are implementation-agent and synthetic tests. They do not claim independent
human acceptance or the deferred physical Hyprland/output-scale checkpoint.
