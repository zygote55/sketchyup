# Affine texture projection uses local double-precision covectors

R061.a, 2026-10-06.

The texture coordinate kernel stores an origin, two gradients and a UV offset.
For a geometry point `p`, its unwrapped coordinates are
`offset + {dot(p-origin, uGradient), dot(p-origin, vGradient)}`. Positions use
metres; gradients use image repeats per metre. UV `(0,0)` is the image's top-left
and positive V runs down the image. Renderers/exporters must retain that image
orientation explicitly. Address wrapping belongs to image sampling, after
interpolation; wrapping vertices would introduce seams across repeat boundaries.

The origin stays near the mapped geometry. It is not flattened into a large
constant offset: doing so loses small relative positions at engineering site
coordinates. All stored values and evaluations use doubles. Mapping is an
affine projection, so points displaced along its projection direction retain UV
and newly split points interpolate without a separate corner array. A single
projection can cover several faces. Faces parallel to the projection direction
collapse in UV; callers must expose that result or choose another projection.

The orthogonal constructor takes an origin, normal and tangent, signed repeat
width/height, rotation and offset. Directions are normalized after removing the
tangent's normal component. Positive rotation turns the image's U direction
toward the frame's V direction around the normal. Negative repeat dimensions
mirror individual texture axes. Repeat size magnitude is bounded to 1 micrometre
through 1 million metres. A second constructor fits three position/UV pins in
their plane, including affine shear. Four-pin perspective warps and cylindrical
or spherical unwrapping are outside this representation.

Mapping follows geometry in the same local coordinate system. An ordinary body
or instance placement samples the existing local coordinates. When an operation
actually rewrites vertex coordinates into another frame, the origin transforms
as a point and gradients transform with the inverse transpose. This preserves UV
under rotation, non-uniform scale, shear and reflection. Multiplying gradients
as ordinary tangent vectors would produce incorrect scale and shear. Physical
front/back assignment is a separate concern: winding reversal must carry the
mapping with the corresponding material side when document integration lands.

Validation rejects non-finite values, origins/points outside the existing
document coordinate bounds, gradient lengths outside `1e-9..1e9` repeats/metre,
offsets/pin UV outside ±1 billion repeats and gradient or pin directions whose
sine is below `1e-10`. Position pins must be at least the modeling tolerance
apart. Re-expressing a mapping that exceeds those bounds rejects rather than
clamping or dropping the assignment. Evaluated coordinates may exceed the
offset bound; they are unwrapped affine results, not new record offsets.

This layer is the independent numerical foundation. It does not change the
document schema, material assignment commands, image decoder, viewport or GLB
output. Those integrations must preserve the projection through face lineage,
component-coordinate changes, booleans, hosted regeneration, Undo and save/load
before R061 can be accepted. Existing asset packaging/missing-asset resolution
remains the resource ownership mechanism. M7 delivery also requires the M6 gate
to close; this foundation can be prepared while its dependency CI runs.
