# Native and headless STL interchange

R076.d, 2026-10-07. File → Import STL chooses a source and explicit units/up axis.
The options dialog offers no welding (initial choice), exact welding or tolerance
welding; tolerance controls activate only for that choice. Degenerate-facet removal
is separately opt-in. All repairs apply solely to the imported copy and belong to
one undoable import edit. Nothing repairs the source file. Private conversion and
native validation precede the dirty-document replacement prompt. Cancel and corrupt
source leave existing work intact. Accepted imports use a fresh recovery context,
an unsaved native document and a new native Save path.

The report shows units, retained triangle counts, welding/movement/removal counts,
metadata omissions and per-body bounded topology findings. Open meshes remain
editable with explicit non-solid findings. Import never silently repairs winding
or certifies print readiness. Standard native face editing and undo are available.

File → Export STL chooses units, up axis and binary/ASCII encoding, then a new file.
The options and completion report explain that all surface geometry, including
hidden surfaces, is exported without section clipping. Export does not change
native document bytes, dirty state or history. Existing destinations reject through
the non-replacing publisher; binary precision limits follow ADR 0118.

```sh
sketchyup-cli --import-stl source.stl --stl-unit mm --stl-up z \
  --stl-weld exact --output converted.sketchyup
sketchyup-cli --export-stl new.stl --input converted.sketchyup \
  --stl-unit mm --stl-up z --stl-encoding binary
```

CLI units (`mm`, `cm`, `m`, `in`, `ft`) and up (`y`, `z`) are required. Import also
requires `--stl-weld none|exact|tolerance`; tolerance mode requires
`--stl-tolerance METRES` (1e-9..1e-3). This flag rejects for other weld modes.
`--stl-discard-degenerate` explicitly permits removal. Import without `--output`
validates and reports without publication. Export requires native `--input` and
`--stl-encoding binary|ascii`. Operations are standalone; mixed or unrelated
options reject before mutation. Structured JSON reports return on stdout.

Acceptance covers CLI isolation and unsafe destinations, native option cancellation,
repair defaults and conditional tolerance control, dirty replacement cancellation,
corrupt-source preservation, menu import/export, unit selection, source-safe Save,
native reopen and face editing/undo on Wayland/X11 at 1×/2× and under sanitizers.
Core conversion, explicit repairs, bounded diagnostics and independent Blender
producer/consumer fixtures are specified in ADRs 0116–0118.

## Implementation notes (R113)

R113 (D-102) changed the STL path. Findings and measured numbers, 2026-11-07,
in [R113](../verification/R113-format-limits.md).

- **Export** writes facets up to `materializedCornerLimit` (a triangle has at most one corner per model
  corner), with a 256 MiB file bound (was 64 MiB). At the materialized face limit (600,000 facets) the binary file
  is 30 MB and the ASCII file 183 MB, so the old bound refused a model within the limits.
- **Import** takes the unique limits: 300,000 facets and 300,000 vertices. The binary facet count and the ASCII
  parse are checked against them. The ASCII line bound is eight lines a facet at the unique limit, and the
  file bound is 128 MiB, which holds 300,000 facets with 17-digit numbers. A mesh over 100,000 facets or vertices
  continues in the next body, and the report lists it under `bodySplits`.
- **Measured:** 150,152 binary facets import as two bodies (before: refused as `STL binary facet count must be
  1..100000`).
- **Contract:** the 64 MiB and one-million-line bounds that the brief names are replaced by the unique-limit
  bounds above. A hostile file is still refused cheaply by the byte and line bounds.
