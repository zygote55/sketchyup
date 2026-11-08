# Native and CLI glTF import workflow

R074.c, 2026-10-07. File → Import GLB/glTF accepts `.glb` and `.gltf` through the
[bounded package reader](0109-bounded-gltf-packages.md) and
[native conversion contract](0110-native-gltf-conversion.md). Conversion completes
privately before any unsaved-document replacement prompt. Malformed or unsupported
input leaves the current document unchanged. Canceling replacement also preserves
its document bytes and history.

A successful import replaces the workspace with an unsaved native model, clears its
save path, resets recovery and selection, and fits the viewport. A native report
shows triangle/instance/camera counts, metre and axis conversion, preserved features,
and human-readable notices for every recorded loss. Imported geometry is editable
through ordinary component tools; edits undo normally. First Save chooses a native
destination. External changes do not update the model.

`sketchyup-cli --import-gltf FILE [--output NEW.sketchyup]` is a standalone operation.
Without output it prints a structured version-1 conversion report. With output,
it verifies a complete native container before atomic no-replacement publication.
Existing files, source paths and symlinks reject, including a competing writer at
publication. The parent directory must exist. Data and directory synchronization
follow the native migration policy; uncertain post-publication durability is
reported without deleting the complete copy. This shares the migration publisher,
not ordinary save/backup replacement behavior.

Images are decoded sequentially with the existing 4,096-pixel/64-MiB per-image bound
and an additional 256-MiB aggregate decoded-image budget, preventing many small
compressed inputs from requesting unbounded total decoding work. Managed payloads
remain subject to the 16-MiB individual and 64-MiB aggregate byte limits.

Exchange remains intentionally asymmetric. GLB export uses the existing immutable
render snapshot, hierarchy, mesh sharing, camera, sided appearance, embedded image
and hash-bearing manifest contracts. It publishes `scene.glb`, optional environment
bytes and `manifest.json` in a new directory; it does not export a sidecar-based
`.gltf` variant. Export permits up to 256 MiB whereas import capture permits 128 MiB;
therefore very large native exports may exceed this bounded importer. Neither path
claims native history, topology, annotations, analytical records, arbitrary PBR or
animation round-trip fidelity. Import never trusts private native extras as editable
records; its explicit loss report applies even to SketchyUp-produced GLB.

CLI tests check new publication, source/destination/symlink preservation and exclusive
mode validation. Native tests check corrupt/canceled replacement, readable notices,
new-file save/reopen, source byte preservation and component editing/undo on Wayland
and X11 at both display scales. Actual Blender interoperability and sanitizer checks
remain the conversion acceptance gates.

## Implementation notes (R113)

R113 (D-102) changed the glTF budget. Findings and measured numbers, 2026-11-07,
in [R113](../verification/R113-format-limits.md).

- **The double charge is gone.** A mesh was charged once as its definition and again for each placement, against
  one budget of 100,000. A definition's members are now unique geometry, counted once; each placement is
  materialized geometry; a node is unique and materialized. The two classes are checked against their own
  limits (`checkTotals` in `src/io/gltf_import.cpp`).
- **Measured:** one mesh of 49,928, 50,562 and 150,152 triangles imports (before: 50,562 was refused). Two meshes of
  150,000 triangles (300,000 in all) import as two definitions.
- **Bodies.** A primitive over 100,000 faces or vertices continues in the next body of its definition. A vertex
  enters a body only when a kept triangle uses it; a vertex that no kept triangle uses is reported as
  `unusedVerticesOmitted`. Before this change every position was kept, used or not.
- **A definition over 200,000 faces, or vertices, is refused by the component definition limit,** named, not by the
  import. A glTF mesh is therefore limited to 200,000 faces.
- **Not a format limit:** four placements of a 300,000-face pair (600,000 materialized faces) in one import are refused by
  the 192 MiB undo budget (`Edit exceeds the 192 MiB history budget`). The budget is R115's and out of scope here.
- **Contract:** no change to the accepted files, except the report's `bodySplits` and the unused-vertex notice.

## Implementation notes (R119)

R119 (D-108) changes the structure of a glTF import. The owner decided on 2026-11-08: a glTF mesh used by one node
imports as a plain group; only a mesh used by several nodes becomes a component. Findings and measured numbers are in
[R119](../verification/R119-gltf-meshes.md).

- **The rule.** Before it builds anything, the import counts, for each mesh, the visited nodes that use it (`countUses`,
  over the roots `run()` visits). A mesh used once is loose bodies under its node's group (`looseMesh`), with document
  ids, no definition and no instance. A mesh used by several nodes is one definition with one placement per node, as
  in R113 and R115.
- **The definition count is named before anything is built.** More than `componentDefinitionCountLimit` (4,096) meshes
  used by several nodes is refused by `checkFormatCount`: "component definitions would be N of 4,096". Nothing is
  created. `importGltf` takes the bound as a parameter, so a test can lower it.
- **Contradicts the R113 notes above.** "A definition over 200,000 faces ... A glTF mesh is therefore limited to
  200,000 faces" was already out of date: R115 raised the definition limit to 800,000 faces. Under this rule the
  definition limit governs only a mesh used by several nodes. A mesh used once is limited by the document's unique
  limits and the one-body split (R113).
- **Counts.** A loose body counts once as unique geometry and once as materialized geometry, with no placement, as any
  body outside every placement does (`madeGeometry`). A definition's members still count once as unique geometry.
- **Not kept.** A single-use mesh's own name: its node's name names the group. A definition keeps the mesh name.
- **Export round trip.** The GLB exporter writes one glTF mesh per distinct body geometry, so two loose bodies with
  identical geometry export as one mesh used by two nodes. That import is a component. The change is in the export's
  sharing, not the import, and is recorded in R119.
