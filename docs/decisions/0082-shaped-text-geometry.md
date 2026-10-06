# Shaped local-font geometry

R066.a, 2026-10-06. The separate `sketchyup_text` library converts local outline
fonts to native planar faces or positive-Z extrusions. It uses the existing Qt Gui
dependency; core modeling remains Qt-free. This foundation requires a
QGuiApplication and keeps font objects in the calling thread. It does not add
persistent editable-text records, commands or authoring UI yet. A subsequent
bounded offscreen helper can provide shaping to the ordinary QCoreApplication CLI.

Text layout uses [QTextLayout](https://doc.qt.io/qt-6/qtextlayout.html#glyphRuns) for
Unicode itemization, bidirectional text, shaping and glyph positioning, then
[QRawFont](https://doc.qt.io/qt-6/qrawfont.html#pathForGlyph) for each actual font's
outlines. Direct character-to-glyph mapping is insufficient for joined scripts and
modern kerning. Baseline origin is (0,0,0), text lies on XY with Y up, and positive
extrusion depth follows +Z. Height is nominal font em height in world metres;
line spacing is explicit in em heights. Lines progress downward. Whitespace and
zero-ink shaping glyphs retain layout advance without creating faces.

Cubic outlines are flattened by bounded de Casteljau subdivision. The maximum
control-point distance from each retained chord is at most
`max(1e-6 metres, height/2048)`. Per-glyph fill rules are normalized before unioning
all outlines, preserving counters/holes, disconnected parts and joined overlaps.
Integer clipping uses a 1e-8 metre grid; resulting native geometry is subsequently
validated at the model's 1e-7 metre tolerance. No surviving collapsed contour is
silently discarded. Native extrusion retains holes. Solid volume checks account
for the independent tessellator's coordinate grid rather than requiring exact
agreement with authoritative loop area.

Input bounds: 4096 UTF-8 bytes, 128 lines, 1024 shaped glyphs, 64 resolved font
faces, height 0.001–1000 m, zero or 0.000001–1000 m depth, and 0.5–10 em line spacing.
Curve subdivision is limited to depth 20, sampled/output points to 32768, contours
to 4096, connected regions to 1024, containment depth to 64 and font-table
fingerprinting to 128 MiB. Geometry below native tolerance, invalid text, empty
output and unsupported visible bitmap/color glyphs fail explicitly.

The requested family/style must be installed unless substitution is explicitly
allowed. Reports include requested and actual family/style, substitution and
fallback flags, glyph counts and SHA-256 fingerprints of relevant shaping/outline
font tables. Missing shaped glyphs fail instead of silently producing tofu.
Fingerprints contain no private filesystem paths. Fonts remain local; this layer
does not embed, redistribute or modify font files. The existing package font
dependency supplies DejaVu Sans; tests can also exercise installed Noto Sans or
Liberation Sans and their script fallbacks. Subsequent persistence must retain
source text/settings and generated geometry, and must explicitly report unavailable
fonts when regeneration is requested.
