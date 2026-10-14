# Changelog

## 1.0.1 — unreleased

A patch release for one defect in 1.0.0. No format, schema or API change.

- Fixed D-030: `annotation.create` was always rejected with `INVALID_REQUEST`
  "Parameter must match exactly one supported schema" when sent through MCP
  `transaction.apply`, a headless `--session` or a recipe step. The shared request
  validator ignored `properties`, `required` and bounds in a schema without `type`,
  so both `annotation.create` choices (distance and label) matched every request.
  A schema without `type` now applies its keywords to the value it meets. The
  desktop Annotations panel and `--script` batches were not affected.
- Package: Arch `sketchyup` 1.0.1-1, upgrading from 1.0.0-1.

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

Versions, upgrade and rollback notes and known limitations:
[release notes](docs/RELEASE_NOTES.md). Support boundaries:
[support matrix](docs/SUPPORT_MATRIX.md).
