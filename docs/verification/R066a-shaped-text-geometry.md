# R066.a — Shaped local-font geometry

2026-10-06. Implementation `c4aa633`; acceptance merge `905047b` and isolated test
environment `5281376`. Parent: PR #147. Contract: [0082](../decisions/0082-shaped-text-geometry.md).

The complete Debug build and **122/122 CTest suites passed in 114.75 s**, including
actual Blender. Dedicated fixtures cover O/B counters, disconnected i components,
planar faces and closed extrusions, bounded volume agreement, accented Greek,
Arabic shaping, multiline spacing, retained whitespace advance, overlapping glyph
union, repeatable native geometry/fingerprints, missing font/style rejection and
input/output bounds. No compiler warnings were emitted.

ASan/UBSan with leak detection and halt-on-error passes the text geometry suite.
The initial run inherited the desktop GTK theme and reported allocations from
GTK/Pango/fontconfig during QGuiApplication theme initialization. The test now
explicitly selects the generic theme and compose input module alongside the
offscreen platform. The final run passes without suppressions; normal fixture
coverage also passes with that committed environment. This does not change user
preferences or the desktop application's theme.

The [installed smoke check](R066a-installed-smoke.json) verifies byte-exact desktop,
CLI and contract installation and headless CLI capabilities. This foundation is a
library; no text authoring command or UI is advertised yet.

[Source-package verification](R066a-source-package.json): **123 installed inputs**
match byte for byte; build and Git artifacts are excluded. Archive SHA-256:
`85fa90a43de365a4df3c40d429c0eacbb3b206b3c6c22aa72e9076be9c06339d`.

Editable source persistence and native authoring remain subsequent R066 work.
Remote CI and ordered dependency merges remain delivery gates.
