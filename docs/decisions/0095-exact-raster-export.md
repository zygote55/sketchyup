# Exact-resolution native raster export

R068.e, 2026-10-06. File → Export view as PNG renders the current camera into a
new OpenGL framebuffer at the requested physical pixel dimensions. It does not
resize a screenshot or the editor widget. Each dimension is 1–8192 pixels, total
area is at most 16,777,216 pixels, and the device's texture/renderbuffer limits
also apply. Export uses single-sample RGBA8 color and depth/stencil storage;
allocation/readback failures report an error without publishing a partial file.

The camera's vertical field of view (or orthographic vertical span), target,
position and orientation remain fixed. Requested aspect ratio changes horizontal
framing. Display scaling does not multiply output dimensions. Profile widths and
annotation text sizes are measured in output pixels. A separate paint device
renders model annotations into the same target after native geometry.

Export includes model style/background/grid/axes, visible modeled geometry,
reference images, material textures, named/free clipping, section caps, and sun
lighting/shadows. Editor selection, hover, context/lock dimming, assistant previews,
construction guides, tool previews, focus outlines, tabs and HUD are omitted.
The background is opaque. Existing viewport transparency ordering and finite
shadow-preview coverage remain applicable; this is a native view export, not a
Blender render.

A visible initialized viewport is required. Active gestures/tool sessions,
benchmark mode and camera transitions must finish before export. Pending or
unavailable image pixels reject with an actionable error rather than silently
publishing a placeholder. Rendering runs synchronously on the owning GUI/GL thread
without processing input events. It does not mutate model history, camera, tools,
selection or widget dimensions. Framebuffer bindings, viewport dimensions and
current graphics context are restored on success or failure; resolution-dependent
profile caches are invalidated for the next editor frame.

PNG publication uses QSaveFile after rendering succeeds. Invalid dimensions,
missing image pixels, rendering failures or a failed write do not replace an
existing destination. The existing read-only `view.capture` inspection endpoint
keeps its bounded screenshot semantics; it is not repurposed as raster export.
