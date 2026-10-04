# Material sides use separate visible-surface render passes

R036.c, 2026-10-04.

Each triangle carries independent front and back RGB/opacity values. A reflected
world placement swaps their raster-facing interpretation so a physical front
remains a front, matching the orientation correction used by consolidation.
Legacy colors remain the fallback for side ID zero. Face lighting and context
fading apply independently to both colors. The existing view-only body opacity
multiplies material opacity; hidden-geometry presentation caps it at 0.18.

Opaque sides render first with depth writes. Translucent sides render in the
existing back-to-front pass with depth writes disabled. When one side is opaque
and the other translucent, the triangle belongs to both batches; the shader
selects the visible side and discards fragments belonging to the other pass.
A zero-opacity side emits no face fragments. This prevents an invisible back
from interfering with the depth behavior of the visible front.

CPU ray picking and the GPU selection buffer apply the same visible-side opacity
and reflection rules. Any nonzero translucent face remains pickable; a fully
transparent side passes face selection through. Explicit topology edges remain
visible and selectable, as in the existing modeling wire presentation. A body
hidden by the view-only zero-opacity control still hides its edges too.

Body caches retain pointers only to the material records they reference. Editing
a swatch updates dependent appearance buffers without retriangulating unchanged
meshes or uploading unrelated bodies. Unused swatches and asset-byte changes do
not invalidate surface appearance. Asset bindings remain stored resources; this
change does not add UV mapping or image texture rendering.

Transparency continues to use centroid sorting. It is correct for the separated
layers in the acceptance fixtures; intersecting translucent surfaces can still
show sorting artifacts and require the later transparency renderer work.
