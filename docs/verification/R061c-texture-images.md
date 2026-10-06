# R061.c — Bounded managed image decoding

2026-10-06 UTC. Depends on R061.b's packaged assets and face projections.
[Image interpretation contract](../decisions/0063-texture-image-decoding.md).

The `texture_image` suite passes in **0.93 seconds**, and in **1.45 seconds** with
ASan, UBSan and leak detection. Synthetic fixtures establish actual pixel values,
not only successful decoding: top-left row order, RGB under zero alpha, palette
transparency, one-bit expansion, grayscale profile conversion, straight alpha,
opaque JPEG and a recognized EXIF rotation that does not change stored pixel axes.
Normalized PNG bytes decode to the same pixels without a second color transform.

Independent sampling oracles cover exact texel centers, a four-color bilinear
average, wrap boundaries, negative and million-repeat coordinates, one-pixel axes,
linear-light grayscale filtering and separately filtered alpha. Nonfinite sample
coordinates and malformed pixel buffers reject.

Missing and unsupported resources return distinct statuses. Media/signature
mismatches, truncated PNG/JPEG, damaged compressed pixels, invalid chunk lengths,
trailing PNG bytes, zero dimensions and excess chunk counts reject without partial
pixels. Animated and 16-bit PNG inputs are explicitly unsupported. PNG and JPEG
width 4,097 rejects; a real **4,096 × 4,096 RGBA image returns exactly 64 MiB**
without resizing. The initial maximum-size fixture used uncompressed PNG output
and correctly exceeded the existing encoded-asset limit; the fixture now uses
normal compression, with production limits unchanged.

Packaged native relocation preserves decodable pixels. Later asset replacement
cannot change a captured payload, and Undo restores its image. Eight concurrent
decodes agree exactly; Qt's global allocation limit is unchanged. The decoder test
also passes with `DISPLAY`, `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM` removed and
only `QCoreApplication` instantiated.

All **103/103 development CTest suites** pass in **95.71 seconds**, including real
Blender. All **6 targeted sanitizer suites** pass in **9.32 seconds**: texture
images, managed asset I/O, face projections, persistence, GLB export and Blender
job handling. The installed CLI opens and describes the existing M6 roof fixture
without display variables; the installed image contract matches source bytes.

Logs retained locally under `build/`: `r061c-image-final-{build,tests}.log`,
`r061c-image-final-sanitize-{build,tests}.log`, `r061c-full-{build,ctest}.log`,
`r061c-regression-sanitize-{build,tests}.log`, `r061c-headless-images.log`,
`r061c-install.log` and `r061c-installed-cli.json`.

This verifies image interpretation and its shared sampling/encoding primitives.
No native texture controls or textured viewport/GLB/Blender integration is claimed
yet. Those remain R061 work, and M7 delivery awaits the M6 gate.
