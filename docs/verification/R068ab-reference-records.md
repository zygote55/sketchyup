# R068.a–b — Reference image calibration and typed persistence

2026-10-06. Calibration `b8a4d5f`, typed records `5c3a055`, integrated acceptance
`1acfa65`. Parent: PR #156. Contracts: [0091](../decisions/0091-reference-image-calibration.md)
and [0092](../decisions/0092-reference-image-records.md).

The complete Debug build and **130/130 CTest suites pass** in **125.01 s**,
including actual Blender. Calibration and reference-image IO pass ASan/UBSan with
leak detection and halt-on-error in **0.32 s**. Four native Wayland 2×
integration cases pass in **18.014 s**: desktop inspection,
native MCP, render jobs and history ([results](R068ab-native-matrix.json)).

A reference image owns a managed asset, explicit dimensions/opacity and affine
placement, while remaining distinct from modeled faces. Bounds include its corners;
geometry counts/area remain empty. Two normalized pixel positions calibrate to a
known world length through parent transforms, preserving aspect and the first anchor.
Tests cover distant placement, degenerate anchors, invalid bounds, immutable inputs,
whole selection, locks, atomic Undo/Redo, stale prepared edits, recovery, groups,
component normalization/shared edits, unsupported hybrid records and missing pixels.

Schema 24 requires `reference-images-v1`. [Installed acceptance](R068ab-installed-smoke.json)
checks exact migration of an actual schema-23 sun-study file, byte-exact typed-image
round trips, embedded pixels after relocation, standalone relocated CLI operation,
missing-payload retention, invalid-record rejection without output, old-reader
rejection and unchanged dimensions when display units change. The surface-only GLB
handoff explicitly reports omitted image planes. Seven catalogs, both contracts and
the installed desktop match the acceptance checkout.

[Source-package verification](R068ab-source-package.json): **137 installed inputs**
match byte for byte, excluding build/Git artifacts. Archive SHA-256:
`8bc6ff68b045b6fc0940bfb48a83547e7434db3f729f054893483ee3e058e47d`.

Shared authoring/inspection and native display/calibration/raster export follow in
subsequent R068 layers. Remote CI and ordered merges remain delivery gates.
