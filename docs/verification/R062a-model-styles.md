# R062.a — Document model styles

Verified 2026-10-06 on Arch Linux with Qt 6.11.2. The
[contract](../decisions/0068-model-styles.md) defines the document record, atomic
history behavior, bounds and schema 17 migration. Rendering and native controls
remain subsequent R062 work; this evidence does not close the M7 milestone gate.

- **108/108 development regression suites passed in 108.52 seconds**, including
  real Blender integration and all historical persistence tests.
- **11/11 targeted ASan/UBSan suites passed in 93.79 seconds**, with leak detection
  and halt-on-error enabled: model style core/I/O, document units core/I/O,
  persistence, face textures, hosted components I/O, component glue I/O, edge
  appearance, staging and site recipes.
- **4/4 native Wayland checks passed at scale 2**: texture editing, assistant
  preview, native MCP and render input. [Results](R062a-native-matrix.json).
- A fresh installed prefix migrated three retained v16 texture fixtures through
  all five modes. All **15 installed cases** preserved styles after relocation and
  produced byte-identical GLB geometry/material exports. The retained v16 CLI
  accepts each old fixture and rejects every upgraded container; its generic
  diagnostic is `Unsupported container field`. The installed contract and desktop
  binary match tested source/artifacts. [Results](R062a-installed-smoke.json).
- All **103 files referenced by `install(FILES)`** are byte-exact in the generated
  source archive; build and Git metadata are excluded.
  [Archive check](R062a-source-package.json).
- Development and sanitizer builds had no compiler warnings; whitespace checks pass.

Core tests exercise all mode names, finite/bounded validation, no-op edits,
Undo/Redo and saved state, stale before-values/revisions, immutable read snapshots,
prepared publication, amendment boundaries, compound scene metadata, shared
component edit scope and atomic invalid restoration.

I/O tests exercise every field and mode, exact container roundtrips, native
save/reopen, immutable save snapshots, verified recovery, missing/unknown/typed
fields, pre-float RGB bounds, feature/encoding/version mismatch and historical
migration. An initial fixture assertion incorrectly required explicit mappings
in every historical texture file. The corrected test compares original document
records directly: migration changes only version and style, retaining both
implicit and explicit projections. Historical fixture bytes remain unchanged.

Local logs: `build/r062a-complete-{build,ctest}.log`,
`build/r062a-sanitize-{build,ctest}.log`, `build/r062a-native-*.log`,
`build/r062a-installed-smoke.log` and `build/r062a-source-package.log`.
