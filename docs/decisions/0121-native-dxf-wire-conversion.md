# Native DXF wires and analytic curves

R077.b, 2026-10-07. Captured supported DXF entities become one separate unsaved
native document in a single undoable edit. Each source entity becomes one editable
wire body. No automatic face filling, intersection splitting or cross-entity welding
occurs. Native tags retain layer names and off/frozen visibility; bodies on locked
layers remain locked. Empty/unsupported-only drawings reject rather than replace
current work with an empty result. Source bytes remain unchanged.

ARC, CIRCLE and polyline bulges retain native analytic curve records bound to their
editable chord edges. The default resolution is 96 segments per full circle; callers
may select 12..256. Arc segment counts scale with sweep and round up. Reports include
maximum chord deviation in metres, so finite display/edit geometry is not described
as an exact curved surface. Exact source endpoints close/join each entity; identical
positions share a vertex within that entity. Curve centers, radii and signed sweep
remain available through native persistence and later format conversion.

Bounds include 10,000 entity bodies, 1,024 tags, 100,000 expanded vertices and wire
segments, 10,000 total analytic curves, 1,024 curves per body and the native bounded
curve-association validator. Unsupported resolution, invalid source records,
collapsed/duplicate entity segments, inconsistent curve endpoints, excessive
expansion and curve-association work reject before publishing a document. The full
native persistence validator checks every completed conversion.

Independent ezdxf-produced R12/R2018 drawings verify 6×4 metre line extents,
20-degree arc sweep, circle radius, half-circle polyline bulge, layer assignment,
hidden/locked state, and explicit wire-only closed outlines. Tests cover exact
native save/reopen, one-edit undo/redo, source preservation, caller resolution,
missing layers, inconsistent endpoints, invalid numeric data and empty conversion.
Appearance, blocks, layouts and omitted entity counts remain in the source report.
Export, File-menu/CLI workflow and final R077 acceptance remain subsequent slices.
