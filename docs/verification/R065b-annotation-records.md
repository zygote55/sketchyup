# R065.b — Persistent annotation records

2026-10-06, local Arch acceptance of code `a7da859` integrated with the R065.a
acceptance head as `3e5524b`. Parent: PR #144. Contract:
[0079](../decisions/0079-annotation-records.md).

- Full Debug build and **120/120 CTest suites passed in 112.97 s**, including the
  opt-in real Blender test (7.09 s). No compiler warnings. The first parallel build
  was interrupted by the linker receiving SIGKILL while linking `recovery_cli_tests`;
  no compiler diagnostic identified a code defect. Completing the remaining links
  with one job succeeded, followed by the complete clean test run.
- ASan/UBSan with leak detection and halt-on-error: annotation records, anchors,
  annotation IO, sections IO and saved-scenes IO **5/5 passed in 1.24 s**.
- Native Wayland at 2× scale: history, recovery, render and saved-scene input tests
  **4/4 passed in 16.206 s**. [Matrix](R065b-native.json).
- The [installed build](R065b-installed-smoke.json), with display variables removed,
  migrated actual schema-20 writer bytes without changing geometry, saved scenes
  or section state. It imported and round-tripped schema-21 distance/label records,
  Unicode multiline text, missing-anchor state and allocator floors. Changing
  display units preserved records; relocating the file round-tripped exactly.
  The installed schema-20 reader explicitly rejected the new file. All seven
  installed catalogs, the contract and desktop binary matched the build inputs.
- [Source-package acceptance](R065b-source-package.json): **118 installed inputs**
  matched byte for byte; no build or Git artifacts. Archive SHA-256:
  `f2fba95a43733388d7478e98940780b1077d4bb5ee0897a8d43c275a4945af56`.

The new record fixtures cover metadata-only edits, snapshot isolation, frozen
aliases, duplicate-name atomic rejection, monotonic allocation, shared-definition
rejection, exact Undo/Redo, prepared and compound snapshots, reflected/nonuniform
scale, numeric amendment, split/deletion remapping and explicit ambiguity. IO
fixtures cover strict record/anchor shapes, required features, canonical identities,
version migration and exact recovery-journal restoration after referenced geometry
is deleted. Broken dimensions have no numeric value. GLB export reports annotation
omission and the native render result panel states that dimensions/labels are omitted.

This layer supplies persistent records and evaluation. Shared commands and native
annotation authoring/drawing are subsequent R065 layers; their work is not claimed
as accepted here. Existing live-provider evidence is inherited unchanged. These
artifacts contain no credentials or private provider transcripts. Remote CI and
ordered dependency merges remain separate delivery gates.
