# 0117 — Native STL conversion and explicit repair

Status: Accepted implementation boundary for R076.b (2026-10-07).

Captured binary/ASCII facets convert into one new unsaved document, with one
geometry body per nonempty source solid. Import is one undoable edit. Source
files remain unchanged. Units and up axis come from the caller; STL does not
encode them. The document passes full native persistence validation before return.

Welding is explicitly selected: none retains separate corner identities, exact
joins identical coordinates, and tolerance joins the nearest existing
representative within 1e-9..1e-3 metres (lowest ID resolves ties). Welding stays
within each source solid. It does not average positions or repair winding.
Collapsed/sub-tolerance source triangles and triangles collapsed by welding fail
unless the caller explicitly selects degenerate-facet removal. Removal counts,
welded references and moved references are reported. No repair is automatic.

Conversion is bounded by 100,000 input facets, 100,000 allocated native vertices,
256 solids and the source parser's 64 MiB capture limit. Unused repair vertices
are pruned. Facet winding is retained and normals are derived from geometry.
Nonstandard binary attribute/color words are counted and omitted. Reports state
that hierarchy, materials, textures and parametric metadata are unavailable.

Each retained body receives bounded native topology diagnostics. Open boundaries,
inconsistent winding, analysis completeness and material volume remain explicit;
import does not imply print readiness. Incomplete or non-solid diagnostics do not
invent a material volume. Reports contain no absolute source paths.

Normal and ASan/UBSan fixtures cover a known 24 cubic metre closed box, unwelded
triangle soup, an open box, reversed facets, exact/tolerance welding, explicit
source/weld-induced degeneration removal, per-solid scope, units, native
persistence and undo/redo. Independent Blender-produced binary and ASCII fixtures
verify millimetre scaling, reflected placement, closed topology and volume.
