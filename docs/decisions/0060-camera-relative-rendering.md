# 0060: Camera-relative native rendering

Status: implemented and locally verified for R060.f.

The site fixture exposed float cancellation in the existing viewport: its native
geometry remains correct at distant coordinates, but converting world vertices
and camera targets to floats before projection changes screen positions and creates
visible triangle artifacts. The original regression shifts by about 1.7 logical
pixels when the same scene is translated to [100000.125, 200000.25, 12.5] m.

Keep the camera target and cached CPU positions in doubles. Snap a rendering origin
to the nearest 16 m grid cell around the target. Immediately before GPU upload,
subtract that origin in doubles and pack the existing eleven-float vertex format.
Build the view matrix around the relative camera target. The model, public world
coordinates, persistence and geometric calculations remain unchanged.

Projection, CPU picking rays, edge clipping, GPU selection, selection overlays,
assistant previews, transparent sorting and clipping planes share the same origin.
Public inference matrices compose their world translation in doubles; native render
camera export retains its double world target. Camera movement inside one origin
cell reuses GPU buffers. Crossing a cell invalidates position-dependent GPU caches
and uploads their retained CPU positions without rebuilding body triangulation.
Reference grids and benchmark buffers follow the same upload path.

This keeps OpenGL 3.3 and the existing GPU vertex bandwidth. CPU presentation vertices
use 56 bytes instead of 44, and upload temporarily allocates a packed 44-byte array
per batch. It improves precision near the camera; it does not promise uniform
sub-millimetre raster precision for objects spanning the entire model coordinate
range in one view. Native geometry retains its existing double precision limits.
