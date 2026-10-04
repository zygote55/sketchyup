# R036.a: material swatches and front/back assignments

Date: 2026-10-04. Local checks passed; CI pending.
Requires the R035 work in [PR #43](https://github.com/zygote55/sketchyup/pull/43)
and [PR #44](https://github.com/zygote55/sketchyup/pull/44).

Materials are immutable document-owned RGB/opacity swatches with monotonic IDs.
Each record has default front/back IDs and sparse face overrides. Zero retains
legacy face/body color. The [record decision](../decisions/0010-material-records.md)
defines side orientation, fallback, ownership, limits and migration.

Four public commands extend the catalog to 56 entries: `material.create`,
`material.edit`, `material.delete` and `material.assign`. `materials.describe`
lists swatches; `material.sample` returns both sides without editing. Mixed
swatch/geometry batches remain atomic and undo in one step. Legacy
`material.color` explicitly clears named assignments when replacing appearance.

Face lineage carries both sides through split, extrusion, push/pull, copy,
group transfer and mirrored consolidation. Conflicting material identities
prevent a destructive boundary merge. Canonical component member assignments
remain shared; make-unique isolates subsequent assignment edits. Used materials
cannot be deleted, including uses in unplaced component definitions.

Schema 10 stores swatches and assignments for both scene and canonical records.
The container declares `materials-v1`/`json-v10` and checks allocator agreement.
A real schema-9 fixture verifies migration of existing tag/component/color data.

## Validation

- Development: 33/33 suites passed. ASan/UBSan: 27/27 suites passed.
- Native component, group and Entity info workflows plus application smoke passed
  on X11; native organization passed on isolated Weston at DPR 2.
- Core fixtures cover independent side opacity, immutable publication, rejected
  missing/used/invalid materials, split/merge lineage, extrusion, mirror/scale,
  shared scope, make-unique, locks, monotonic IDs, amendment and undo.
- Additional fixtures cover raw copying, group transfer with source-reference
  pruning, push/pull side inheritance, legacy color replacement/undo and mirrored
  consolidation of differently assigned records.
- Public-command cases validate every registered command's required/unknown
  fields, handler and undo. Material sampling checks read-only side distinction;
  an invalid assignment rolls back an earlier swatch edit in its batch.
- Persistence fixtures cover exact canonical assignment/opacity roundtrip,
  historical migration, malformed opacity/table/allocator rejection and container
  allocator mismatch.
- `examples/materials.json` saves and reopens an extruded six-face block with
  Terracotta fronts and Blue glass backs at 0.4 opacity. Schema, face assignments
  and swatch values were asserted after reopening. CI also executes this recipe.

This is implementation-agent evidence. Assets and native swatches/paint/sample
and opacity rendering remain R036.b/c; R036 is not complete.
