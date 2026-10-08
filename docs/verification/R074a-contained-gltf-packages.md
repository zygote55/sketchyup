# R074.a — Bounded immutable glTF/GLB package capture

2026-10-07. Reader `e342c05`, validation budgets `94fa3a8`, sparse-interleaved
correction `a4037e1`; final integration `d8d7652`.
Parent: [PR #168](https://github.com/zygote55/sketchyup/pull/168).
Contract: [0109](../decisions/0109-bounded-gltf-packages.md).

The complete Debug build passes **145/145 CTest suites in 165.50 s**.
Four final native Wayland 2× integration checks pass in
**18.499 s** ([matrix](R074a-native-matrix.json)).
The final focused parser regression passes in **0.05 s**; ASan/UBSan with leak
detection and halt-on-error passes in **0.72 s**. The sparse interleaved-accessor
regression rejects before cgltf's scalar accessor reader could use an incompatible
stride. The vendored library remains unmodified.

The move-only package owns captured JSON, buffers and image bytes. It bounds file,
JSON, parser allocation, aggregate buffers/images, graph depth/counts, accessor
ranges/strides/sparse indices and total validation work before native conversion.
GLB headers, lengths, alignment, chunk layout and limited BIN padding are strict.
External resources require contained relative paths; parent/absolute paths, URL
schemes, malformed escaping/base64 and canonical symlink escape reject. No network
access occurs. Parser reports retain hashes/counts without absolute source paths.
Unknown required extensions reject; KHR_texture_transform is supported.

Fixtures cover embedded and external buffers/images, canonical data URIs, GLB
padding, truncated/oversized packages, invalid UTF-8/JSON, impossible accessor
ranges, alignment, sparse ordering/duplicates, graph cycles/depth, unsupported
required extensions and unsafe paths. Capture survives later sidecar removal.

cgltf is pinned at `85cd62382dfea638278962690cf515023f33ed00` under its retained MIT
license, installed separately from SketchyUp's license. The
[installed smoke](R074a-installed-smoke.json) verifies eight catalogs, fourteen
contracts, exact desktop/library-license bytes and existing lighting exports.
[Source package](R074a-source-package.json): **160 installed inputs** match byte
for byte; SHA-256 `69aad5ec521db7afa57f750c4e5531fef220f89d93a6a10154c494fab72567ec`.

R074.a is locally complete. This reader alone does not expose native import;
geometry/material/camera conversion, CLI and native workflow follow in R074.b/c.
Required remote CI and ordered merges remain delivery gates; M7 is not yet accepted.
