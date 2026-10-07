# R078.b — Measured document capture

2026-10-07. Capture contract `5056b76`; integration `381b30a`.
Parent: [PR #184](https://github.com/zygote55/sketchyup/pull/184).
Contract: [0125](../decisions/0125-measured-document-capture.md).

Desktop/CLI targets build successfully. Final measured projection/capture plus
shared-snapshot GLB, section-export and animation-capture regression checks pass
**5/5 in 0.34 s**. Dedicated capture checks previously passed **1/1 in 0.04 s**
and ASan/UBSan **1/1 in 0.19 s**, with leak detection and halt-on-error; the capture,
snapshot and fixture sources match this integration.

An immutable snapshot supplies world-space visibility, orthographic camera frame,
sections and annotations. Technical lines omit hidden geometry and soft interior
edges while retaining boundaries/silhouettes. Section contours preserve provenance,
and opaque displayed caps occlude. Associative dimensions retain world measurements,
page anchors, offsets and explicit broken-reference state. Texture, transparency and
image appearance losses are reported for the later explicit output-mode choice.

Oracles verify a 2 m edge at 1:50 as 40 mm, dimension values, page offsets, exact
section clipping, source/history preservation, foreground occlusion, hidden pieces,
transparent-face non-occlusion and suppressed clipped/hidden annotation anchors.
Input/derived work limits reject the complete drawing before excessive expansion.
PDF/SVG publication, raster capture and native/CLI workflows follow in R078.c–e.

The [installed smoke](R078b-installed-smoke.json) verifies eight catalogs,
thirty contracts and existing DXF/STL/OBJ/glTF workflows. [Source package](R078b-source-package.json):
**176 installed inputs** match byte for byte; SHA-256 `85e0fefa03119eb68cf7fc3f26a93dfc82d243e3ef942a3933c794089ab0f633`.

Complete remote CI, ordered merges and M7/M8 milestone acceptance remain required.
