# R071 — Packed one-way Blender scene handoff

2026-10-07. Worker `de55a0f`, native workflow `93d53fd`, documentation `6776fc7`,
CI integration/complete acceptance `e8cc6c5`.
Parent: [PR #163](https://github.com/zygote55/sketchyup/pull/163).
Contracts: [0102](../decisions/0102-packed-blender-scene-handoff.md),
[0103](../decisions/0103-native-blender-handoff.md).

The complete Debug build and **137/137 CTest suites pass** in **184.52 s**.
Four final native Wayland 2× integration suites pass in
**20.595 s** ([matrix](R071ab-final-native-matrix.json)).
[Eight native checks](R071ab-native-sanitize-matrix.json) cover Wayland/X11, both
scales, normal and ASan/UBSan builds (`f0be6ad` / `9df131d`). Four sanitized
handoff/worker/store/queue suites pass in 15.43 s with leak detection and halt-on-error.

A captured textured mesh, camera, sun study and HDR environment are handed off to
Blender, packed, saved and relocated. The original HDR and capture are removed before
reopening. **Blender 5.2.1 and 5.2.2 both reopen the scene** with two packed images,
a perspective camera, Cycles, source revision 6 and the one-way transfer notice:
[5.2.1 evidence](R071ab-blender-521.json), [5.2.2 evidence](R071ab-blender-522.json).
The verifier also checks resolution, samples, color transform, SUN light, mesh,
relative render output and absence of executable module texts. The initial legacy
12-byte-header assumption failed actual Blender testing; the implementation verifies
the modern 17-byte header, with independent actual scene reopening as its acceptance.

Native tests save before detached launch, check the explicit `.blend` path and
`--disable-autoexec` argument, modify that external file, and verify unchanged native
model bytes and undo history. The modeless progress view cancels handoff; worker
metadata, hashes, packed/one-way flags, fallback, corrupt bytes and invalid output
paths are checked. A failed launch keeps the saved scene and explains manual opening.
The native result view states one-way transfer and the separate potentially lossy
import path. No external edit watcher or silent native overwrite is installed.

The [installed smoke](R071ab-installed-smoke.json) verifies seven catalogs, eight
contracts, exact desktop bytes and existing relocatable lighting/export behavior.
[Source package](R071ab-source-package.json): **150 installed inputs** match byte
for byte; SHA-256 `689b770dcc1fdbd12dd7897df29589d3e9f27d035514a9c89a8f63ebac21d206`.

R071 is locally complete. Real handoff reopening and sanitized worker verification
are included in CI. Remote CI and ordered merges remain delivery gates.
