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
