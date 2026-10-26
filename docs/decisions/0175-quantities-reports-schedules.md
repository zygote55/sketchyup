# Quantities, reports and schedules

Status: **Accepted (2026-10-23)**: owner decisions 1 to 13 approved as recommended on 2026-10-23. Roadmap
[R101](../PR_ROADMAP.md#r101) (M10, scope row [F07](../SCOPE.md)); requires
[R089](../PR_ROADMAP.md#r089) and [R060](../PR_ROADMAP.md#r060), both
delivered (R089 released at 1.0.0; R060 verified, all seven layers merged),
and the milestone gate rule (M10 entries require M9, then their own
dependencies, [PR_ROADMAP](../PR_ROADMAP.md#how-to-execute-this-roadmap),
item 3). F07 ships in its own gated minor release with the other M10
entries ([M10 acceptance](../M10_ACCEPTANCE.md),
[ADR 0162](0162-release-versioning.md)).

**Build line:** "Build configurable component/material counts, areas/volumes
and CSV/tabular reports from document semantics. Include units, filters and
source revision." **Verify line:** "Shared components, hidden content and
nested transforms follow explicit counting rules; geometry edits update
totals and invalid solids are not counted as known volumes." F07 asks for
"component/material counts, areas/volumes and configurable CSV/tabular
exports update from document semantics". Each section below gives the
options that were weighed and the recommendation. Facts about the code were
read in this worktree (branch `docs/r101-quantities-design`, based on
origin/main) and are cited with file paths; anything not verified in the
repository is marked **not verified**. The numbered **Owner decisions** at
the end each give a recommended default and a one-line reason, so the owner
can "approve all".

## Context

What the code already gives us:

- **Components and instances**
  ([ADR 0007](0007-component-records.md),
  `src/core/component_records.hpp`). A `ComponentDefinition` owns a root,
  member bodies keyed by canonical member ID, leaf `references` to other
  definitions and an optional glue face. A `ComponentInstance` (keyed by the
  placement's scene root) names its definition and maps every canonical
  member to a stable scene record. Resolved records are checked projections
  of the definition, never a second source of geometry.
  `ComponentSize.placements` counts "materialized component instances, the
  placement itself and nested ones"; `ComponentGeometry.stored` counts
  "every definition member counted once (unique geometry)".
  `DocumentTotals` (incrementally maintained, `src/core/model.hpp`) and
  `document.describe` report body, definition, instance, tag and material
  counts (`src/automation/inspection.cpp`).
- **Parametric placements**
  ([ADR 0165](0165-constrained-component-parameters.md)): a top-level
  placement's `overrides` drive a `ComponentVariant`; the variant's records
  are what the placement owns (`ComponentInstance.members` resolves to "the
  variant's members: the template's plus each repeat copy that exists").
  One placement is one instance, whatever its variant.
- **Materials** (`src/core/material_records.hpp`): a `MaterialRecord` (name,
  colour, opacity, asset); each face resolves `MaterialSides` (front and
  back) from its own `faceMaterials` entry or the body's default; face ID
  zero means no material. `faceMaterials(body, face)` is the resolution used
  everywhere.
- **Tags** (`src/core/tag_records.hpp/.cpp`): a `TagRecord` has `parent`
  (folder), `folder` and `visible`; `tagVisible` walks the folder chain
  (refusing past depth 32) and is false if any link is invisible.
- **Hidden state** (`src/core/body.hpp`, `src/core/selection.cpp`):
  `Body.hidden` and `TagRecord.visible` are the persistent flags;
  `Selection::hidden` walks the parent chain and reports a body hidden when
  it or an ancestor is hidden or its tag is invisible, plus the session's
  temporary hide set. `Selection` itself is "ephemeral editor state" that
  "never changes document bytes" (`src/core/selection.hpp`).
- **Nested transforms** (`src/core/model.cpp`,
  `src/core/transform.hpp`): `worldTransformIn` multiplies the ancestor
  chain (refusing a cyclic or excessively deep hierarchy past 128);
  `Transform::determinant` is public and `Transform::inverse` refuses a
  near-singular matrix.
- **Measured properties** (`src/core/entity_measure.hpp/.cpp`):
  `measureEntity` returns world/parent/local `FrameMeasures`; the header
  states "Context measures include all descendants, even hidden ones"; face
  area is the sum of `length(cross(world.vector(a), world.vector(b))) * .5`
  over the triangulated faces, and `world.volume = *solid.volume *
  std::abs(world.determinant())` (likewise for the parent and local
  frames). The desktop entity info shows area and volume in the document
  units and labels an invalid solid "Not a solid: …"
  (`src/app/entity_info_panel.cpp`); `entity.describe` reports
  `"includesHidden": true` and reports in metres regardless of the document
  units (`src/automation/entity_info.cpp`).
- **Solid validity** (`src/geometry/solid.cpp`): `inspectSolid` validates
  the closed, non-intersecting shell hierarchy with alternating cavity
  winding and returns a `SolidReport` whose `volume` is set only on success
  (`validated_shells`, promoted to `solid` for one outer shell). Failures
  return no volume and one of the status strings `empty`,
  `loose_geometry`, `open_boundary`, `non_manifold`,
  `inconsistent_winding`, `degenerate`, `self_intersection`,
  `ambiguous_containment`, `analysis_limit`, `multiple_shells`;
  `measureEntity` adds `multiple_records` and `not_a_context` for
  context-level cases.
- **Document units** (`src/core/units.hpp`,
  `src/presentation/unit_display.hpp`): the document carries `DisplayUnit`
  (`m`, `mm`, `ft-in`) and a display precision (`full`, or 0–6 for m, 0–3
  for mm/ft-in); the display layer converts lengths (×1000 for mm; feet via
  1 m = 1/0.3048 ft) and squared/cubed measures by the same factor to the
  power 2 or 3, with `full` the trimmed round-trip-safe form.
- **Document identity and revision** (`src/core/model.hpp`, `model.cpp`):
  the identity is a 32-hex-digit string created with the document;
  `revision` is a monotonically increasing `uint64` — `apply`, `undo` and
  `redo` each `++revision_` (refusing at `UINT64_MAX`), and `restore` sets
  it — so a document state never repeats a revision.
- **Current schema** (`src/io/native_format.hpp`):
  `nativeDocumentVersion = 29`; the writer emits the lowest schema the
  content needs (26 sheet-free, 27 with sheets, 28 with parameter records,
  29 with terrain or a site origin).
- **Limits** (`src/core/document_limits.hpp`): the materialized budget is
  20,000 placements, 40,000 bodies, 600,000 faces (and vertices); terrain
  triangles are bounded at 250,000 per document, counted apart.
- **IFC-imported spaces**
  ([ADR 0171](0171-ifc-exchange-subset.md) decision 8,
  `src/io/ifc_import.cpp` `tagFor`): spaces are ordinary geometry bodies on
  the tag `IFC Space`, which is invisible unless the import was asked for
  `--ifc-spaces shown`.
- **Sheets** ([ADR 0163](0163-documentation-sheets.md),
  `src/core/drawing_sheet_records.hpp`): drawing sheets, views and items
  are page records (paper in millimetres), not model geometry.
- **CLI and surfaces** (`src/automation/main.cpp`,
  `docs/TOOL_REFERENCE.md`): file-level flags (`--import-dxf`,
  `--export-sheets`, `--export-glb` …) with `--input`/new-file output, a
  `--session` JSON-lines automation, bounded queries requiring `documentId`
  and `expectedRevision`, and an MCP surface that exposes the full
  command/query set; the in-app assistant sees only a hand-pinned subset,
  and widening it needs a new corpus version
  ([ADR 0164](0164-sheet-layout-interface.md),
  [ADR 0168](0168-terrain-import-and-surfaces.md) decision 10).
  User-level storage precedent: `QSettings("SketchyUp", "SketchyUp")`
  (`src/app/window_units.cpp`).

Nothing in the product today produces a quantities report: there is no
`--report` flag, no report command or query, and no schedule (saved report
definition) record anywhere. R101 adds all of that; nothing it reuses is
modified.

## 1. Report model

A **report** is a pure function of a **report request** and the document at
a given revision; a **schedule** is a saved, named report request. The
report never mutates the document.

### Request

```
{
  "metrics": [ "counts" | "areas" | "volumes" ] or "all",
  "groupBy": "none" | "kind" | "tag" | "material" | "definition",
  "filters": {
    "visibility": "all" (default) | "visible",
    "tags": [id …]            // a tag, or a folder (all its descendant tags)
    "materials": [id …],      // applies to the areas metric only (§2.5)
    "definitions": [id …],
    "selection": true         // the session's current selection (sessions only)
  }
}
```

- `metrics` selects which of the result tables are computed and emitted
  (§6). `volumes` implies the invalid table, because the Verify line needs
  the unvalidated list.
- `groupBy` keys the rows:
  - `none` — one row per table ("All");
  - `kind` — the `ownerKind` families of `entity.describe` (`geometry`,
    `group`, `component`, `terrain`, `reference_image`), where `component`
    covers placement roots and their member records;
  - `tag` — one key per tag a record carries (folders aggregate their
    descendants), plus an untagged key;
  - `material` — one key per material (and `—` for none);
  - `definition` — one key per definition: its placement roots and its own
    member records; a reference is a member of the definition that names
    it, so a nested placement's members fall under the outer definition.
- Filter semantics are fixed:
  - `visibility` — §2.2;
  - `tags` — a tag or folder ID; a folder includes every descendant tag;
  - `materials` — restricts **areas rows only** (a record's faces of the
    named materials); `counts` and `volumes` ignore it, stated in the
    output;
  - `definitions` — the named definitions' placements and every materialized
    record those placements own, including nested reference records;
  - `selection` — the session's current selection (typed selections expand
    to their bodies); refused with `INVALID_REQUEST` outside a session,
    like the existing "selection: requires explicit editor state;
    unavailable in file-only queries" capability note
    (`src/automation/inspection.cpp`).

### Result

Four tables (all rows in deterministic order, §6.1):

1. **counts** — per group: `contexts` (materialized body records in scope),
   `instances` (materialized component placements, nested included),
   `definitions` (unique definitions with at least one in-scope placement).
2. **areas** — per (group, material): the world-space area of the in-scope
   faces, attributed to the face's **front** material
   (`faceMaterials`, `src/core/material_records.cpp`); face ID zero (no
   material) goes to `—`. The back material is not counted in v1.
3. **volumes** — per group: `volume` (known volume only, §2.4) and
   `unvalidated` (contexts with no known volume, listed in the invalid
   table with their reason).
4. **invalid** — per solid status: the count of contexts that are not known
   volumes.

Two information tables: a **terrain** table (per terrain: contexts,
triangles, area; no volume column, §2.5) and, in the metadata, the
sheet/view/item counts (§2.5).

## 2. Counting rules (the Verify line)

### 2.1 Shared components and nested instances

Each counted path, stated explicitly:

- **A context** (a materialized body record) counts once per group it
  belongs to. Unique-geometry bookkeeping (`DocumentTotals`,
  `uniqueGeometryCounts`) never feeds the report; reports count the
  **materialized** document.
- **A definition** counts once per report (the `definitions` metric),
  whatever its number of placements; a shared definition is not multiplied
  by its instance count.
- **An instance** (placement) counts once per materialized placement: every
  top-level `ComponentInstance` and every **nested reference** — one per
  reference edge per outer placement (a `ComponentDefinition.references`
  entry materializes a placement of the referenced definition for each
  placement of the referencing definition; the definition graph is
  validated acyclic). So a `Table` with four `Post` references and two
  placements yields 2 `Table` + 8 `Post` placements.
- **A parametric placement** (with `overrides`, ADR 0165) is one instance;
  its variant's records count with it.
- **A hosted (glued) cutter** is an instance of its definition like any
  other; its host counts under the host's own groups. `Make unique`
  (`component.edit_instance`,
  [ADR 0042](0042-instance-only-component-edits.md)) makes a placement
  ordinary geometry: it leaves the `instances` metric and stays in
  `contexts`.

### 2.2 Hidden content and hidden tags

- **Included by default.** With `visibility: "all"`, every record counts,
  hidden or not, on visible or hidden tags. This matches the existing
  semantics: `measureEntity`'s context measures "include all descendants,
  even hidden ones" and `entity.describe` reports `"includesHidden": true`.
- **Excluded under `visibility: "visible"`**: a record counts only when it
  and every ancestor are not `hidden` and, for the record and each
  ancestor, `tagVisible(tag)` is true — the persistent half of
  `Selection::hidden` (`src/core/selection.cpp`). The session's temporary
  hide set is *not* part of the rule, so a report is reproducible from the
  file alone.
- The applied mode is stated in the output metadata
  (`#filters.visibility=…`). "Hidden content" in the Verify line therefore
  means: hidden groups, hidden bodies and content under hidden tags
  (including the IFC `IFC Space` tag, hidden by default) are **included by
  default and excluded only under `visible`**.

### 2.3 Nested and mirrored transforms

- **Area** is the sum, over the in-scope faces, of the world-space
  triangle areas — exactly `Document::worldArea` / `measureEntity`: each
  triangle's vertices go through the full `worldTransform`, and the area is
  the length of the cross product of the transformed edge vectors. A scale
  multiplies area by the product of its axis factors, and a **mirror
  (negative determinant) changes nothing** (the magnitude is
  mirror-invariant).
- **Volume** is the validated local solid volume (material summed over all
  roots/islands with alternating cavity winding, `analyzeSolidShells`)
  times **`abs(worldTransform().determinant())`** — the existing
  `measureEntity` rule, verbatim. The sign of the determinant is never
  used: a mirrored placement reports the same volume as an unmirrored one,
  and a 2× scale multiplies volume by 8. No sign-based cancellation
  (subtraction of mirrored volumes) exists or is introduced.
- World bounds and the ±1e6 m coordinate limit
  (`src/geometry/surface.hpp`) apply as elsewhere; a placement whose world
  transform exceeds the limit cannot exist in a valid document, so the
  report sees no such record.

### 2.4 Invalid and non-manifold solids

- **Never counted as known volumes.** A context contributes to the volume
  total only when `inspectSolid` (on the record's own surface and topology,
  in its local frame) returns `solid`/`validated_shells` with a volume.
- **Listed separately with a reason.** Every such context appears in the
  `invalid` table under its exact status string (the eleven above), and
  the `unvalidated` counter in `volumes` equals their sum.
  `analysis_limit` (the check did not complete within its budget) is also
  *not* a known volume — conservative, because no volume was validated.
- An invalid solid's **area still counts** (area needs no closed shell);
  its volume is simply absent from every total.
- Multi-record contexts report `multiple_records` and contexts without
  geometry `empty`.

### 2.5 Terrain, IFC-imported spaces and sheets

- **Terrain** (`src/core/terrain_records.hpp`,
  [ADR 0168](0168-terrain-import-and-surfaces.md)): a
  `BodyKind::Terrain` container plus its tile records (triangles only,
  identity transform, `src/core/body.hpp`). Counted: the container once in
  `contexts`, the tile records once each. **Area** is the sum of the
  tiles' world-space triangle areas. **No volume**: terrain is not a solid,
  so it is excluded from the volume total and its triangles never enter
  the invalid tables (they are not solid candidates); cut and fill volumes
  stay out of scope, as in
  [ADR 0169](0169-terrain-editing-drape-stamp.md) §10. The terrain table
  reports per terrain: contexts, triangles, area.
- **IFC-imported spaces** are ordinary bodies on the (by default hidden)
  `IFC Space` tag; they follow the generic rules — included by default,
  and dropped from a `visible` report with their tag, which is the
  intended way to keep the space volumes out of a takeoff (they "double
  the faces and hide the building if shown", ADR 0171 §3.1). Their own
  volumes appear when their shells close and validate; otherwise they are
  listed under their status, like any other solid.
- **Sheets, views and items** are never counted in any metric; they appear
  only in the metadata (`#sheets=… #views=… #items=…`) as an information
  block.

## 3. Units and precision

- Values are computed in metres (the internal unit, per
  `docs/TOOL_REFERENCE.md` "Lengths use metres") and converted to the
  **document's display units** (`Document::displayUnits`), the same way the
  entity info panel does (`displayMeasure`,
  `src/presentation/unit_display.hpp`).
- In tabular (CSV/JSON) output the unit codes are the **base codes**:
  `m` / `mm` / `ft` for length, `m2` / `mm2` / `ft2` for area,
  `m3` / `mm3` / `ft3` for volume. A `ft-in` document reports in feet
  (feet, square feet, cubic feet); the compound feet-inches form is
  display-only and never appears in a tabular cell. The unit codes are
  stated in the output header (`#units.length`, `#units.area`,
  `#units.volume`), so a report is self-describing without the model.
- **Precision** defaults to the document's `displayPrecision`: `full` is
  the trimmed, round-trip-safe form (at most 8 decimals, trailing zeros
  dropped, `-0` normalized to `0`, mirroring `displayDecimal`); an explicit
  `0…max` gives a fixed number of decimals. Counts are always bare
  integers. The applied precision is stated in the metadata
  (`#precision=…`).
- Conversion factors: mm ×1000; ft via 1/0.3048 (≈3.2808398950131 ft/m,
  10.7639104167 ft²/m², 35.3146667214 ft³/m³). No rounding happens before
  the final formatting; sums are accumulated in metres and converted once.

## 4. Source revision stamp

Every report carries, in its metadata, the **document identity** (the
32-hex `Document::identity()`) and the **revision**
(`Document::revision()`), and a schedule re-run re-stamps both. Rationale:
the pair identifies the model state the totals came from — the identity is
stable for the document's life, and the revision is injective over a
document's states (each `apply`, `undo` and `redo` takes a new revision;
`restore` sets an explicit one) — so a total in an exported file can be
traced to the model state that produced it, and two different states never
share a stamp. The identity also guards the stored schedules (§7).

A content digest (a hash of the canonical native encoding) is **not** in
v1: it would force a full serialization per run on large models, and the
identity+revision pair already distinguishes states for one document. It is
a reconsideration trigger (§14) if consumers need a model-independent
digest.

## 5. Live update and caching

- The engine is **stateless per run**: a report is computed from the
  document, on demand, in one pass. There is **no cross-run result cache
  in v1** and no report state is persisted with the document.
- **Live update** is the UI re-running its active schedule when the
  document revision changes (the same pattern as the entity info panel
  re-running `measureEntity` on change, `src/app/entity_info_panel.cpp`).
  Because the revision increments on every `apply`, `undo` and `redo`, a
  "computed at revision N" label is always exact: a newer revision implies
  stale.
- Long runs (large documents, §12) run **asynchronously with progress and
  cancel** (the existing worker patterns: `src/app/inference_worker.cpp`,
  the terrain preview worker of
  [ADR 0169](0169-terrain-editing-drape-stamp.md) §4.2); cancel returns
  control to the UI in ≤ 100 ms.
- A per-run memo (world transform per body, solid analysis per body) is
  allowed within a single run; nothing survives the run.

## 6. Outputs

### 6.1 CSV (the canonical tabular format)

Exact dialect, v1:

- **Encoding:** UTF-8, **no BOM**; lines terminated by **LF** (0x0A), no
  CRLF, no trailing whitespace.
- **Delimiter:** comma (0x2C); no alternative delimiter.
- **Header metadata block** (the "metadata header" option of
  "metadata header or sidecar" chosen; no sidecar file in v1): the first
  lines are `#key=value` lines in this fixed order — `#sketchyup-report=1`
  (the marker), `#format=1.0`, `#documentId=…`, `#revision=…`,
  `#schedule=…` (or `#ad-hoc=1`), `#units.length`, `#units.area`,
  `#units.volume`, `#precision=…`, `#metrics=…`, `#group_by=…`, then the
  applied filters (`#filters.visibility=…` always;
  `#filters.tags` / `#filters.materials` / `#filters.definitions` as set,
  IDs semicolon-joined; `#filters.selection=1` when applied); then
  `#sheets=…`, `#views=…`, `#items=…`. No other `#` lines.
- **Column header row:** exactly
  `section,group,tag,material,definition,metric,value` — a single tidy
  (long) table for all the tables: one row per (section, group, metric[,
  material]).
- **Rows and order** (deterministic; two runs over the same revision are
  byte-identical): `counts` rows (group order: `All`, then keys in ID
  order for ID groups, byte order for names), then `areas` (group order,
  then material ID order, `—` first), then `volumes`, then `invalid`
  (fixed status order: `empty`, `loose_geometry`, `open_boundary`,
  `non_manifold`, `inconsistent_winding`, `degenerate`,
  `self_intersection`, `ambiguous_containment`, `multiple_shells`,
  `analysis_limit`), then `terrain` (terrain ID order).
- **Quoting:** minimal RFC 4180 — a field is quoted only if it contains a
  comma, a double quote, a CR or an LF; embedded double quotes are
  doubled. Numbers are never quoted.
- **Numbers:** plain decimal, never scientific, no thousands separator;
  counts are integers; measures at the report precision (§3).
- **Refusal:** writing to an existing path refuses (the product's "never
  replace an existing path" convention, `src/io/native_format.hpp`).

### 6.2 JSON (the second format)

`--format json` emits the same metadata and tables as a JSON object
(`{ "metadata": {…}, "counts": […], "areas": […], "volumes": […],
"invalid": […], "terrain": […] }`), with measure values as the **same
decimal strings** the CSV would write, so the two formats always agree.
JSON is secondary: it is the shape the session and MCP surfaces return
natively; CSV is the file format of record. No HTML/PDF report is added
(sheets are the document's own visual surface, ADR 0163).

## 7. Saved report definitions (schedules)

- **Options weighed:**
  - (a) **Document-embedded**: a `reports-v1` record table, schema **30**,
    required feature, the writer emitting the lowest schema the content
    needs. Travels with the file; costs a schema bump, a decode path,
    fuzz seeds and a migration entry for a read-only convenience feature.
  - (b) **User-level store (recommended)**: a JSON file in the user data
    directory (the `QSettings("SketchyUp", "SketchyUp")` family,
    `src/app/window_units.cpp`), mapping **document identity →
    schedules**, each a named request (§1) with a creation timestamp.
    Schedules are keyed to the document whose tag/material/definition IDs
    they reference, so a schedule for one document is never offered for
    another (refused with a located error).
- **Recommendation: (b)** for R101. Schedules are tool state, not model
  state; the document file stays byte-identical with or without them
  (checked: a save with a schedule present is bit-identical to one
  without), and the document keeps its current schema — **29**
  (`src/io/native_format.hpp`) — following
  [ADR 0169](0169-terrain-editing-drape-stamp.md) decision 8's "no
  schema 30" pattern. Document-embedding (option a) is a reconsideration
  trigger (§14).

## 8. CLI

```
sketchyup-cli --input <model> --report <file.csv|file.json>
    [--schedule <name>]            # run a saved schedule instead of ad-hoc args
    [--metrics counts,areas,volumes|all]
    [--group-by none|kind|tag|material|definition]
    [--visible]
    [--tag <id>] [--material <id>] [--definition <id>]
    [--precision full|0..6]
    [--format csv|json]
    [--list-schedules]
    [--save-schedule <name>]      # saves the flags as a schedule
    [--delete-schedule <name>]
```

- Read-only with respect to the model; it never modifies it and never
  replaces an existing output file (refused with a message).
- Ad-hoc flags and `--schedule` are mutually exclusive.
- The `--session` JSON-lines mode gets `report.run` (a bounded query: the
  request object, `documentId`, `expectedRevision`; the same four-table
  JSON of 6.2, with the metadata) and `report.schedule`
  (save/describe/delete), following the "every runtime feature includes
  its command/query path" execution rule (PR_ROADMAP item 6).

## 9. Assistant and MCP

- **MCP** exposes `reports.run` and `reports.schedules` (the MCP surface
  exposes every command/query; ADR 0169 decision 9 pattern).
- **Assistant: off by default in v1.** Exposure to the in-app assistant
  needs a new corpus version and a token measurement first, exactly as
  ADR 0168 decision 10 did for the terrain queries and ADR 0171
  decision 13 ("assistant exposure needs a new corpus version").

## 10. Desktop UI needs (UX_DESIGN section 19, to be designed later)

The desktop work waits for an approved **UX_DESIGN section 19** (§16 is
the IFC dialog work, §17 is the camera-path work (ADR 0173), §18 the
styles work (ADR 0174), all in parallel
drafts; none of them exists in `docs/UX_DESIGN.md` at this commit
**not verified** beyond that observation). That section must resolve:

- **Entry point and default view.** A Quantities entry (an Info-tab
  section beside Terrain edits, or a dedicated Reports menu — the section
  decides) whose default view is the `none`-group "All" row (counts,
  area, volume, unvalidated) for the document's units.
- **Grouping and filters.** The `groupBy` switch (none/kind/tag/material/
  definition), the visibility toggle (all/visible), tag/material/
  definition pickers (by ID, as the existing pickers do), and a selection
  toggle only while a session selection exists (the file-only case shows a
  refusal hint, mirroring the capability note, §1).
- **Live state.** "Computed at revision N" under the totals; a "computing
  …" state with progress and Cancel for long runs (§5, §12); the invalid
  list with the "Not a solid: …" wording of the entity info panel and a
  status count per row.
- **Schedules.** A schedules list (the document's, keyed by identity, §7):
  create (from the current filters), rename, delete, Run, and Export
  CSV…/Export JSON… through the Save dialog, new-file-only (the product
  convention).
- **Units.** Totals in the document's display units, precision per §3,
  with the unit stated in the labels (e.g. "40,174.20 m²" at precision
  2; "141.2587 ft³" for the ft-in fixture).
- **Keyboard and accessibility** per the usual matrix (X11 and Wayland,
  1×/2×, 200 % text; not colour alone for the invalid rows).

## 11. Verification plan (fixtures with hand-computed expected totals)

Two in-repo fixtures (`.sketchyup`, built by the CLI; golden outputs
committed as exact bytes). **`quantities-a`** (meters, precision `full`,
default `visibility: all`):

| Fixture content | Hand value |
| --- | --- |
| Box P 2×2×1 m; definition `BoxP`; three placements on tag `Boxes` | 3 instances; 6 contexts (root + member each); 12 m³; 48 m² |
| Scaled copy of P (plain geometry), scale (2,2,2) | 1 context; 32 m³; 64 m² |
| Mirrored copy of P (plain geometry), scale (−1,1,1) | 1 context; 4 m³; 16 m² (identical to unscaled) |
| Three-level transform chain: root group scale 1.5, child group scale 2, holding a 1×1×1 box | 3 contexts (2 groups + 1 box); 27 m³ (3.0 cubed); area 6 m² × (3.0)² = 54 m² (corrected in R101.a review; was 9 m²) |
| Hidden group `Stash` holding a 1×1×1 box (1 m³; 6 m²) | included by default; excluded under `visible` |
| Box on hidden tag `Off`, 2×2×0.5 (2 m³; 12 m²) | included by default; excluded under `visible` |
| Open plate 1×2 m, single face | area 2 m² counted; volume absent; status `open_boundary` |
| Two 1 m³ cubes sharing a face, as one body | 1 context; area 12 m² counted; volume absent; status `non_manifold` |
| Zero-thickness extrusion of a 1×2 face | area 0 m²; volume absent; status `degenerate` (cannot be built: `Surface::extrude` refuses a zero distance; see the R101.a notes) |
| Terrain T: 200 m × 200 m flat plane, 1 m grid | 80,000 triangles; area 40,000 m² (flat, tolerance ≤ 1e-6 relative); 1 container + tile contexts |
| Definitions: `Post` (0.5×0.5×2 box: 0.5 m³; 4.5 m²), `Top` (2×1×0.1 box: 0.2 m³; 4.6 m²), `Table` (4 `Post` refs + 1 `Top` ref, no members of its own); two `Table` placements | 3 unique definitions; 12 materialized placements (2 `Table` + 8 `Post` + 2 `Top`); 22 contexts (root + placements + members); 4.4 m³; 45.2 m² |
| Materials: `Post` painted `Steel`, `Top` painted `Wood` | Steel 36 m²; Wood 9.2 m²; unassigned 214 m² (of the 259.2 m² non-terrain) |
| One sheet, two views, three items | metadata only |

*Corrected in the R101.a review (2026-10-26): the chain area, the area totals, the shared-face and
degenerate contexts and the terrain tile contexts below; the earlier figures did not follow from the
rows above. The R101.a notes give the arithmetic.*

Hand totals for the whole document as first written: **contexts 80,041** (3 `BoxP`
placements = 6 contexts + 1 scaled + 1 mirrored + 3 chain + 2 `Stash` + 1
`Off` + 1 plate + 2 shared-face cubes + 1 degenerate + 80,001 terrain +
22 component contexts = 80,041); **instances 15** (5 top-level: 3 `BoxP`
+ 2 `Table`; 10 nested: 8 `Post` + 2 `Top`); **unique definitions 4**
(`BoxP`, `Post`, `Top`, `Table`); **known volume 82.4 m³** (12 + 32 + 4
+ 27 + 1 + 2 + 4.4; the invalid and degenerate records contribute none);
**unvalidated 3** (one per status: `open_boundary`, `non_manifold`,
`degenerate`); **area 40,174.2 m²** (169 m² non-terrain non-component +
45.2 m² component + 40,000 m² terrain); under `visible`: volume 79.4 m³,
area 40,156.2 m². **Corrected:** area **40,259.2 m²** (214 m² non-terrain non-component + 45.2 m²
component + 40,000 m² terrain), under `visible` **40,241.2 m²**; volumes unchanged (82.4 m³ and
79.4 m³); the shared-face cubes are one context and the degenerate body cannot be built, so
**unvalidated 2**; the terrain is 1 container + 81 tile contexts, so **contexts 120**. The same fixture in `ft-in`:
4 m³ = 141.2587 ft³ (tolerance ≤ 1e-4 relative), 16 m² = 172.2226 ft².

Checks (each maps to the Verify line):

| Verify clause | Check |
| --- | --- |
| Shared components follow the explicit counting rules | V1: definitions = 4 counted once each; `Table` filter returns 2 placements + their 10 owned records; the `Boxes` group row equals 3 / 12 m³ / 48 m² |
| Nested instances | V2: `Table` placements yield the 8 nested `Post` placements; a `definition=Post` filter counts exactly 8, no double count, members under the outer definition's group |
| Hidden content and hidden tags are explicitly stated | V3: default totals include Stash and Off (82.4 m³ / 40,259.2 m²); `visible` excludes them (79.4 m³ / 40,241.2 m²); the metadata states the applied mode |
| Nested and mirrored transforms | V4: the mirrored copy equals the unscaled box; the 2× scale is ×8 volume / ×4 area; the three-level chain is 27 m³ / 54 m² |
| Geometry edits update totals | V5: scale `Stash` by 2 → its contribution ×8; the totals change by hand; the revision stamp increments; undo restores the previous totals with a new, higher revision (§4: revisions only increase) |
| Invalid solids are not counted as known volumes | V6: the three invalid contexts appear under their exact statuses; volume totals exclude them; an `analysis_limit` case (an over-budget shell) is also excluded from the total and listed |
| Live update | V7: on a large model the panel shows "computing …" without freezing the UI; Cancel returns in ≤ 100 ms |
| Units and precision are stated | V8: the header carries the unit codes and precision for each document unit setting (m, mm, ft-in) and each precision (`full`, 3) |
| Source revision is stamped | V9: the stamp equals the document's identity + revision; a different revision changes the stamp; undo/redo states are both reported exactly |
| CSV dialect | V10: golden bytes — UTF-8 no BOM, LF, minimal RFC 4180 quoting (fixture names with commas and quotes), metadata block, byte-identical repeated runs (hash-pinned) |
| Saved schedules | V11: the sidecar round-trips; a document save is bit-identical with or without a schedule present; a schedule for another document is refused |
| CLI | V12: `--report` writes a new file and never replaces; an existing path is refused; `--list-schedules` / `--save-schedule` / `--delete-schedule`; the CLI output is byte-identical to the session `report.run` result for the same request and revision |

**`quantities-b`** is the budget fixture (§12): 250,000-triangle
terrain plus 10,000 placements of a 12-face part (two joined 6-face
boxes): 10,000×(1 root + 2 members) = 30,000 materialized bodies and
120,000 materialized faces — inside the 20,000-placement / 40,000-body /
600,000-face limits (`src/core/document_limits.hpp`).

## 12. Budgets

The document limits (`src/core/document_limits.hpp`) bound a "large" model
to 20,000 materialized placements, 40,000 materialized bodies, 600,000
materialized faces and 250,000 terrain triangles. Proposed budgets (to be
**measured and confirmed in R101.b before being frozen**, the
[ADR 0171](0171-ifc-exchange-subset.md) §6.1 /
[ADR 0169](0169-terrain-editing-drape-stamp.md) §13.1 pattern):

| Work (desktop, the developer machine class) | Budget |
| --- | --- |
| Counts over `quantities-b` (250,000-triangle terrain + 10,000 placements of a 12-face part: 120,000 materialized faces) | ≤ 2 s |
| + areas (world triangle areas over all records) | ≤ 5 s |
| + solid volume analysis (per-context `inspectSolid`, `quantities-b`) | ≤ 30 s, async with progress; cancel returns in ≤ 100 ms and the run stops in ≤ 5 s |
| Desktop panel re-run after a single edit, ≤ 100,000-face model | p95 ≤ 250 ms |
| CSV write | ≤ 10 s for 100,000 rows; output ≤ 256 MiB (refused above) |
| Schedule re-run | the matching report budget (no extra pass) |
| Engine memory | peak ≤ 512 MiB; no cross-run cache |

**Not verified** in the repository: no full-document solid sweep or
full-document area sum has been benchmarked before R101 (the entity info
panel measures one context at a time, `src/app/entity_info_panel.cpp`);
the 30 s / 5 s figures are proposals to be confirmed, not measurements.

## 13. Out of scope

- Terrain cut/fill volumes (ADR 0169 §10), terrain re-simulation.
- A `reports-v1` document table and any schema bump (the document stays at
  schema 29, `src/io/native_format.hpp`).
- Assistant exposure (needs a new corpus version and a measurement, §9).
- Sign-based volume arithmetic (subtraction, mirrored cancellation).
- Counting the session's temporary hide set in `visible`.
- HTML/PDF/Excel output; CSV is the file format of record, JSON secondary.
- A content digest in the stamp (identity + revision are the stamp).
- Back-material attribution in areas (front material only in v1).

## Implementation split

R101 lands as six independently reviewable units, each with its own tests
and acceptance record. The acceptance record lives at
`docs/verification/R101f-quantities-reports.md` (JSON: the Verify line and
each clause's checks, the confirmed §12 budgets, the native matrix — Linux
desktop, Windows desktop, macOS; the sanitizers; and the support matrix
row) — the R097/ADR 0171 pattern.

- **R101.a — The report engine.** The request model (§1), the counting
  rules (§2.1–2.5) as one pure pass over the materialized document, unit
  and precision conversion (§3), the revision stamp (§4), and the session
  `report.run` bounded query. Hand-computed fixtures Q1–Q4 (V1–V4, V6, V8,
  V9).
- **R101.b — Outputs and CLI.** The CSV dialect and JSON format (§6), the
  golden bytes (V10), `sketchyup-cli --report …` with the flags of §8, the
  new-file-only refusal, and the confirmed budgets (V12, §12).
- **R101.c — Saved schedules.** The user-level store (§7), save/describe/
  delete, the no-schema-impact check (bit-identical save), the
  identity-guarded refusal, CLI and session paths (V11).
- **R101.d — MCP surface.** `reports.run`, `reports.schedules`, following
  the "MCP exposes all" pattern; assistant stays off (owner decision 10).
- **R101.e — Desktop.** The Quantities panel, schedules list and export
  dialog per the approved UX_DESIGN section 19; live update and cancel
  (V7).
- **R101.f — Acceptance record.** `docs/verification/
  R101f-quantities-reports.md` per the pattern above, with the measured
  budgets.

## Reconsideration triggers

- The owner wants schedules to travel with the model file → the
  document-embedded option (a) of §7: `reports-v1`, schema 30, the writer
  emitting the lowest schema the content needs, a decode path and a
  migration entry.
- A 250,000-triangle / 10,000-instance report cannot meet the §12 budget
  on the developer machine class → a background worker for the sweep
  (the inference/terrain-preview worker pattern), measured first.
- Consumers need a model-independent content digest in the stamp →
  canonical-encoding digest, measured for cost.
- The owner wants the report assistant-visible → new corpus version,
  token measurement, the ADR 0168 decision 10 pattern.

## Owner decisions

Each decision lists the recommendation and a one-line reason. "Approve
all" takes every recommendation.

1. **Default scope.** Count everything — hidden groups, hidden bodies,
   hidden tags — by default; the `visible` filter is an explicit opt-out.
   *Reason: it matches the existing `measureEntity` / `entity.describe`
   ("includesHidden: true") semantics and keeps a takeoff stable.*
2. **Component counting.** Unique definitions counted once; materialized
   placements (top-level + one per nested reference edge per outer
   placement) counted per instance; parametric placements are one
   placement. *Reason: it follows the ADR 0007/0159/0165 placement
   semantics and `ComponentSize.placements`.*
3. **Transforms.** Area = Σ world-space triangle areas; volume = validated
   local volume × |world determinant|; mirrors (negative determinant)
   count identically; no sign-based arithmetic. *Reason: it is the
   existing `measureEntity` rule verbatim (mirror-invariant,
   scale-aware).*
4. **Invalid solids.** Never a known volume; listed per exact status
   string; `analysis_limit` counts as unknown; their area still counts.
   *Reason: conservative with the geometry contract — no validated volume
   is a known volume.*
5. **Terrain.** Container + tile records counted; area from the tiles'
   triangle areas; no volume; cut/fill stays out of scope. *Reason:
   terrain is not a solid, and ADR 0169 §10 already defers cut/fill.*
6. **IFC spaces and sheets.** Spaces follow the generic rules (typically
   excluded by `visible` with their default-hidden tag); sheets/views/
   items are metadata only. *Reason: spaces are ordinary bodies and sheets
   are not geometry, so nothing special should exist.*
7. **Units and precision.** The document's display units (base codes in
   tabular output: m/mm/ft, m²/mm²/ft², m³/mm³/ft³); precision defaults
   to the document's `displayPrecision`; both stated in the output
   header. *Reason: F07 asks for reports "from document semantics", and
   `unit_display.hpp` already has the conversion.*
8. **Stamp.** Document identity + revision on every report; no content
   digest in v1. *Reason: both are available in every state, the revision
   is injective, and a digest costs a full serialization per run.*
9. **Saved schedules.** User-level JSON keyed by document identity; no
   document schema change (the document stays at schema 29). *Reason:
   schedules are tool state, not model state — the ADR 0169 "no schema
   30" pattern.*
10. **Live update and caching.** No cross-run cache; the UI re-runs on
    revision change; long runs async with progress and cancel. *Reason:
    the revision already invalidates any cache, so caching buys nothing
    in v1.*
11. **Outputs.** CSV with the §6.1 dialect is the format of record; JSON
    (same data) secondary; no HTML/PDF. *Reason: F07 asks for
    "configurable CSV/tabular exports".*
12. **Surfaces.** `sketchyup-cli --report …` + session `report.*` +
    MCP `reports.*`; the assistant stays off by default. *Reason:
    follows ADR 0171 decision 13 and ADR 0168 decision 10 — widening the
    assistant needs a corpus version first.*
13. **Budgets.** Adopt the §12 table, confirmed by measurement in
    R101.b before being frozen. *Reason: the established 0168/0169/0171
    pattern; no full-document sweep has been benchmarked before R101.*

## Implementation notes (R101.a)

R101.a is the report engine, ADR §1–§4 and §11. The counting pass is `src/core/report.{hpp,cpp}`
(one pure function over the materialized document). The request model, units, precision and the
stamp are `src/automation/report_commands.{hpp,cpp}`. The `report.run` query is registered in
`src/automation/inspection.cpp`. Tests: `tests/report_tests.cpp` (fixtures Q1–Q4; checks V1–V4, V6,
V8, V9; refusals; determinism; the query). `tests/report_benchmark.cpp` is an `EXCLUDE_FROM_ALL`
harness for the §12 timings. ADR 0175 does not name Q1–Q4; they are read as four documents, Q1
the components, Q2 the transform copies and hidden content, Q3 the invalid solids and the
analysis limit, Q4 the whole quantities-a document with the terrain.

### Where the ADR's hand totals do not follow from its own rows

The tests assert the corrected arithmetic and name the ADR value beside the check. The ADR text is
unchanged here; these need the owner's decision before R101.b freezes the fixtures.

1. **Three-level chain, area (§11, V4): 9 m² should be 54 m².** A 1 m cube has 6 m² of faces (the
   Stash row of the same table counts it as 6 m²). Scaled by 3 in every direction, each face grows
   by 3² = 9, so 6 × 9 = 54 m². The ADR's "area ×(3.0)² = 9 m²" applies the factor to 1 m² instead.
2. **Area total (§11, V3): 40,174.2 m² should be 40,259.2 m².** The ADR's own rows give 169 m² of
   non-terrain, non-component area (the chain at 9 m²), 45.2 m² of components and 40,000 m² of
   terrain: 169 + 45.2 + 40,000 = 40,214.2 m², not 40,174.2 m² (off by 40). With the chain at 54 m²
   and the zero-thickness row at the ADR's 0 m² (see 6): 48 + 64 + 16 + 54 + 6 + 12 + 2 + 12 + 0 =
   214 m²; 214 + 45.2 + 40,000 = 40,259.2 m². Visible (minus Stash 6 m² and Off 12 m²) is
   40,241.2 m², not 40,156.2 m². The materials line becomes "unassigned 214 m² (of 259.2 m²
   non-terrain)", not 169 m² (of 214.2 m²).
3. **Shared-face cubes (§11 context total): one context, not two.** The row describes one
   non-manifold solid with no volume, and "unvalidated 3 (one per status)" needs one such context.
   Two separate cubes would each be a valid solid with a known volume. The fixture is one body.
4. **Terrain contexts (§11 "80,001 terrain"): one tile per triangle is assumed.** Tiles hold at most
   2,048 triangles (`TerrainLimits::tileTriangles`), so the 80,000 triangles of the 200 m flat grid
   are 81 tiles (measured). The whole-document total is 38 non-terrain + 1 container + 81 tiles = 120
   contexts, not 80,041.
5. **Undo and the revision (§11 V5, checked here through V9): "undo … a lower revision" contradicts
   §4.** `Document::undo` and `redo` each increment the revision, as §4 says, so a state is never
   stamped twice. The test checks that the totals come back and that the revision moves forward.
6. **Zero-thickness extrusion (§11, the `degenerate` row): not buildable, so not verified.**
   `Surface::extrude` refuses a distance of zero and one below tolerance. A closed prism with a
   collinear base vertex is accepted and is a valid 1 m³ solid, not `degenerate`. Neither
   construction gives `degenerate`. No body the public API can build gets `degenerate`, so the fixtures have two
   invalid statuses (`open_boundary`, `non_manifold`) and the whole-document unvalidated count is 2,
   not 3. The `analysis_limit` case (65 disjoint cubes, over the 64-shell limit) is tested on its own.
   **Open:** either a construction the solid analysis reports as `degenerate`, or a different row.
7. **Status count (§2.4).** The text says "the eleven above"; §0 and §6.1 list ten. `multiple_records`
   and `not_a_context` are context-level cases. A context is one materialized body record (§2.1), so
   `multiple_records` cannot arise and the invalid table has the ten statuses of §6.1.

### Decisions where the ADR was silent

- **Containers are not solid candidates.** Groups, placement roots, reference images and terrain
  containers have no solid of their own. Only `Geometry` records are analysed and listed; otherwise
  every placement root would be `empty` and the "unvalidated 3" of §11 could not hold. §2.4's
  "contexts without geometry: empty" is read as a Geometry record with no faces.
- **Tags are inherited.** A record carries its own tag and every ancestor's (`inheritedTags`), so the
  members of a tagged placement count under its tag (V1's Boxes row needs this). A folder aggregates
  the tags below it.
- **Definitions.** A nested placement and its members fall under the outermost placement's
  definition (§1). The `definitions` count of a group is the definitions with an in-scope placement in
  that group.
- **Material grouping** keys a record by the front material of its lowest-ID face. Areas are split
  per face, so each area is counted once.
- **A `precision` request field** (`"full"` or an integer) is added to §1. §3 says precision defaults
  to `displayPrecision` and §8 has `--precision`, so the request needs it. Full precision is at most
  eight decimals, trimmed, in the C locale. It is not `displayDecimal` or `displayMeasure`: those use
  the default locale, and `displayMeasure`'s full form is eight significant digits. The engine uses
  the factors of `unit_display.hpp` (mm ×1000, ft ÷0.3048); the ft-in example of §11 is checked
  against `displayMeasure`'s own value.
- **Selection** (editor sessions only) is the selected bodies and their descendants. A hidden body
  cannot be selected, so a selection never brings in hidden content.
- **Empty tables.** The All row is always in `counts` and `volumes`, zero when nothing is in scope.
  The terrain table is always present. `volumes` also emits the invalid table (§1).
- **No document format change.** The document stays at schema 29 (§7).

### Surfaces

- `report.run` is an inspection query, so it reaches the session, `transaction.inspect` and MCP
  through the registry, like every query. **MCP:** `docs/api/mcp-2026-07-28.json` and
  `docs/api/mcp-native-2026-07-28.json` now list `report.run`. §9 names the MCP tool `reports.run`
  (R101.d). R101.a does not add that name, so R101.d must decide whether to rename or alias it.
- **Assistant:** excluded by prefix in `assistantVisible` (decision 12). `assistant_tool_surface_tests`
  passes against the unchanged baseline.
- **Native assistant execution:** `NativeAssistantSession` runs any registry query it is given and
  does not check the assistant's advertised list. It already does this for `drawing_*`,
  `component_parameter*` and `site.*`. Advertising is what keeps the assistant from `report.run`; an
  execution-time filter would apply to all of those queries and is a separate change.

### Measured timings (§12)

Build type: Release (`CMAKE_BUILD_TYPE=Release`, GCC 16.2.1, `-O3`) for the Release column, and
Debug (`-O0`, no debug info) for the second column. These are
not the R101.b confirmation. Measured with `tests/report_benchmark.cpp` on the developer machine
(Linux, 30 GB RAM): a 249,218-triangle terrain (256 tiles), 10,000 placements of a 12-face part (two
6-face boxes in one component, the quantities-b shape), 30,257 records, 120,000 faces.

| Work | Budget (§12) | Release | Debug |
| --- | --- | --- | --- |
| Counts (groupBy none) | ≤ 2 s | 0.36 s | 2.35 s |
| + areas (areas run alone) | ≤ 5 s | 0.47 s | 2.89 s |
| + solid volumes (volumes run alone) | ≤ 30 s | 1.31 s | 8.70 s |
| All four tables | (the sum) | 1.32 s | 9.06 s |
| Peak resident memory | ≤ 512 MiB | 233 MiB | 254 MiB |

Each run computes the face areas as well, because the terrain table is always emitted, so the
single-table rows are not cheaper than the whole pass. Building the fixture (the terrain, then the
10,000 placements) took 0.96 s and 0.83 s in Release, and is not part of any budget. The §12 budgets
are the ADR's proposals; on this machine every measured stage is well inside them.

### Not covered by R101.a

- The `degenerate` status (point 6 above).
- V5 beyond V9's single edit and undo, V7 (live update and cancel), V10–V12 (outputs, schedules, CLI).
- Sheets. The metadata reads the sheet, view and item tables, but no fixture has sheets, so the
  "one sheet, two views, three items" row is not built.
- The desktop selection path (`DesktopInspection`) is not run by a desktop test.
- The §12 budgets on the full application, and on the other platforms of the acceptance record.

## Implementation notes (R101.b)

R101.b is the outputs and the CLI: the CSV file of record and the JSON form (§6), `sketchyup-cli
--report` (§8, without the schedule flags), the quantities-a fixture with hash-pinned golden outputs
(V10), the CLI and session parity (V12), and the §12 budgets confirmed in a Release build on
quantities-b. The CSV is `src/io/report_csv.{hpp,cpp}`, a rendering of the `report.run` data object,
so the CSV and the JSON cannot disagree. The file writer is `writeReportFile` in
`src/automation/report_commands.cpp`, and it publishes through `publishNewFile` (`src/io/new_file.cpp`),
the native "never replace" convention. Tests: `tests/report_output_tests.cpp` (the dialect, quoting,
the metadata matrix, goldens, session parity and the fixture generator), `tests/report_cli_tests.cpp`
(success, refusals, read-only input, the CSV and JSON agreeing, parity with the session), and
`tests/report_fixtures.hpp` (the R101.a builders, now shared with `report_tests`, plus quantities-b).
The goldens are `tests/fixtures/reports/quantities-a-{default,material}.{csv,json}`, generated from
`tests/fixtures/reports/quantities-a.sketchyup` by `report_output_tests --write-goldens`.

### Decisions where the ADR was silent

- **The group column carries the group's name.** V10 asks for fixture names with commas and quotes
  in the golden bytes, and the ADR's `group` column is where a name belongs. The fixture's Steel
  material is named `Steel, "galvanised"` for that reason. The ID goes in the `tag`, `material` or
  `definition` column when the report groups by that axis; a group with no ID (untagged, no
  material, unplaced) has `—` there; every other row leaves the cell empty, and the All row never
  has one. Areas rows always carry the material cell (its ID or `—`), in the order of §6.1.
- **Invalid rows** use the status as the group and `contexts` as the metric. **Terrain rows** use
  the terrain's name as the group, because the table has no axis column. Two terrains with the same
  name are told apart only by their JSON `terrain` ID, which the CSV does not carry. This is a gap.
- **Metadata lists are semicolon-joined** (`#metrics=counts;areas;volumes`, `#filters.tags=5;7`), so
  that no metadata line holds a comma. The ADR named only the filter lists this way.
- **The JSON file** is the compact `data` object with no final newline. It is byte-identical to the
  session's `report.run` data, which is the V12 test. Qt writes the keys sorted, so `metadata` is not
  first, as §6.2 lists it. The data is the same.
- **The metadata has no `materialsApply` line.** The session's JSON states that counts and volumes
  ignore the materials filter (§1). The CSV cannot state it without a `#` line the ADR does not list
  ("no other `#` lines"). The JSON says it; the CSV does not. This needs the owner's decision.
- **Session shape.** The count, area, volume and terrain rows gained a `name` field. `key` is
  unchanged. The shape is additive, and no existing field moved. The generated tool reference
  documents no response shape, so it is not changed for this.
- **Format and paths.** The format comes from the `.csv` or `.json` extension. A `--format` that
  disagrees with the extension is refused. `--tag`, `--material` and `--definition` repeat; they
  do not take comma lists. An existing path is refused as `FILE_EXISTS` before any work, and
  `publishNewFile` checks it again. The 256 MiB refusal comes before anything is published.
- **Capabilities.** `--capabilities` lists `reportExport`, and `--describe-command report` describes
  the command. Neither lists the schedule flags.

### Confirmed budgets (§12)

Release build (`CMAKE_BUILD_TYPE=Release`, GCC 16.2.1, `-O3 -DNDEBUG`), on the developer machine
(Linux, 22 threads, 30 GB RAM). `tests/report_benchmark.cpp` builds quantities-b in memory: a
249,218-triangle terrain plus 10,000 placements of a 12-face part, 30,257 records. The file
`tests/fixtures/reports/quantities-a.sketchyup` is committed; quantities-b (12.5 MB as a `.sketchyup`) is
not, and is written to a scratch path when the CLI run is needed. The measured figures are the
ones below; the budgets stand as the ADR proposed them.

| Work (quantities-b) | Budget (§12) | Measured | Notes |
| --- | --- | --- | --- |
| Counts (groupBy none) | ≤ 2 s | 0.25 s | 30,257 contexts |
| Areas (all world triangle areas) | ≤ 5 s | 0.27 s | with counts, the all-tables run is 0.71 s |
| Solid volume analysis (`inspectSolid` per context) | ≤ 30 s | 0.69 s | synchronous; the async run and its cancel are R101.e |
| All four tables | (the sum) | 0.71 s | R101.a measured 1.32 s on this machine class |
| CSV write, 100,002 rows | ≤ 10 s | 0.070 s | a synthetic report: 33,334 count groups, 3,989,304 bytes |
| CSV write, real report by tag | (part of the 10 s) | 0.005 s | 6,016 rows; about a thousand tag groups |
| Output size | ≤ 256 MiB | 3.99 MB | the 100,002-row file; the refusal above 256 MiB is not exercised |
| Engine memory, peak | ≤ 512 MiB | 284 MB | `VmHWM` of the whole process; the document alone is 225 MB before the runs |
| CLI, end to end (load, report, write) | (no budget) | 1.99 s wall, 292 MB peak RSS | `--report` with all metrics, CSV and JSON; the load is most of the time |

The 100,000-row case is synthetic because one document cannot hold 100,000 groups: a document
holds at most 1024 tags, and quantities-b placements share 1000 of them. The real tag report
(6,016 rows) is the largest real output measured here, and the synthetic case measures the writer.

**Not measured in R101.b:**
- the desktop re-run after one edit (p95 ≤ 250 ms), the cancel (≤ 100 ms) and the stop (≤ 5 s):
  R101.e, with the desktop panel;
- the schedule re-run (the matching report budget): R101.c;
- the refusal of a file over 256 MiB: the check runs before anything is published, but no report
  that large was built.

## Implementation notes (R101.c)

R101.c is the saved schedules (§7, option b), the schedule flags (§8), the session `report.schedule`
and check V11. The store is `src/automation/report_schedules.{hpp,cpp}`. The flags are in
`src/automation/main.cpp`, the scheduled run is `writeReportFile` in `src/automation/report_commands.cpp`
(a `schedule` field on `ReportFileOptions`), and the CSV marker is in `src/io/report_csv.cpp`. Tests:
`tests/report_schedule_tests.cpp` (in process: the store, the identity guard, the stale IDs, the
bit-identical save, the run path and the session) and `tests/report_schedule_cli_tests.cpp` (the built
`sketchyup-cli`). The R101.b CLI test no longer expects the schedule flags to be refused.

### Decisions where the ADR was silent

- **Store.** `AppLocalDataLocation/report-schedules.json`, beside the presentation jobs
  (`SketchyUp/SketchyUp` on Linux). The ADR names the `QSettings` family as the place, but this is a
  JSON file, so `QSettings` is not used. The layout is
  `{"storeVersion":1,"documents":[{"documentId":…,"schedules":[{"name","createdAt","request"}]}]}`,
  compact, with keys sorted. The file is written with `QSaveFile` (owner read and write only) under
  a `QLockFile`. A commit refuses with `FILE_CHANGED` when the file changed since this process read it.
  A missing file is an empty store.
- **Corrupt or oversized store.** Refused with `INPUT_ERROR` and located by path, then by byte (for
  JSON) or entry (for the layout: `documents[2].schedules[0]`). It is never reset and never rewritten.
  A folder or link where the file should be is refused too.
- **Bounds (§7).** Store file 1 MiB; 64 documents; 32 schedules per document; a name of 1 to 64
  characters, made of letters, digits, spaces and `. _ ( ) -`, starting and ending with a letter or a
  digit, so it never holds a comma, quote or line break (it is a plain value in the CSV's
  `#schedule=` line); a saved request of 16 KiB at most (the inspection request bound). Each refusal
  names the bound.
- **Identity guard (V11).** A store entry is keyed by `Document::identity()`, the random identifier
  the native file keeps (a save keeps it). A name saved only for another document is refused with
  `WRONG_DOCUMENT`, and the refusal names both identities. A name saved nowhere is `NOT_FOUND`. A
  session request whose `documentId` is not the session's document is refused with `WRONG_DOCUMENT`.
  Names are unique per document, so the same name may exist for two documents.
- **Stale IDs.** Checked at save and again at run, before the engine's own check. Every tag,
  material and definition ID is looked up. The refusal (`NOT_FOUND`) names the missing IDs in its
  message and in `details.missing`, and the run never proceeds with fewer filters.
- **Selection.** A saved schedule cannot use `filters.selection`, because the CLI has no session
  selection to run it with. A save that asks for it is refused by name.
- **`report.schedule` is a session operation, not an inspection query.** The inspection registry is
  read-only by its own contract (`inspection.hpp`), and `transaction.inspect` nests its queries, so
  a write there could run inside a draft. The operation sits beside `document.save`:
  `AutomationSession::execute` dispatches it, and `sessionCapabilities()` lists it with its schema and
  side effect. Its request vocabulary is the one `report.run` validates, read from the registry. The
  actions are `save` (a name and a request), `describe` (a name, or without one the document's list)
  and `delete` (a name). **This departs from the brief's "registered like report.run"**: it is
  registered in the session dispatch, not in the query registry.
- **Not on MCP.** `mcpToolCatalog()` builds its list from the session operations, so the new
  operation would have appeared there. It is skipped until R101.d adds `reports.schedules` (ADR §9).
  `mcp-2026-07-28.json` is unchanged. The desktop's native MCP list takes only `session.describe` from
  that catalog, so `mcp-native-2026-07-28.json` is unaffected.
- **Assistant.** `assistantVisible` already hides the `report.` prefix. The operation is also in no
  assistant catalog, and the native assistant takes only `transaction.*` operations and registry
  queries. `assistant_tool_surface_tests` passes against the unchanged 1.0.1 baseline.
- **Flags.** `--format` is one of the ad-hoc flags, as the §8 list has it, so `--schedule` and
  `--save-schedule` refuse it. `--schedule` needs `--report`. The three management flags run one at a
  time, and none combines with `--report` or `--schedule`. A flag that does not apply is refused by
  name, and nothing is written.
- **Metadata.** A scheduled run's CSV says `#schedule=NAME` in place of `#ad-hoc=1`, and the rest of
  the bytes are the ad-hoc run's. Its JSON carries `metadata.schedule`. The ad-hoc JSON carries no
  marker, so the R101.b goldens are unchanged. The CSV's marker line is therefore the only ad-hoc
  marker, and the JSON agrees with the CSV on the data but not on the marker.
- **Application names.** The store location needs the organization and application names. The CLI
  sets them before any schedule or session work, as the presentation job path does, and the desktop
  sets them in its `main`. A caller without them is refused (`INVALID_OPERATION`), never given a
  guessed folder.
- **Concurrency.** A second process that holds the store lock is refused with `OUTPUT_ERROR`. A file
  that changed after this process read it is refused with `FILE_CHANGED`, and never merged.
- **Timestamp.** `createdAt` is UTC, ISO 8601 (`Qt::ISODate`), written by save and checked on open.

### Measured budget (§12, schedule re-run)

A schedule re-run is the report's own pass: the store read, the identity lookup, the ID check and the
same `reportDescription` the flags use. There is no extra pass over the document. Measured with the
Release `sketchyup-cli` (`CMAKE_BUILD_TYPE=Release`, GCC 16.2.1, `-O3`, `-DNDEBUG`) on the developer
machine (Linux, 30 GB RAM), on quantities-b written by `report_benchmark --write-fixture` (12.5 MB).
Each figure is the wall clock of one CLI process, from start to exit, with `--metrics all --group-by
tag` and CSV output. The runs alternate, five of each:

| Run | Wall time, five runs (s) | Median (s) |
| --- | --- | --- |
| Ad-hoc flags (`--report … --metrics all --group-by tag`) | 2.06, 2.08, 2.07, 2.07, 2.10 | 2.07 |
| Saved schedule (`--report … --schedule Bench`) | 2.05, 2.08, 2.10, 2.08, 2.09 | 2.08 |

Most of each run is loading the document (R101.b measured the CLI load at 1.99 s end to end on this
machine class). The scheduled CSV equals the ad-hoc CSV byte for byte apart from the marker line. The
schedule's own work (opening the store, the lookup, the ID check) was not timed alone; it sits inside
the run-to-run spread of about 0.03 s. The schedule re-run therefore meets the matching report
budget, and this is a measurement, not a proposal. The §12 budgets are unchanged.

### Gaps and what was not run

- **Windows and macOS.** `XDG_DATA_HOME` and `HOME` redirect the store on Linux only. There, the CLI
  schedule test exits 77 (skipped) and the run-path and session checks in `report_schedule_tests`
  are skipped. The store-level checks use temporary folders on every platform. Closing the gap needs
  a test seam for the store path, which ADR 0175 does not define, so none was added.
- **Atomic write.** The read-only folder check runs only where this process cannot write the folder;
  as root it is skipped, and the test says so. A failure in the middle of `QSaveFile`'s commit is
  covered by Qt's contract, not by a test.
- **Sanitizer.** The schedule tests are registered in the sanitized CI lines but were not run under
  sanitizers locally.
- **Desktop artifacts.** The desktop binary was not built here, so `inspection-desktop-v1.json` and
  `mcp-native-2026-07-28.json` were not regenerated and checked. Neither derives from a catalog this
  change alters, so they should be unchanged; CI's check will confirm.
- **Hand-edited requests.** A store entry edited by hand is checked when it runs, not when the store
  opens, so an invalid request loads and refuses at run time, naming the schedule and the store.
- **Desktop.** The desktop application does not yet call `reportScheduleStorePath` (R101.e).
