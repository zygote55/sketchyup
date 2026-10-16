# Changelog

## 1.1.1 — 2026-10-16

Patch release from `release/1.1.x` ([GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.1.1)).
No format, schema or API change.

- Fixed D-036: on Qt 6.8.0 and 6.8.1, sheet PDF exports (desktop and
  `sketchyup-cli --export-sheets`) and measured PDF exports had a malformed document
  information dictionary, because `QPdfWriter` closes it with `2` instead of `>>`
  (and Qt 6.8.0 writes invalid dates without a system time zone). SketchyUp now
  repairs exactly those patterns in place, before the export report's SHA-256 is
  computed; other PDFs are unchanged. The desktop Print path through `QPrinter` is not
  repaired, and on Qt 6.8.0 without a time zone the XMP dates stay empty.
- Package: Arch `sketchyup` 1.1.1-1, upgrading from 1.1.0-1.
- The same fix reached `main` in #334.

## 1.1.0 — 2026-10-15

A minor release that adds documentation sheets (scope row F01). Additive: existing
commands, queries and schemas keep their shapes.

- Documentation sheets: the Sheets workspace with pages, linked views of saved scenes at
  a declared scale and a "changed since accepted" state, lines, rectangles, text,
  labels, referenced dimensions with Reattach, title blocks and a per-sheet dimension
  format.
- PDF export of sheets with a preflight and a reconciliation report, and printing at
  100%. CLI: `sketchyup-cli --export-sheets`.
- API: new `drawing_sheet.*`, `drawing_view.*` and `drawing_item.*` transaction
  operations and drawing-sheet inspection queries, including
  `drawing_sheets.export_preflight`.
- Format: schema 27 (`drawing-sheets-v1`) is written only for documents with sheets.
  SketchyUp 1.0.x refuses those files unchanged and still opens sheet-free ones.
- Package: Arch `sketchyup` 1.1.0-1, upgrading from 1.0.1-1.

Tag `v1.1.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.1.0).

## 1.0.1 — 2026-10-14

Patch release from `release/1.0.x` ([GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.0.1)).

- Fixes D-030: `annotation.create` sent through MCP `transaction.apply`, a headless
  `--session` or a recipe step was always rejected, because the request validator ignored
  the rules of `oneOf` choices without a `type`. No format, schema or API change; the
  package upgrades from 1.0.0-1 and keeps preferences.
- The same fix reached `main` in R090.e (#314).

## 1.0.0 — 2026-10-13

The first release of SketchyUp, a native Linux face modeler. Earlier builds were
the 0.1.0 development version and Arch development packages 0.1.0-1 through 0.1.0-3.

- Native modeling: faces and solids with typed measurements, inference, groups,
  components, tags, materials, textures, sections, annotations, text, scenes and
  styles; undo, automatic recovery and a backup of the previous save.
- Native document schema 26 in container version 2. The CLI validates and migrates
  older documents to a new path.
- Exchange: GLB/glTF, OBJ/MTL, STL, a DXF subset, measured PDF/SVG, PNG, Formline
  import and one-way Blender 5.2 LTS handoff.
- Automation: MCP (protocol 2026-07-28), recipes, transactions and inspection v1.
- Assistant: OpenAI through a ChatGPT-plan sign-in is supported; local Ollama is
  experimental.
- Package: Arch `sketchyup` 1.0.0-1, upgrading from 0.1.0-3.

Tag `v1.0.0`; [GitHub release](https://github.com/zygote55/sketchyup/releases/tag/v1.0.0).

Versions, upgrade and rollback notes and known limitations:
[release notes](docs/RELEASE_NOTES.md). Support boundaries:
[support matrix](docs/SUPPORT_MATRIX.md).
