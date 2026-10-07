# R068.d–e — Native reference images and exact raster export

2026-10-06. Implementation `1c5e7a0`, integrated acceptance `58b2b91`.
Parent: [PR #158](https://github.com/zygote55/sketchyup/pull/158).
Contracts: [0094](../decisions/0094-native-reference-images.md),
[0095](../decisions/0095-exact-raster-export.md).

The complete Debug build and **131/131 CTest suites pass** in **140.89 s**,
including actual Blender. The final source passes **16 native cases**: image controls
and raster export on Wayland/X11 at 1×/2×, both normal and ASan/UBSan. Normal cases take
**37.067 s**; sanitized cases take
**49.497 s**, with leak detection and
halt-on-error ([matrix](R068de-focused-native-matrix.json)). Sanitized Wayland uses the
[previously verified private client reference fix](R062b-wayland-proxy.md); no sanitizer
suppression or system-library replacement is used.

Eleven final integration cases pass on Wayland 2× in **52.080 s**:
desktop inspection, native MCP, render jobs, history, reference images, raster export,
sun, textures, styles, sections and annotation rendering ([matrix](R068de-native-matrix.json)).

The Images tray imports bounded PNG/JPEG assets atomically and edits dimensions,
opacity and anchored calibration. Placed image planes display their actual pixels
with alpha picking, section clipping, selection and affine placement while remaining
distinct from modeled surfaces. Sun lighting does not shade the image itself or turn
it into a shadow caster. Component import shares one asset across placements and
undoes in one step. Calibration defaults use placed world dimensions; accepting an
unchanged calibration does not rescale an image or add history.

The renderer tests check image orientation/colors/transparency, all four model
styles, front/back viewing, free/named clipping, window/crossing selection, hidden and
locked entities, missing assets, stale controls, calibrated world anchors, native
component import and exact relocated pixels. Modeled-face paint/extrude operations
cannot treat the display triangles as model topology.

File → Export view as PNG draws into a fresh framebuffer at the requested physical
pixel size, preserving vertical framing and rendering model annotations at output
pixel sizes. Tests compare 901×607 and 1802×1214 output, prove the larger render differs
from a resized image, check clipped image pixels and annotation scaling, and confirm
editor HUD/selection are omitted. Document/history, camera, tools, selection, widget
size and GL bindings remain unchanged. Invalid dimensions, missing pixels and failed
writes leave existing output intact. Native preview and the 1802×1214 export were
also visually inspected.

[Installed acceptance](R068de-installed-smoke.json) repeats the actual grid example,
calibration, atomic rejection, standalone CLI relocation and explicit GLB omission
checks. Seven catalogs, examples, all three workflow/native/export contracts and the
installed desktop match the acceptance checkout.

[Source-package verification](R068de-source-package.json): **142 installed inputs**
match byte for byte, excluding build/Git artifacts. Archive SHA-256:
`58bf7ee88d5da7ebba5ce7102447ce349c99d2222934a8d3f33cb3da1e7abc67`.

R068 implementation and local acceptance are complete. Remote CI and ordered merges
remain delivery gates.
