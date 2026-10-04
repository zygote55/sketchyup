# R030.a: scoped transforms and copies

Date: 2026-10-03. Development, all 17 ASan/UBSan suites and native preview checks pass; CI
validation is pending. R030.b native manipulation remains a separate step.
Owner/implementer: coding agent under the owner's full-roadmap authorization.

The shared `transformSelected` operation and public `geometry.transform_selection`
command accept typed contexts, faces, edges, vertices and guides. A nonsingular
column-major affine matrix acts around an explicit pivot in world or local space.
The default pivot is the origin; the default space is world. Rotation uses a
right-handed axis/angle matrix; negative scale supports reflection.

Raw geometry transforms each selected vertex once, including vertices reached by
selected faces or edges. Every incident face/edge remains attached. Fully
transformed face loops reverse under reflection to preserve their orientation.
Partial moves that make faces nonplanar or collapse edges reject the entire edit.
Whole contexts transform their local frame; selected descendants of a selected
ancestor are subsumed and inherit its transform once.

Raw copies remain in the original editing context with fresh vertex, edge, face,
curve and guide IDs. The result returns typed old-to-new maps; face/edge/vertex
lineage includes retained sources and new copies. Whole-context copies create new
contexts, duplicate the descendant hierarchy and retain subentity IDs in their
new context namespaces. Undo restores the original content in one step; subsequent
edits cannot reuse retired identities. A batch omits copy targets deleted later in
the same batch. Native temporary visibility/lock policy remains a UI concern.

Analytic curve frames follow complete affine transforms; partial edits retire
analytic provenance when they no longer describe the outline. Spaced curve copies
retain analytic metadata and remapped edge associations. Guides remain separate
from topology. Copies do not automatically weld coincident geometry or perform
booleans. Ambiguous overlapping analytic outlines remain subject to the existing
curve-binding validation. Vertex-only copies that would create unattached points
reject; use an edge/face copy or an explicit construction guide point.

Preview remains private and includes each changed body's world matrix, including
unchanged child meshes whose parent moved. The desktop preview consumes that
matrix, so a new context or moved hierarchy previews in its resulting frame.
Matrices, nested entity fields, canonical IDs, copy booleans and frame names are
validated before publication. Identity transforms produce no history entry.

Validation:

- All 22 development suites pass, including the new scoped-transform suite and
  an executable catalog fixture for every one of the 30 public commands.
- Fixtures cover pivot rotation, reflection/nonuniform scale, connected box caps,
  shared endpoints, local/world axes, ancestor/child selection, hierarchy copies,
  raw face/edge copies, analytic curves, guides, exact undo, ID floors and atomic
  rejection across multiple contexts.
- Command fixtures verify strict input validation, private preview, typed copy
  mappings and preview world frames for children moved by their parent.
- Native drawing, curves, guides, numeric amendment, tool lifecycle and navigation
  suites pass serially on Xvfb/Mesa at DPR 1 after the preview change.
- `examples/transforms.json` copies a face within its context and rotates the copy
  90 degrees around its center through the public command path.

```sh
ctest --preset dev
ctest --preset sanitize
build/dev/sketchyup-cli --script examples/transforms.json --output /tmp/transforms.sketchyup
build/dev/sketchyup-cli --preview --script examples/transforms.json
```

These are implementation-agent tests; no independent human manipulation review
is claimed. Numerical and pointer agreement will be verified in R030.b.
