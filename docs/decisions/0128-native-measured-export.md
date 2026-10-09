# Native measured drawing workflow

R078.e, 2026-10-07. File → Export measured PDF/SVG opens a keyboard-accessible
Qt form with format, vector/raster content, physical width/height/margin, explicit
1:N scale, optional hidden lines and raster DPI. It explicitly describes an
orthographic projection in the current viewing direction, centred on the view
target; even a perspective editing view remains unchanged. Technical-line content
omits surface appearance and identifies image/material losses. Raster appearance
retains the existing viewport's model style, embedded images, sections, lighting
and annotations. No automatic approximation is hidden behind a vector label.

Native capture uses the same immutable drawing/visibility policy as the CLI.
Temporary clipping/opacity/benchmark overrides, show-hidden overlays, active
interactions and camera transitions reject. Raster content is rendered through a
scoped orthographic matrix with exactly the requested physical width/height and
scale. Visible geometry/image/annotation depths determine clipping bounds instead
of the editing camera's near/far planes. The 140,000-point depth-framing budget,
72–300 DPI range, 8192-pixel sides and 16-million-pixel limit are enforced.

The existing bounded framebuffer path captures real pixels, not a resized viewport
screenshot. Annotation pixel dimensions scale with print DPI to retain their
physical sizes. Selection, tools, guides and interface overlays remain omitted.
Camera, document, selection and normal renderer state are restored on success or
failure. The same captured pixels are embedded losslessly in PDF or SVG and the
report identifies rasterization, physical DPI and output dimensions.

A new destination is required. Canceling the options or file chooser creates no
file. Existing destinations, including the native source, are not overwritten.
The completion sheet describes physical size, scale, content, relevant omissions
and the requirement to print at actual size. It avoids exposing internal hashes
or protocol fields in the normal user flow. The machine-readable export report
remains embedded in the output as specified in ADR 0126.

Acceptance exercises measured reference-image pixel extents, vector omission
reporting, annotations, source/view/selection/normal-raster preservation, rejected
overrides/settings, native options cancellation, File-menu SVG publication and
existing-file protection. Final normal/sanitized Wayland/X11 runs and independent
consumer/platform evidence are required before marking R078 complete.
