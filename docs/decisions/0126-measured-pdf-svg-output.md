# Physically scaled PDF and SVG output

R078.c, 2026-10-07. The measured writer receives a complete immutable drawing and
an explicit SVG/PDF format. Technical lines remain vectors, with physical stroke
widths and optional dashed hidden edges. SVG declares millimetre width/height and
matching millimetre viewBox coordinates. PDF uses QPdfWriter at 2540 DPI and 100
device units per millimetre. PDF media bounds follow Qt's whole-point page-size
representation (at most half a point of paper-edge rounding); geometry scale stays
physical. Consumers must print at actual size, not fit-to-page.

Dimension and label text reuse the native annotation formatter and document units.
Qt QTextLayout provides Unicode shaping, fallback fonts and 360-logical-pixel line
wrapping. Glyph outlines are centred on the annotation point and scaled at 96
logical pixels per inch; all labels are portable vector paths without external
font dependencies. Missing glyphs are counted. SVG paths include readable label
titles. PDF text is outlined, so it is not selectable/searchable text. Broken
references retain red dashed leaders and crosses. Text backgrounds are opaque white.
All geometry and annotation paths are clipped to the page's content rectangle.

An explicit full-page raster input is an alternative to the line drawing. The
caller must capture it at the same camera/scale/page framing. It must match the
page aspect ratio to within one pixel, remain within 8192 pixels per side and
16 million pixels, and be nonempty. SVG embeds PNG bytes; PDF requests lossless
image encoding. Both identify rasterization, pixel dimensions and physical DPI.
SVG embeds a JSON description; PDF attaches drawing-report.json and identifies
raster output in its title. Raster capture, source framing and fallback selection
belong to the next native workflow slice, not this serializer.

The writer requires QGuiApplication for font shaping. It is independent of the
visible desktop and needs no Qt SVG dependency. Shared annotation/unit formatting
now lives in presentation headers, with existing app include forwarding retained.
Limits: one million path elements, one MiB annotation UTF-8, 1024 lines per label,
and a 64 MiB bounded output device. Publication verifies the byte hash and uses
the shared atomic non-replacing file publisher; no existing destination is replaced.

Independent XML parsing checks physical SVG dimensions and the 40 mm path for a
2 m edge at 1:50. Poppler independently renders PDF at 254 DPI to measure the page,
rectangle coordinates, standalone horizontal wire and clipped appearance raster;
it also extracts the embedded JSON report. Other tests cover Unicode labels,
vector/raster disclosure, publication integrity and invalid raster dimensions.
Native/CLI integration and final platform/source-package acceptance follow.

Primary references: [QPdfWriter](https://doc.qt.io/qt-6/qpdfwriter.html),
[QPageSize](https://doc.qt.io/qt-6/qpagesize.html),
[QTextLayout](https://doc.qt.io/qt-6/qtextlayout.html),
and [SVG 1.1 units](https://www.w3.org/TR/SVG11/coords.html#Units).
