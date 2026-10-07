# Measured document capture

R078.b, 2026-10-07. An immutable RenderSnapshot supplies the document, visible
body/face/edge state and orthographic camera frame. Camera target is the page
centre; the explicit print denominator controls scale independently of camera
zoom. Perspective rejects. Drawing uses infinite orthographic depth, explicitly
reported: viewport near/far planes do not silently remove printable geometry.

World-space visible topology edges and opaque face triangles feed the bounded
hidden-line engine. Hidden bodies/tags/faces/edges are omitted. Soft interior edges
are omitted while boundaries and facing-sign silhouettes remain. Explicit hidden
edges remain hidden. Transforms, including reflections, are applied before facing
and depth tests. Active scoped sections clip both edges and surfaces; displayed
section contours retain provenance and filled caps occlude. Open contours retain
existing section-engine omission reports. Face appearance is deliberately unfilled
technical line work, independent of display shading, lighting and background.

Reference images, textures and material transparency are counted and identify
raster-appearance reasons. Images are not pretended to be vectors; texture alpha
and either-side material alpha are conservatively non-opaque for line occlusion.
The following serialization/workflow slice must require an explicit technical-line
choice or render a disclosed raster appearance fallback. This layer does not
silently choose or publish either form.

Annotations reuse associative measurements and native display units. The capture
retains exact world measurements, projected anchors, offsets, text and broken
reference state. Resolved anchors on hidden geometry or beyond active section cuts
suppress the annotation, matching native visibility. Broken references retain their
fallback positions and explicit state. Text is planned at fixed physical size,
using 96 logical pixels per inch, independent of drawing scale.

Capture limits are 10,000 bodies, 100,000 source vertices/edges, 200,000 face
references, 20,000 source faces/triangles and the existing annotation limit.
Checks precede adjacency/triangulation allocations. Derived visible edges and
occluders obey the hidden-line engine's tighter limits. Existing bounded section
work limits still apply. Exceeding any bound rejects the complete drawing.

Tests verify a 2 m edge at 1:50 as 40 mm, dimension world values and page offsets,
snapshot/source/history preservation, exact section clipping and cap flags,
foreground occlusion, optional hidden pieces, transparent-face reporting and
non-occlusion, temporarily hidden edges, and clipped associative anchors.
PDF/SVG publication, raster capture and native/CLI acceptance follow.
