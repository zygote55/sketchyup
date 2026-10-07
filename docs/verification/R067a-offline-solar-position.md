# R067.a — Offline solar position

2026-10-06. Implementation `007277e`, integrated acceptance head `4c3fa8c`.
Parent: PR #152. Contract: [0087](../decisions/0087-offline-solar-position.md).

The complete Debug build and **126/126 CTest suites pass** in **116.13 s**,
including actual Blender. The solar suite passes ASan/UBSan with leak detection and
halt-on-error in **0.03 s** total CTest time. It also builds and runs without Qt.

The versioned Julian-century calculation matches **239 independent cached NOAA
spreadsheet rows** at 40° N, 105° W on 2010-06-21, across both geometric horizon
crossings. Declination, equation of time, geometric elevation and geographic
azimuth agree within **1e-7 degrees/minutes**. The fixture retains numeric source
values; tests do not recalculate the spreadsheet or access the network. The contract
links the primary source and records its downloaded archive hash.

Additional cases verify unit directions, fixed-offset UTC equivalence, a quarter-hour
offset, pre-1970 date rollover, model north rotation, both poles, dateline equivalence,
supported date boundaries and invalid calendar/nonfinite/range inputs. Refraction,
terrain and the apparent solar-disc sunrise convention are explicitly excluded.

[Installed acceptance](R067a-installed-smoke.json) checks the exact installed
contract and binaries plus a displayless editable-text model round trip.
[Source-package verification](R067a-source-package.json): **130 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`ffb4e29d877c2dd0ee388962a4421a718250f0dc499d1b6bc9d779fb5532e3c1`.

This Qt-free foundation adds no user-facing controls yet. Document/scene persistence,
shared commands and native sunlight/shadows follow in subsequent R067 layers.
Remote CI and ordered dependency merges remain delivery gates.
