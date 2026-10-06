# Section-aware surface export

R064.f, 2026-10-06. GLB/Blender export consumes the immutable snapshot's active
named section records. It does not activate a saved scene implicitly. Recall the
scene first to render its persistent activation; explicit render settings continue
to select the camera. The native render panel still rejects temporary free-clip,
opacity and benchmark overrides. Model display styles do not replace physical
exported materials or introduce a wireframe render mode.

For each visible body, export computes the same ordered model-to-body ancestor
cuts and bounded world-space section mesh as the native viewport. Hidden source
faces are omitted before contour construction. Generated positions return to the
existing local node frame; source-triangle weights interpolate each physical
side's texture coordinates and smooth normals. Side identity, native face IDs,
material pairing, texture fallback and component hierarchy keep their existing
contracts. Reflected contexts retain the intended half-space; cap normals and
local winding point outward. Existing rejection of sheared local GLB transforms
continues to apply.

Filled caps become opaque, double-sided surface primitives using the plane's
sRGB color through the existing linear PBR conversion. They have plane identity
and separate metadata, never invented native face IDs. Turning fill off removes
only that plane's cap surfaces. Open or ambiguous contours produce retained native
surfaces and report unavailable fill. Kernel budget or geometry errors reject the
export instead of silently substituting uncut geometry. An entirely clipped model
has no visible surfaces and rejects through the existing empty-export rule.

Each affected body's manifest entry contains `sections`: ordered `active` plane
IDs, `unfilled` plane IDs, `cutEdgesOmitted`, and `caps`. A cap entry carries its
`section`, material/primitive indices and first vertex/count. Native face entries
remain separate. Bounds and `visibleTriangles` describe the emitted surfaces;
`sectionCapTriangles` counts the generated portion. Export remains surface-only:
cut-edge lines are not emitted as mesh geometry or given a physical line width.
The existing losses object explicitly totals `sectionCutEdgesOmitted` for edges
whose plane requests edge display. This is independent of temporary editor style.

Export cannot change native geometry, plane records, activation, Undo state or
source image bytes. A render already captured keeps its plane state even if the
live model changes before worker processing completes.
