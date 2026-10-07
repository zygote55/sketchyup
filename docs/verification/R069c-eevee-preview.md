# R069.c — Explicit Eevee preview and conversion comparison

2026-10-06. Implementation `98006c4`; integrated acceptance `bc2a3d2`.
Parent: [PR #160](https://github.com/zygote55/sketchyup/pull/160).
Contract: [0098](../decisions/0098-eevee-preview.md).

The complete Debug build and **133/133 CTest suites pass** in **188.82 s**,
including actual Blender Cycles/Eevee worker lifecycle, sun/HDR, changed-renderer
rejection, explicit CPU fallback and cancellation. Focused worker ASan/UBSan passes
with leak detection and halt-on-error in 5.78 s. An earlier real-worker retry hit the
host temporary-filesystem user quota; after generated scratch cleanup it passed in
14.27 s. The complete integration run above is the final acceptance.

Eight native render cases pass on Wayland/X11 at 1×/2×, normal and ASan/UBSan:
**17.777 s** normal,
**45.482 s** sanitized
([matrix](R069c-focused-native-matrix.json)). These check explicit engine choice,
fresh device proof after changing engines, verified output, approximation labels,
captured HDR settings, cancellation and continued modeling. Sanitized Wayland uses
the documented private client fix, with no sanitizer suppressions. Four final native
Wayland 2× integration cases pass in **15.648 s**
([matrix](R069c-native-matrix.json)).

The [cross-engine comparison](R069c-engine-comparison.json) covers 18 smooth-normal,
material/texture and section fixtures, with 460 compared pixel channels. Maximum
absolute difference is 0.046998 against a 0.15 tolerance. Independent Eevee oracles
cover [smooth/mirrored normals](R069c-smooth-blender-eevee-validation.json),
[textures, sidedness and alpha](R069c-texture-blender-eevee-validation.json),
[section caps](R069c-section-blender-eevee-validation.json),
[solar shadows/night](R069c-solar-blender-eevee-validation.json) and
[HDR rotation/strength](R069c-environment-blender-eevee-validation.json).
Actual [camera/import checks](R069c-camera-validation.json) cover perspective,
orthographic framing, mirrored instances and world-space bounds. Prior Cycles
lighting evidence is retained with R069.a–b.

The native setup selects the renderer reported by the active OpenGL context. Its
canonical fingerprint is rechecked before rendering; it identifies renderer/driver
configuration, not a unique physical GPU or a GPU-switching capability. The headless
host probe reported Intel Arc OpenGL/Mesa. Software-rendering CI reports its actual
renderer name. Eevee never silently falls back to Cycles. Unsupported approximations
and unused sampling seeds remain explicit transfer losses.

[Installed acceptance](R069c-installed-smoke.json) verifies explicit Eevee settings,
invalid-engine rejection, captured HDR relocation, source-file removal, unchanged
native model, standalone CLI relocation, all seven catalogs, three contracts and
the matching desktop binary. [Source package](R069c-source-package.json): **145 installed
inputs** match byte for byte; SHA-256 `34ba4bede10a0888f33717b90ab8530634bcd16c64fded7d6e5dca1ed3addcd0`.

R069 implementation and local acceptance are complete. Remote CI and ordered merges
remain delivery gates. R070 adds persistent queues and retained result management.
