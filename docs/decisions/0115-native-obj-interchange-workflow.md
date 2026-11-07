# Native and headless OBJ interchange

R075.d, 2026-10-07. File → Import OBJ/MTL opens an explicit source chooser and
unit/up-axis dialog. Units are millimetres, centimetres, metres, inches or feet;
Y-up and Z-up are supported. OBJ has no authoritative unit metadata, so the app
never guesses. Canceling either dialog preserves the current document. Conversion
and full native validation happen privately before a dirty-document replacement
prompt; malformed source therefore cannot discard current work. Accepted imports
are unsaved native documents with fresh recovery context and no source save path.
The report explains dimensions, material/resource fallbacks and omitted statements.

File → Export OBJ package uses the same explicit options, then requests a new
folder name. It exports all model geometry, including hidden objects, without
section clipping. Both options and the completion report explain that scope.
A new contained OBJ/MTL/texture/manifest package is published through ADR 0114;
existing destinations fail. Export leaves document bytes, dirty state and history
unchanged. Imported groups, faces and wires use ordinary native editing and undo;
Save selects a new native destination rather than overwriting source OBJ/MTL.

The standalone CLI modes use the same converters:

```sh
sketchyup-cli --import-obj source.obj --obj-unit mm --obj-up z \
  --output converted.sketchyup
sketchyup-cli --export-obj new-package --input converted.sketchyup \
  --obj-unit mm --obj-up z
```

Both `--obj-unit` (`mm`, `cm`, `m`, `in`, `ft`) and `--obj-up` (`y`, `z`) are
required. Import without `--output` validates and reports without writing a file.
Native output uses the verified non-replacing publisher from R073. Export requires
explicit native `--input`. Mixed import/export, unrelated options, missing units,
invalid units/axes and OBJ options outside an OBJ operation reject as structured
errors before publication. Reports are JSON; filenames do not become shell commands.

Tests exercise CLI option isolation and source/overwrite protection; native source
and option cancellation, corrupt input, dirty replacement, explicit millimetres/Z-up,
conversion notices, menu-driven package export, source-safe Save, native reopen,
face editing and undo. Platform acceptance repeats native behavior on Wayland/X11,
1×/2× and ASan/UBSan with leak detection. Parser and converter bounds, missing
assets, concavity, material/UV handling and independent Blender producer/consumer
checks remain in ADRs 0112–0114. Import does not establish a live external link and
OBJ does not preserve native hierarchy, components, annotations or parametric data.

## Implementation notes (R113)

R113 (D-102, the format count limits) changed the OBJ path. Findings and measured numbers, 2026-11-07,
in [R113](../verification/R113-format-limits.md).

- **Export** takes the materialized limits by name (`DocumentLimits::materialized`): positions, faces and
  wires. Corners, normal records and texture records are bounded by `materializedCornerLimit` (two times the
  materialized edge limit, see `src/io/format_limits.hpp`). The text bound is 256 MiB (was 64 MiB): a model at
  the materialized face limit writes 54 MB.
- **Import** takes the unique limits: 300,000 vertices, faces and wires, 900,000 edges, and corners bounded by
  `uniqueCornerLimit`. A mesh over 100,000 faces or vertices continues in the next body (`Name (2)`), and the
  report lists the split under `bodySplits`. The parser's bounds are the unique limits; the byte bound is 128 MiB
  and the physical line bound is the vertex, normal, texture, face and wire records at their limits.
- **Hot spot.** Import of 96,800 faces took 12.2 s, of which the body encoder's quadratic `QJsonObject` inserts
  were most (gdb sampling put every sample there). Sorting the keys first brought it to about 4.4 s in a later single run on a
  shared machine, and it grows about linearly now.
- **Contract:** no change. Shading does not carry across a split, which the user guide states.
