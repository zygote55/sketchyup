# SketchyUp release notes

## 1.0.1

SketchyUp 1.0.1 is a patch release for 1.0.0. It fixes one defect and changes no
format, schema or API. The release check is
[R089.g](verification/R089g-1.0.1-patch.md).

### Fix

- **D-030: `annotation.create` through `transaction.apply`.** In 1.0.0, every
  `annotation.create` sent through MCP `transaction.apply`, a headless `--session` or
  a recipe step failed with `INVALID_REQUEST` "Parameter must match exactly one
  supported schema". The shared request validator ignored `properties`, `required`
  and bounds in a schema without `type`, and both `oneOf` choices of
  `annotation.create` (distance and label) are such schemas, so both matched every
  request. A schema without `type` now applies its keywords to the value it meets.
  The desktop Annotations panel and `--script` batches did not use this path and were
  not affected. Invalid annotation requests are still rejected
  ([R089.a](verification/R089a-defect-triage.md)).

### Upgrade from 1.0.0-1

Run `pacman -U sketchyup-1.0.1-1-x86_64.pkg.tar.zst`. pacman orders 1.0.1-1 after
1.0.0-1. Preferences, data and cache are kept. The native document format is
unchanged: 1.0.1 still writes schema `sketchyup-document-v26` in container version 2,
and 1.0.0 and 1.0.1 open each other's documents. The only byte difference in a saved
document is the writer string `SketchyUp/1.0.1`. To roll back, install
`sketchyup-1.0.0-1-x86_64.pkg.tar.zst` with `pacman -U`; the D-030 defect returns.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.0.1 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.0.1-1, x86_64 |
| Native document | Schema `sketchyup-document-v26` in SKUPDOC container version 2, unchanged. Saves record the writer `SketchyUp/1.0.1` |
| MCP, inspection, transactions and other public schemas | Unchanged from 1.0.0 (below) |

Everything else in the 1.0.0 notes below applies to 1.0.1.

## 1.0.0

SketchyUp 1.0.0 is the first release. Every earlier build was the 0.1.0 development
version ([ADR 0162](decisions/0162-release-versioning.md)). The release gate record,
with the release-candidate evidence, is [verification/M9.md](verification/M9.md).
The [support matrix](SUPPORT_MATRIX.md) states what is supported and what is untested.

### Versions

| Item | Version |
| --- | --- |
| SketchyUp | 1.0.0 (`sketchyup --version`, `sketchyup-cli --version`) |
| Arch package | `sketchyup` 1.0.0-1, x86_64 |
| Qt | Build requires Qt 6.8 or later. CI and the package build use Qt 6.11.2. The package runs on clean Arch machines with Qt 6.8.0 through 6.12.0 ([R087.u](verification/R087u-clean-machine-packages.md)) |
| OpenGL | 3.3 core profile |
| Native document | Schema `sketchyup-document-v26` in SKUPDOC container version 2 ([ADR 0108](decisions/0108-public-native-format.md)). Saves record the writer `SketchyUp/1.0.0` |
| MCP | Protocol version 2026-07-28 (`docs/api/mcp-2026-07-28.json`, `docs/api/mcp-native-2026-07-28.json`) |
| Inspection | v1 (`inspection-v1.json`, `inspection-session-v1.json`, `inspection-desktop-v1.json`) |
| Transactions | v1 (`transactions-v1.json`) |
| Other public schemas | `recipe-v1`, `headless-session-v1`, `extensions-v1`, `tool-reference-v1`, `native-format-v1` |
| Blender (optional) | 5.2 LTS family; the worker rejects other versions. CI installs Arch `blender` 17:5.2.2-2 (Blender 5.2.2) |
| Assistant provider | Supported: OpenAI `gpt-6-astra` through a ChatGPT-plan sign-in. Experimental: local Ollama ([ADR 0160](decisions/0160-provider-scope-for-1-0.md)) |

The schemas are installed under `/usr/share/doc/sketchyup/api`. After 1.0.0, a
major version may drop reading support for a native format or break one of these
schemas; minor and patch versions do not ([ADR 0162](decisions/0162-release-versioning.md)).

### What 1.0 does

SketchyUp is a native Linux face modeler built with Qt Widgets and OpenGL. It needs
no network, account, browser runtime or Blender for ordinary modeling.

- Draw, measure and edit faces and solids with typed measurements, inference,
  groups, components, tags, materials, textures, sections, annotations, text,
  scenes and styles.
- Save `.sketchyup` documents with undo, automatic recovery and a backup of the
  previous save. Validate and migrate older documents from the CLI.
- Exchange through GLB/glTF, OBJ/MTL, STL, a DXF subset, measured PDF/SVG,
  PNG, Formline import and one-way Blender handoff. Each conversion reports its
  losses.
- Drive the model through the MCP server, recipes, transactions and inspection
  APIs, and through the optional assistant panel.

The 59 Core rows of the [scope matrix](SCOPE.md) are the 1.0 scope. The
[user guide](USER_GUIDE.md) describes the workflows. Extended and Investigate rows
are not part of 1.0, and 1.0 does not claim full SketchUp parity.

