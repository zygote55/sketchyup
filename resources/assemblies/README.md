# Configurable assemblies

These six files are the sources of the configurable assemblies that ship with SketchyUp
([ADR 0166 §6](../../docs/decisions/0166-configurable-component-interface.md#6-assemblies)):

| Source | Assembly | Parameters |
| --- | --- | --- |
| `interior-door.json` | Interior door (cuts its opening) | width, height, hinge side, leaf open |
| `casement-window.json` | Casement window (cuts its opening) | width, height, panes, frame depth, opening angle |
| `dining-table.json` | Dining table | width, depth, height, legs (square or tapered), apron |
| `bookshelf.json` | Bookshelf | width, height, depth, shelves |
| `base-cabinet.json` | Base cabinet | width, height, depth, doors, shelves, toe-kick height |
| `straight-stair.json` | Straight stair | total rise, going, width, riser count (derived), nosing |

## Licence and provenance

The sources are authored by the SketchyUp project and are MIT-licensed like the rest of the
repository (see [LICENSE](../../LICENSE)). They contain no third-party models, textures or
fonts: the geometry is built from boxes by the commands in each file, the materials are plain
colours and the thumbnails are drawn from the geometry at build time. SketchUp Dynamic
Components are not imported or converted.

## Format

Each file is one JSON object:

- `format` (`"sketchyup-assembly-source"`), `version` (1) and `license` (`"MIT"`);
- `assembly`: the library assembly identity `{id, version}` (ADR 0166 §6.2). Placing an
  assembly reuses a definition already in the model with the same identity, so bump `version`
  when a change to a source must not be merged with placements of the previous version;
- `name`, `description` and `labels`: the library metadata shown in the Components tab;
- `units`: the display units of the bundle document (`mm` or `m`); every value in the
  commands is in SI units (metres, radians);
- `commands`: one transaction batch in the public transaction format
  ([transactions](../../docs/api/transactions-v1.json), `sketchyup-cli --describe-command
  <name>`) applied to a new document. It builds the members, groups them into one component,
  defines the parameters and drivers and, for the door and the window, sets the glue face that
  cuts the opening. The IDs in the commands are the ones a new document allocates in order.

## Building

CMake builds every source into `<build>/share/sketchyup/assemblies/<name>.sketchylib` with

```sh
sketchyup-cli --build-assembly resources/assemblies/interior-door.json interior-door.sketchylib
```

and installs the bundles to `share/sketchyup/assemblies`. The output must be a new path. The
bundles are not committed: the same source and the same SketchyUp version build the same
bytes (the bundle's document identity is derived from the assembly identity and the
thumbnail draws no text). `assembly_bundle_tests` builds every source twice and compares the
bytes, then places each assembly at its defaults and at its bounds.
