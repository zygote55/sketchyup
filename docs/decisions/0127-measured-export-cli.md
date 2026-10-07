# Explicit measured export CLI

R078.d, 2026-10-07. `--export-view NEW --input NATIVE --view-settings JSON`
performs a standalone measured export. The versioned settings explicitly choose
technical-lines mode, PDF/SVG format, an orthographic camera and a print scale.
Width/height/margins may use A4 defaults; all supplied fields are strictly typed
and unknown fields reject. Settings are capped at 32 KiB. Perspective and implicit
appearance conversion reject. Native raster appearance requires the viewport.

The source is loaded read-only, captured immutably and published through the
same hash-verified non-replacing writer. Existing files, source destinations and
mixed command modes fail without mutation. Standard output returns the export
report including physical units, dimensions, omissions and rasterization state.
The command does not change or save the native document.

Only this explicit font-using command initializes QGuiApplication, with the
bundled offscreen platform and generic/compose environment. Other CLI modes retain
QCoreApplication. Original command arguments are parsed independently so GUI
framework switches cannot be consumed as undeclared export options. No display,
window manager or desktop configuration change is needed. No credentials or
provider connection is involved.

Normal and sanitizer tests launch the real CLI with display variables removed
and intentionally unusable user platform settings, verify PDF and physically
scaled SVG output, and exercise source/existing-file protection, malformed and
oversized settings, missing required fields, invalid numbers, perspective,
unsupported modes and mixed command arguments. Native File-menu and raster
capture acceptance follow in R078.e.
