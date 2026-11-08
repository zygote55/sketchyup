# Texture pixels have a bounded, shared interpretation

R061.c, 2026-10-06. Image interpretation for the stored
[face projections](0062-face-texture-records.md), before renderer/export integration.

Managed assets remain opaque immutable bytes. Decoding never edits a record or
resolves a path. The image layer accepts the exact media types `image/png` and
`image/jpeg`, checks their signatures, and explicitly selects the corresponding
Qt reader. Other asset types remain valid document resources but return an
`unsupported` image status. Missing, invalid and oversized images have separate
statuses; no failed result contains partial pixels. Callers can retain the swatch
color and expose the reason when a texture cannot be used.

PNG accepts static grayscale, palette, RGB and RGBA images with at most eight bits
per channel. A bounded chunk scan requires a complete header/data/end envelope,
rejects animation chunks, truncated/trailing data and more than 4,096 chunks, and
checks dimensions and bit depth before invoking the pixel decoder. JPEG requires
its start/end markers. Qt performs the codec validation; this layer does not claim
to reject every recoverable JPEG defect. Both formats require positive dimensions
of at most 4,096 by 4,096, checked before decoding and checked again afterwards.
Pixels are never silently resized. Existing encoded asset limits remain 16 MiB
per asset and 64 MiB per document.

The resulting immutable image contains at most 64 MiB of tightly packed,
unpremultiplied RGBA8. Row zero is the top row. Valid embedded color profiles are
converted to sRGB; untagged pixels are assumed sRGB. Alpha is linear coverage,
including RGB values stored under zero alpha. EXIF orientation is intentionally
ignored: stored pixel axes determine UV placement. This policy is shared by the
eventual viewport and export consumers, rather than depending on an external
viewer applying metadata.

The encoder writes a PNG without color/orientation metadata from these pixels. Original
managed bytes remain available for document persistence and export provenance.
The derived PNG is an export image, not a new managed asset: its compressed size
can exceed the managed asset's 16 MiB limit. Export callers must account for it
within their output budget. This normalization fits glTF's top-left UV origin,
straight-alpha sRGB base-color data and requirement that importers ignore embedded
image color metadata. See the [glTF 2.0 texture and material specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#textures).

CPU sampling uses repeat addressing and bilinear interpolation at pixel centers.
RGB is converted to linear values before filtering; alpha is filtered directly.
Unwrapped negative or positive coordinates wrap only at sampling, preserving
continuous interpolation across repeated geometry. Nonfinite coordinates reject.
One-pixel axes work without special padding or duplicated border pixels.

Each decode owns its reader/device and can run with `QCoreApplication` on a worker
thread without a display. It never changes Qt's process-wide image allocation
limit. Header checks and a successful read are both required, consistent with the
[QImageReader contract](https://doc.qt.io/qt-6/qimagereader.html). The 64 MiB limit
describes the returned pixel buffer, not a hard process-memory cap: codec buffers,
color conversion and copies have additional bounded-image working costs. Consumer
caches and aggregate export/GPU budgets belong to their integrations.

This layer does not yet bind textures to viewport draws, author mappings through
native controls, or export UV/image arrays. Those remain R061 work; M7 delivery
also awaits the M6 gate.

## Implementation notes (R118)

R118 (2026-11-08) amends the chunk rule above. The envelope scan refuses more than `max(4,096, bytes / 1,024)` chunks: 4,096 for a file of up to 4 MiB, and one chunk per KiB of file for a larger one. The fixed count was set for 16 MiB assets. A 64 MiB asset can hold an 8192 by 8192 image, but encoders write data chunks of 8 KiB, so such a PNG has about 7,100 chunks (58.5 MB measured) and the fixed count refused it, which showed the viewport's placeholder. The other rules are unchanged: a complete envelope, no animation chunks, and no truncated or trailing data. The decoder also reads the stored bytes in place rather than copying them, and a header probe reports the stored size without decoding pixels ([ADR 0065](0065-viewport-textures.md#implementation-notes-r118)).
