# 0031: Authored room and instance-only window recipes

Status: accepted for the M5 recipe subset.

`assembly.room` and `assembly.window.resize` are strict entries in the shared
command registry. They expand into ordinary face, rectangle, extrusion,
push/pull, grouping, component, vertex-transform and material commands. Every
expansion runs on the batch's private document; publication is one composed edit.
The bounded `recipeOperations` report records dimensions, actual allocated IDs and
`expandedCommands`. A recipe admits at most 100 ordinary commands / 64 KiB and a
128 KiB report. It cannot call another recipe or run inside `component.edit`.

## Dimensions and authored relationships

The default room is **6 × 4 m outside**, with 200 mm walls and 2.7 m height. The
floor spans the inside footprint. Wall thickness uses outer and reversed inner
face loops, followed by extrusion. Two rectangles are drawn on the front wall
and pushed through its 200 mm thickness using existing geometry operations.

Each window is **1.2 × 1 m outside its frame**, at 900 mm sill height. Its 80 mm
members leave a 1.04 × 0.84 m clear opening. Frame depth is 100 mm, centered in the
wall. Window centers are one-quarter and three-quarters of room width. Glass is
a single face with a translucent material; this is not a glazing fabrication
model. Both placements initially share a component definition.

Flat, versioned `recipe.*` body properties bind each window to its room, wall
and opening slot. Member roles identify frame and glass. Names remain labels;
commands use explicit body IDs. Window placement metadata is instance-owned.
Material and organization edits do not invalidate the geometry contract.

## One-instance resize

A caller must explicitly supply `body`, `width` and `scope: "instance"` to
`assembly.window.resize`. Width uses the outer-frame convention. There is no
second confirmation inside the command. When the selected definition is shared,
the ordinary make-unique operation runs before editing it. Already-unique
windows retain their definition. The other opening and every unrelated body,
definition, material, asset and tag remain unchanged.

Opposite frame/glass vertex groups move by half the width change. The four
corners at each jamb of the host opening move by the same amount. A 1.2→1.4 m
resize therefore gives 1.24 m clear width while preserving center, sill, height,
depth and 80 mm member thickness. It does not scale the component.

Before editing, the engine checks reciprocal host bindings, versioned dimensions,
locks, member roles, component scope, placements and exact oriented face-loop
geometry against a freshly constructed reference. After editing it checks
the widened geometry, closed-wall volume and unrelated record preservation.
Rotating, translating or reflecting the entire room works in its local frame.
Scaling/shearing the host, moving a window independently, changing frame topology,
adding geometry to the wall or placing the host inside a shared component is
rejected. Restore the authored relationship before using this initial recipe;
there is no heuristic repair or nearby-wall guess. A failed command leaves the
live document and history unchanged, including earlier commands in that batch.

## Executable examples

`examples/room-recipe-v1.json` stages, measures, previews, commits and saves the
room. `examples/room-window-resize-recipe-v1.json` then stages and commits the
instance-only resize in a second transaction. Typed references carry IDs and
revisions between steps; they do not assume allocator values or search names.
These are version-one recipes for the existing `--recipe` runner, requiring an
explicit output and private outcome directory. The combined example creates a
new room; the shared resize command also works on an existing authored room
using the selected instance's explicit ID.

Each committed stage has its own undo entry. The runner is sequential rather
than a whole-file transaction: if a later stage fails, earlier saved commits
remain. No provider, network connection or M6 offset/boolean operation is needed.
