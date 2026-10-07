# R068.c — Shared reference image workflows

2026-10-06. Implementation `306eb46`, integrated acceptance `b52b39d`.
Parent: [PR #157](https://github.com/zygote55/sketchyup/pull/157).
Contract: [0093](../decisions/0093-reference-image-workflows.md).

The complete Debug build and **131/131 CTest suites pass** in **124.09 s**,
including actual Blender. Five ASan/UBSan suites pass with leak detection and
halt-on-error in **191.02 s**: reference image commands, general commands,
inspection, staging and transaction dispatch. Four native Wayland 2× integration
cases pass in **14.670 s**: desktop inspection, native MCP,
render jobs and history ([results](R068c-native-matrix.json)).

Create, update and anchored calibration share strict command validation, atomic
batches, staging, Undo/Redo and component edit scopes. Bounded metadata and pixel
inspection distinguish image entities from modeled surfaces. Tests reject invalid
fields, out-of-range opacity/dimensions, locks, stale edits, no-op batches and later
batch failures without publishing partial changes.

[Schema validation](R068c-schema-validation.json) checks five valid and 29 invalid
requests against each of three command catalogs, and three valid/three invalid
requests against each of three inspection catalogs. All seven installed catalogs
match the checkout.

[Installed acceptance](R068c-installed-smoke.json) recreates the saved reference-grid
example exactly, inspects its embedded 128×64 pixels without mutation, calibrates
its 8×4 m image to 16×8 m with the anchor and separate modeled block unchanged,
rejects invalid edits atomically, relocates both model and standalone CLI, and
reports the explicit reference-plane loss in surface-only GLB output. Examples,
contract and installed desktop match the acceptance checkout.

[Source-package verification](R068c-source-package.json): **140 installed inputs**
match byte for byte, excluding build/Git artifacts. Archive SHA-256:
`7fcbfb500b487b96a2151b37832e1a65dde7d92f84d38ba1c3ab7b6d0a95b863`.

Native image display/calibration controls and exact raster export follow in the
next R068 layer. Remote CI and ordered merges remain delivery gates.
