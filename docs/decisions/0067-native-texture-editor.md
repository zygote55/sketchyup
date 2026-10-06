# Native texture mapping controls

R061.g, 2026-10-06. Builds on the [scoped mapping command](0066-texture-authoring-commands.md).
The Materials panel offers **Texture mapping…** and **Reset mapping** for selected,
editable faces. Each inspected side identifies its default or custom mapping.
Mapping is independent of the selected swatch and does not require an attached
image; stored PNG/JPEG resources retain the existing Attach/Replace/Resolve flow.

The editor lists the selected source face and its front/back projection explicitly.
It loads the effective projection, including an implicit default, into either
world coordinates (initial choice) or geometry-body local coordinates. World repeat
lengths account for enclosing reflection, scale and shear. Local values apply in
each target geometry body's frame, not its parent or component root.

Controls are origin X/Y/Z, repeat width/height, a relative rotation in degrees,
and unwrapped U/V repeat offsets. Lengths accept the same locale-aware document
units and explicit unit syntax as Measurements. Negative repeat sizes mirror the
corresponding axis. Tiny or very large repeat lengths use metres in scientific
notation so display formatting does not round them to zero or overflow a
feet/inches integer conversion. Untouched fields retain the original doubles;
their displayed precision is not written back.

For gradients `u` and `v`, repeat edge lengths are `|v| / |u × v|` and
`|u| / |u × v|`. Scaling a gradient by its old/new edge-length ratio changes that
repeat dimension while retaining shear. Rotation turns both gradients around the
source projection's oriented normal `u × v` at the chosen origin. It is relative
to the loaded projection, and resets to zero whenever a source/frame is loaded.
UV offsets address the image's top-left origin with positive V downward.
The kernel validates the resulting affine projection before publication.

A single projection is applied to all selected faces in one atomic command batch.
Choosing a source face's world projection therefore projects across faces and
body placements. This is also the way to align mappings across several surfaces.
Changing source, source side or coordinates reloads that saved projection and
clears draft values; the dialog explains this behavior. Apply-to Front/Back/Both
is independent of the source side. Opening and accepting a single-face editor
without edits preserves both side records exactly, including when Both is chosen.

Cancel and invalid input never publish. Errors remain inside the dialog. Apply
requires the same document snapshot, revision, edit context and selected faces.
Transient and persistent locks are checked again at publication. Opened component
members use the existing explicit shared edit scope; closed component bodies
cannot masquerade as faces. Undo/Redo restores the complete prior assignment.
Reset affects only the panel's chosen side(s), restores implicit mapping, and
skips already-implicit sides without creating empty history items.

The controls edit projection parameters numerically. Direct viewport pin dragging,
four-pin perspective warps and automatic unwrapping are not implemented. The
existing three-pin affine command remains available for explicitly specified pins.
Native rendering, transparency, resource budgets and export precision keep their
[existing contracts](0065-viewport-textures.md).
