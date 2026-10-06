# Native texture previews and alpha-aware selection

R061.e, 2026-10-06. Builds on the [image decoder](0063-texture-image-decoding.md)
and [export mapping contract](0064-textured-glb-export.md). This layer does not
close R061; scoped authoring commands and native mapping controls remain separate.

The modeling viewport displays managed static PNG/JPEG images on independent
physical front/back sides. It uses the same body-local explicit or implicit
projections as export. Reflected placements exchange both the displayed side and
its image/UV binding. Camera-relative position packing remains independent of UV
packing; moving an instance to site coordinates does not change its texture phase.

Triangle UVs lose one common integer repeat offset before float conversion. The
same 1/64-texel conversion/spacing guard used by export applies. A projection too
large for that representation uses the material color in the native preview and
reports that fallback; it does not disable editing. Export retains its explicit
rejection policy for this condition. No native mapping or asset bytes are changed.

Images use straight alpha, top-row-first storage, repeat addressing and bilinear
filtering. GPU storage is `GL_SRGB8_ALPHA8`. The shader fetches four individual
texels and interpolates them explicitly, ensuring RGB conversion happens before
interpolation. This avoids relying on older implementations' choice of conversion
order: [OpenGL 3.3 §3.8.17](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)
allows sRGB conversion after filtering. Alpha remains linear. The sampled linear
RGB multiplies the linear material tint, then converts to sRGB before the existing
preview lighting and inactive-context dimming. Untextured swatches keep the
existing preview appearance. This is the modeling preview's lighting model, not
a claim of matching Cycles illumination. There are no mipmaps in this layer.

The opacity passes classify the product of image alpha and material opacity.
Only fully opaque visible samples write depth. Fractional coverage uses the
existing globally sorted transparent triangles; intersecting transparent surfaces
retain the existing centroid-sorting limitation. Image pairs are bound per
contiguous draw run without changing transparent triangle order. Selection and
assistant overlays keep their existing highlight colors.

The GPU ID pass samples alpha without changing encoded entity colors. CPU ray
picking evaluates the same packed triangle projection and repeat-filtered alpha.
Zero coverage passes through; nonzero fractional coverage remains pickable.
Both paths use the color fallback when the current image is unavailable or the
projection exceeds the precision guard. They retain the existing edge-selection,
clipping and active-context policies. Pixels near coverage boundaries still obey
their respective raster/ray sample positions and floating-point precision.

Decoding runs in one background task per viewport, using immutable asset records
and no widget or GL pointer. New requests replace the pending asset set. A worker
checks supersession between images and publishes only a complete current result.
Closing a viewport invalidates publication without waiting for an active codec;
the running decode can finish safely. The GUI observes publication through its
existing timer. Texture uploads and deletion occur only with the owning GL
context current. Context recreation reuploads retained decoded images.

OpenGL dispatch functions belong to the context, obtained through
[Qt's version-functions factory](https://doc.qt.io/qt-6/qopenglversionfunctionsfactory.html).
The viewport drops its non-owning pointer during cleanup and acquires the new
context's functions during initialization. This avoids retaining the previous
context's function backends across reparenting; the reparenting acceptance case
runs with leak detection enabled.

Each completed cache snapshot admits at most 128 images and 128 MiB of decoded
RGBA, in ascending asset-ID order. Images exceeding the remaining budget get a
color preview. This avoids repeated eviction/redecoding when a scene exceeds the
budget. Removing a referenced image can admit a previously deferred one. The
request list is limited by the model's existing 1,024-asset limit. Only images on
visible faces are requested. Missing, unsupported, invalid and oversized inputs
retain explicit decoder statuses; budget fallback is a separate cache status.

GPU image storage has the same 128 MiB/image-count admission bound; obsolete
textures are deleted before replacements are uploaded. Allocation failure uses
the same color-preview/picking fallback. These figures describe decoded pixel
payloads and texture storage, not process RSS. During changes the displayed
snapshot, worker source snapshot and new result may coexist, plus one decoder's
temporary storage. Immutable images reused between snapshots share storage.
Qt conversion buffers, driver allocations, source assets, geometry and document
history have their own costs. The cache exposes immutable snapshots only to its
owner and tests; retaining arbitrary historical snapshots would retain their data.

Asset identity, immutable payload identity and MIME type determine decoded-image
reuse. Renames do not decode again. Replacement bytes cannot use the previous
image even before the next repaint. Dependent appearance buffers compare decoded
images and face mappings; pixel changes and Undo/Redo do not retriangulate meshes.
Loading, unavailable images and excessive mappings are visible preview statuses.
