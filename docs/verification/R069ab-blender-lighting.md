# R069.a–b — Captured sun and HDR lighting in Blender

2026-10-06. Sun conversion `223b93b`, HDR capture/native setup `2cb335a`,
worker header guard `10018f0`; integrated acceptance `f7f48f4`.
Parent: [PR #159](https://github.com/zygote55/sketchyup/pull/159).
Contracts: [0096](../decisions/0096-blender-solar-conversion.md),
[0097](../decisions/0097-render-environments.md).

The complete Debug build and **133/133 CTest suites pass** in **127.87 s**,
including real Blender CPU rendering, sun/HDR snapshots, unavailable-device fallback
and cancellation. Three ASan/UBSan suites pass with leak detection and halt-on-error
in **5.56 s**: bounded HDR capture, immutable environment export and worker
verification. Sun-only worker verification separately passed sanitizers in 4.67 s.

Eight native render cases pass on Wayland/X11 at 1×/2×, normal and ASan/UBSan:
**18.911 s** normal and
**44.658 s** sanitized
([matrix](R069ab-focused-native-matrix.json)). They exercise actual native HDR setup,
chosen strength/rotation, original-file removal after capture, verified result labels,
job failure/cancel, continued modeling and unchanged model/history. Sanitized Wayland
uses the documented private client fix without suppressions. Four final Wayland 2×
integration cases pass in **14.992 s**
([matrix](R069ab-native-matrix.json)).

[Actual Cycles sun pixels](R069ab-solar-blender-validation.json) confirm analytical
east/north shadow placement, disabled shadows, ambient-only night and retained
legacy/studio behavior. [Actual HDR pixels](R069ab-environment-blender-validation.json)
confirm 180° rotation swaps colored illumination, strength dims/zeros the light and
packed pixels remain usable after original-file removal. Eight invalid metadata/file
cases reject, including forged actual dimensions before decoding. The worker uses
captured inputs and does not consult the live model or host location/time.

HDR files are bounded and packaged by exact hash; parser tests cover flat/modern RLE,
every truncation, invalid runs, radiance/dimension/settings limits, atomic publication
and relocated immutable input. Worker results must match complete lighting, settings,
source identity and transfer-loss reports before pixels publish. Standalone GLB still
reports omitted sun/environment lighting; applied Blender lighting clears those losses.

[Installed acceptance](R069ab-installed-smoke.json) exports the actual saved sun study
with exact environment metadata and bytes, removes the original image and relocates
the package, runs a standalone relocated CLI, rejects missing/truncated/invalid inputs
without output and preserves the native model. Seven catalogs, both contracts and
the installed desktop match the acceptance checkout.

[Source package](R069ab-source-package.json): **144 installed inputs** match byte for
byte, excluding build/Git artifacts. SHA-256: `d86b14374c3d81f87aedf563abb5176a1c41547ed43da54145cde81251ecf822`.

Eevee preview/capability and broader conversion comparisons are the next R069 layer.
Remote CI and ordered merges remain delivery gates.
