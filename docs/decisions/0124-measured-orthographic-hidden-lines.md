# Measured orthographic hidden-line geometry

R078.a, 2026-10-07. A pure geometry layer maps an orthonormal world frame to a
physical page in millimetres. At scale 1:50 a one-metre in-plane edge measures
20 mm. Width/height are 10..2,000 mm, margins must leave positive content area,
and the finite scale denominator is .001..1e9. Larger view depth is closer to the
viewer. Frame orientation is validated; perspective is not silently approximated.

World edges are clipped to the printable rectangle. Projected opaque triangles
supply barycentric half-space and linearly interpolated depth intervals. A triangle
occludes only where it is more than the native modeling tolerance in front of an
edge. Coplanar face edges remain visible. Hidden intervals are unioned before
splitting output, independently of triangle winding or overlapping occluders.
The caller may retain hidden pieces for dashed presentation. Source indices survive
clipping and splitting. View-normal collapsed edges are counted and omitted.

A 32×32 page grid bounds broad-phase work. Limits: 20,000 input edges and triangles,
one million grid entries, twenty million grid visits, five million unique candidate
checks and 100,000 output line segments. Excess work rejects; no partial drawing is
returned. Projection coordinates must remain finite and within 1e12 mm. Numerically
collapsed projected triangles (area below 1e-12 mm²) do not occlude; projected edges
shorter than 1e-9 mm collapse. Interval comparisons use bounded numeric tolerances.

Independent geometric oracles verify physical lengths, rotated frames, margins,
foreground triangle clipping, behind/coplanar geometry, optional hidden pieces,
union of duplicate occluders, winding independence, changing depth, collapsed edges,
invalid settings and dense-grid limits. This layer has no Qt, document mutation or
file IO. Document visibility/sections/annotations, PDF/SVG serialization, raster
fallback policy, native/CLI workflow and final measured-output acceptance follow
in subsequent R078 slices.
