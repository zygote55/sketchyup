# Native and standalone DXF interchange

R077.d, 2026-10-07. File → Import 2D DXF chooses the source, units and curve chord
resolution. The initial unit choice reads declared drawing units; missing/unitless
or unsupported metadata requires the user to choose a scale. Conversion privately
validates the supported XY subset before the dirty-document replacement prompt.
Cancel or invalid input preserves current work. Accepted imports create an unsaved
native document with fresh recovery context and a separate native Save destination.

The report identifies drawing units, wire/curve/body counts, maximum chord deviation,
omitted entity types/reasons and conversion counts. Layers become tags, frozen/off
layers stay hidden, and locked-layer bodies stay locked. Closed outlines remain
editable edges without automatic face filling. Native edge editing and undo apply.

File → Export 2D DXF chooses explicit units and a new file. Options and reports
explain world-XY scope, hidden geometry inclusion, absence of section clipping and
face-to-contour conversion. Nonplanar geometry fails explicitly. The shared bounded
exporter and non-replacing publisher preserve current native bytes and history.

```sh
sketchyup-cli --import-dxf source.dxf --dxf-unit header --dxf-segments 96 \
  --output new.sketchyup
sketchyup-cli --export-dxf new.dxf --input new.sketchyup --dxf-unit mm
```

Both CLI modes require `--dxf-unit mm|cm|m|in|ft`; import additionally accepts
`header`. Import optionally takes `--dxf-segments 12..256`, default 96. Import
without output validates/reports only. Export requires native `--input` and rejects
`header` or import-only resolution flags. Mixed modes, unrelated options and DXF
options outside a DXF operation reject before publication. Existing output files,
source destinations and symlinks cannot be replaced. Reports are structured JSON.

Tests cover CLI isolation, required units, resolution bounds, exact analytic-curve
export, overwrite/source protection, native options/replacement cancellation,
corrupt input, menu import/export, conversion reports, source-safe Save, native
reopen and ordinary edge erase/undo. Platform acceptance covers Wayland/X11 at
1×/2× plus ASan/UBSan. ADRs 0120–0122 define exact format/version limits, independent
reference drawings and the external export consumer. No full DWG claim is made.