### Install and upgrade

Install the package with `pacman -U sketchyup-1.0.0-1-x86_64.pkg.tar.zst`. pacman
orders 1.0.0-1 after the development package 0.1.0-3, so the same command upgrades
an existing 0.1.0-3 installation.

- **Preferences are kept.** The package installs only under `/usr` and does not
  touch the settings file (`~/.config/SketchyUp/SketchyUp.conf`). Package
  acceptance seeds preferences, upgrades a 0.1.0-3 package built from the release
  source to 1.0.0-1, and checks that the preferences survive. The upgrade across
  application revisions, including shortcut bindings and unknown settings, was last
  checked between development packages ([R087.u](verification/R087u-clean-machine-packages.md)).
- **Older documents open.** 1.0.0 reads document versions 1 through 26. Saving
  writes version 26. To keep the original file unchanged, migrate it to a new path:

  ```
  sketchyup-cli --validate-native old.sketchyup
  sketchyup-cli --migrate-native old.sketchyup --output migrated.sketchyup
  ```

  Migration refuses an existing destination and never converts in place.
- **Match the package to your system.** The PKGBUILD's dependencies carry no
  versions. pacman therefore installs a package built against newer Qt or glibc on
  an older system, where it fails to start ([R087.u](verification/R087u-clean-machine-packages.md)).
  Install a package built for your repository snapshot.

### Rollback

No earlier release exists, so a rollback goes to a 0.1.0 development build.

- A build that predates schema 26 (development builds of `main` before PR #284,
  merged 2026-10-10) rejects every document saved by 1.0.0 as an unsupported format
  and leaves the file unchanged. Readers never load a newer format on a best-effort
  basis ([ADR 0108](decisions/0108-public-native-format.md)).
- When 1.0.0 saves over an existing valid document, it first copies the previous
  bytes unchanged to `NAME.sketchyup.bak`. A document last saved by an older build
  therefore keeps its older format there. The next save replaces that backup, so it
  holds only one previous version. Copy any document you may need in an older build
  before saving it in 1.0.0.
- Removing the package leaves documents, preferences, data and cache in place.

### Known limitations

No known defect is release-blocking. The open-defect list is
[R089.a](verification/R089a-defect-triage.md); the release-candidate triage is in
[verification/M9.md](verification/M9.md).

- **Untested for 1.0** by owner waiver ([ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md)):
  Intel graphics campaigns, the sharing-off memory control on hardware, X11 output
  hotplug, suspend/resume, a physical 150% output, physical pointer capture, input
  methods, D-020 on Hyprland, automatic X11 output placement and an interactive Orca
  session. These are neither claimed to work nor known to fail. Wayland hotplug is
  owner-attested ([R085.r](verification/R085r-hotplug-owner-attested.md)).
- **D-004:** the Intel integrated GPU missed the 16.7 ms complete-frame target on
  100k-triangle scenes under one CPU (p95 17.3–19.6 ms) and was not remeasured.
- **D-005:** a 186–282 ms progress gap while opening on Intel/Wayland was not
  remeasured after the fix that brought AMD/X11 gaps below 50 ms.
- **Performance evidence** at the current revision is from an AMD discrete GPU only
  ([R082.ii](verification/R082ii-amd-hardware-session.md)).
- **Assistant:** Scope row A05 is met by the remote OpenAI profile only. The local
  Ollama adapter is experimental: the measured profile fails the frozen corpus
  (D-001), and it carries no quality guarantee, automatic download or cloud
  fallback. Remote model output is probabilistic, and acceptance covers one
  configuration (D-018).
- **Exchange** is conversion, not native identity. There is no native SKP or DWG
  support, and the DXF subset is not DWG compatibility (D-015). Blender handoff is
  one-way, and animation exports PNG frames without movie encoding (D-016).
- **Geometry** has finite planar and sweep limits; retained internal faces may be
  non-manifold (D-017).
- **External defects:** a Wayland 1.26 client proxy leak (D-010), a Qt Wayland
  decoration framebuffer leak (D-011), a compositor crash with RGB565 surfaces that
  the eight-bit surface policy avoids (D-012), a Qt crash with no installed fonts,
  avoided by the `ttf-dejavu` dependency (D-013), and a Weston 15.0.1 crash on a
  progress popup for a minimized window (D-020).
- **Recovery** evidence uses injected faults, not physical power loss (D-014).
- **Minor defects:** D-006 (intermittent X11 viewport assertion), D-019 (an
  unreachable Wayland re-show path), D-023 (focus-ring contrast next to the 3D
  canvas and native scroll bars), and test-infrastructure items D-007, D-008, D-009,
  D-021 and D-022.
- **Not in 1.0:** the Extended rows F01–F07 (LayOut-style documents, configurable
  components, terrain, IFC/STEP, more animation output, style and asset authoring,
  reports) and the Investigate rows C01–C07 (native SKP, Ruby extensions, Dynamic
  Components, 3D Warehouse, DWG, online maps, collaboration). See the
  [scope matrix](SCOPE.md).
