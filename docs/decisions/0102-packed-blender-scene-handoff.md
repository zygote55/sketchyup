# Packed Blender scene handoff

R071.a, 2026-10-06. A separate handoff operation consumes the same immutable input
and shared scene-conversion policy as rendering. The existing bounded worker owns
its deadline, cancellation, process slot, private preferences, logs and source
verification. Handoff does not render an image and cannot publish an image result.

Blender configures the captured camera, engine/device, samples, color transform,
sun/HDR lighting and supported geometry/materials, then packs external image assets
and saves an uncompressed scene. The transfer manifest is embedded as inert JSON
text, with an explicit one-way notice. The future render destination is relative
`//render.png`; no native-model write-back or filesystem watcher is installed.

Before exposing a scene, the C++ verifier checks source identity/hash, engine/device,
lighting, transfer losses, the exact 5.2 modern file header, length and SHA-256.
Scene output is limited to 512 MiB. Blender 5.0 introduced the 17-byte header; see
[Blender's format declaration](https://github.com/blender/blender/blob/main/source/blender/blenloader_core/BLO_core_blend_header.hh)
and [5.0 core release notes](https://developer.blender.org/docs/release_notes/5.0/core/).
The supported Blender 5.2 Linux output uses `BLENDER17-01v0502`. This header/integrity
check is not an independent full .blend parser; actual Blender reopening tests verify
camera/settings and packed texture/HDR assets after removing the captured source.

Scene results and image results are distinct public types. Normal rendering and
retained render jobs continue to require verified PNGs. Native save/open integration
is a following layer; file publication must complete before launching the external
application, and an external edit must never mutate the native document.
