# R078.d — Headless measured export

2026-10-07. CLI contract `877df28`; integration `c05a395`; packaged example fix `db7de71`.
Parent: [PR #186](https://github.com/zygote55/sketchyup/pull/186).
Contract: [0127](../decisions/0127-measured-export-cli.md).

Desktop/CLI targets build. Five final measured CLI, projection, capture, serialization
and independent Poppler checks pass **5/5 in 1.70 s**. Dedicated ASan/UBSan CLI and
serializer checks previously passed **2/2 in 4.24 s**, with leak detection and
halt-on-error. No native UI behavior changes in this slice.

The real CLI exports PDF and SVG without a display, using explicit orthographic
camera, physical page scale and technical-lines settings. Invalid platform settings
cannot break the explicit offscreen font initialization. Tests reject unknown or
mixed options, invalid/oversized settings, perspective and implicit raster conversion.
Existing output and native source bytes remain intact.

The first installed smoke found that the documented example settings were missing
from installation. The CMake install manifest now includes `measured-view.json`;
the repeated installed smoke successfully uses that installed example for both formats.
The [installed report](R078d-installed-smoke.json) also verifies eight catalogs,
thirty-two contracts and prior DXF/STL/OBJ/glTF workflows.
[Source package](R078d-source-package.json): **179 installed inputs** match byte for
byte; SHA-256 `42f04efbde667f0563cb469052b510e3df982da9b02121c19cb3320b07c5863a`.

Native dialog and raster capture follow in R078.e. Complete remote CI, ordered
merges and M7/M8 milestone acceptance remain required.
