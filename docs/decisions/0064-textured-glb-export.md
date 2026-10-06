# Textured GLB snapshots preserve independent physical sides

R061.d, 2026-10-06. Uses the [stored projections](0062-face-texture-records.md)
and [bounded image decoder](0063-texture-image-decoding.md).

GLB export binds supported managed images through standard embedded PNG images,
textures, linear/repeat samplers, `baseColorTexture` and float `TEXCOORD_0`
attributes. Original managed payloads remain embedded separately with their exact
hashes and identities. Derived PNG pixels are normalized to sRGB and straight
alpha before export. Linear swatch RGB and opacity multiply the sampled image;
any image coverage below one requests `BLEND`, including otherwise opaque swatches.
No image URI, source path or external lookup is written. These conventions follow
the [glTF 2.0 image/material contract](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#textures).

Explicit projections sample body-local triangle points. An absent side derives a
one-metre repeat from the face plane at the body origin. The dominant normal axis
has a positive sign; projected global X is the tangent unless nearly parallel,
when global Y is used. Both implicit sides share this basis. Coplanar subdivisions
keep its phase, and reversing winding alone does not mirror the image. Implicit
mapping is derived from current geometry; an explicit projection records a chosen
placement independently of later changes to the face plane.

Before packing a triangle, one common integer repeat offset is removed from all
three corners in double precision. Independently wrapping each corner would
introduce seams and change interpolation. The exporter permits at most 1/64 texel
of coordinate-conversion error on each axis and requires float spacing fine enough
to resolve that bound throughout the triangle's coordinate range. Excessive spans
reject even when their endpoints happen to be exactly representable floats. This
is a coordinate-quantization bound, not a promise about every renderer's raster
arithmetic, filtering or minification. Large constant offsets remain supported.

Different front/back projections require paired primitives even when they use the
same material and image. Each physical side receives its own correctly wound UVs
and normals. UV bytes participate in mesh identity, so equal reflected instances
share geometry while different projections cannot alias. Existing node transforms,
camera conversion, face provenance and shading normals remain intact; sheared
local-node transforms retain their explicit unsupported result.

Each referenced asset is decoded once per export. Pixels are released after its
normalized PNG is embedded. The existing 256 MiB GLB bound includes the original
and normalized images, geometry and metadata. A separate 256 MiB aggregate decoded
RGBA budget counts every exported image asset, preventing small compressed inputs
from requesting unbounded renderer image storage. Exceeding either output budget
rejects the export. Missing, unsupported, invalid or individually oversized images
retain the swatch color and original resource record, with explicit per-asset
statuses in `textureImages`. `textureAssetsPreservedWithoutUVMapping` counts these
unresolved references, and `decodedTextureBytes` records the accepted image cost.

Public output stays ordinary glTF. Textured snapshots advertise version 2 of the
private sided-material metadata; untextured snapshots keep version 1. The Blender
worker reads both. It validates paired positions, opposite normals, finite UVs,
image bindings, sampler policy and buffer bounds before removing redundant back
primitives from a private import copy. Independent back UVs become a second UV
set in the retained surface. Back images are loaded from fixed private temporary
filenames and packed into Blender before those files are deleted. A Backfacing
shader selects the matching image, linear color factor and alpha product. The
original snapshot and checksum never change, and mirrored component meshes remain
shared. This avoids ambiguous coincident surfaces in Cycles.

The private import copy, including its second UV sets, also remains capped at
256 MiB. A render that exceeds that derived-copy limit fails explicitly even if
the original interchange file itself fits.

Decoded-image accounting describes image data, not total Blender process memory:
the importer, shader copies, geometry and renderer have additional working costs.
Native viewport textures and authoring controls remain separate R061 work. This
layer does not close R061 or the M7 gate.
