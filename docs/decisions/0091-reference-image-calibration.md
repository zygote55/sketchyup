# Reference image dimensions and anchored calibration

R068.a, 2026-10-06. A typed reference-image record identifies one managed asset,
physical width and height in meters, and opacity. It describes a visual plane;
it does not create modeled vertices, edges or faces. Asset resolution, document
ownership and display are integrated in subsequent layers.

Both dimensions are finite and between 1e-6 and 1e6 meters. Opacity is finite in
[0,1], including fully transparent placement. The asset identity must be nonzero;
document integration additionally checks that it exists. Pixel dimensions, DPI,
EXIF and external paths do not determine physical scale implicitly.

Normalized image coordinates use a top-left origin: (0,0) is top left, (1,1) is
bottom right. The local plane lies in XY with its lower-left corner at (0,0,0).
Coordinates outside [0,1] reject. World corners pass the document coordinate bounds.

Calibration takes two image coordinates and an explicit known length in meters.
It measures their separation through the full parent and local affine transforms,
including nonuniform scale, rotation, shear and reflection. The transformed vector
is measured directly to avoid cancellation at distant world placements. Anchors
closer than 1e-6 meters reject, as do invalid lengths and out-of-bounds results.

The ratio of known length to measured length multiplies both physical dimensions,
preserving their aspect ratio. Local translation compensates so the first anchor
remains fixed in world space. Affine axes, asset identity and opacity remain exact.
The calculation returns a separate record and placement without mutating its inputs;
its caller can publish both together through the document's atomic edit path.
