# First public native document format

R073, 2026-10-07. The first public storage contract is
**sketchyup-document-v24**, carried by **SKUPDOC container version 2**. This names
and freezes the existing format; it does not renumber files or discard the earlier
migration chain. Incompatible record changes require a new document version and
required-feature set. A future version is rejected, never best-effort loaded.

## File envelope and assets

All header integers are unsigned little endian. Bytes 0–7 are `SKUPDOC\0`, bytes
8–11 hold container version 2, and bytes 12–15 hold the UTF-8 JSON manifest length.
The manifest follows, then contiguous chunk payload bytes. There is no compression,
archive extraction, executable content or host-path resolution.

The manifest has exactly `documentId`, `epoch` (decimal string `1`), `revision`,
`writer`, `units` (`m`), `up` (`Z`), `requiredFeatures`, `allocatorFloors`, `assets`
and `chunks`. The writer identifies itself as `SketchyUp/<version>` (`SketchyUp/0.1.0` before
1.0.0; see [ADR 0162](0162-release-versioning.md)); readers
validate supported required features independently of that informational string.

The first chunk has `kind: document`, `encoding: json-v24`, `offset: "0"`, `bytes`
and lowercase SHA-256. Its JSON uses external asset storage. Following asset chunks
have `kind: asset`, `encoding: raw`, positive `id`, logical `path: assets/ID.bin`,
`offset`, `bytes` and SHA-256. Offset/length values are canonical decimal strings
relative to the first payload byte. Chunks are contiguous, nonoverlapping and fully
accounted for; trailing data, duplicate assets, unknown chunk kinds/fields, invalid
checksums and disagreement between manifest and model are rejected.

Asset manifest entries contain `id`, `name`, `mediaType`, `missing`, `bytes`, `path`
and `sha256`. Missing assets have no payload chunk, zero bytes and a null checksum.
Paths are logical keys, not filesystem references. Assets remain embedded when a
file is relocated. Limits: 1 MiB manifest, 32 MiB document chunk, 1,024 asset records,
16 MiB per present asset and 64 MiB total asset payload. The file reader independently
caps input at 128 MiB. Inline raw JSON remains a legacy/import representation with
canonical base64 asset data and a 128 MiB limit.

## Document records

The JSON root has `format: sketchyup`, numeric `version: 24`, `documentId`, `revision`,
`units: m`, `up: Z`, `displayUnits`, `nextId`, `bodies`, `definitions`, `instances`,
`nextDefinitionId`, `tags`, `nextTagId`, `materials`, `nextMaterialId`, `assets`,
`nextAssetId`, `assetStorage`, `hosted`, `style`, `scenes`, `nextSceneId`, `sections`,
`nextSectionId`, `activeSections`, `annotations`, `nextAnnotationId` and `solar`.
Unknown root and required record fields are rejected. Named model properties are
an explicit exception: their string/number/boolean values are preserved metadata.

Stable identities and allocation floors use canonical unsigned decimal strings,
without signs, leading zeroes or exponent syntax. References use `"0"` only where
the record permits a null/root/default reference. Revisions are decimal strings.
Geometry uses finite JSON numbers, metres and native Z-up coordinates. Identity,
revision, allocator floors, parent/reference validity and topology are semantic
validation requirements; parsing JSON alone does not establish validity.

Bodies contain `id`, `name`, `kind` (`geometry`, `group`, `reference_image`), `parent`,
16-number affine `transform`, `hidden`, `locked`, `tag`, `properties`, RGB `color`,
front/back `materials`, `faceColors`, `faceMaterials`, `faceTextureMappings`,
`edgeAppearances`, `nextId`, `nextEdgeId`, `vertices`, `faces`, `wires`, `edges`,
`curves` and `guides`. Editable text and reference images add the optional
`textSource` and `referenceImage` records. Group/geometry consistency and invertible
transforms are checked; empty or inconsistent references are not silently repaired.

Vertices are `[id,x,y,z]`; faces are `{id,loops}` with ordered vertex-ID loops;
wires are endpoint-ID pairs; stable edges are `[id,a,b,wire]`. Face maps are keyed by
face ID. Materials hold front/back IDs, colors hold RGB, texture sides hold either
null or `{origin,uGradient,vGradient,offset}`. Edge appearance holds exactly
`hidden`, `soft`, `smooth`. Curves carry `id`, `kind`, `center`, `xAxis`, `yAxis`,
`radius`, `startAngle`, `sweepAngle`, `segments` and oriented `[edge,reversed]` pairs.
Guides carry `id`, `kind`, `origin` and a `direction` for lines.

Definitions hold `id`, `root`, `nextMemberId`, `name`, canonical body `members`,
member-to-definition `references` and optional `glue`. Instances hold `root`,
`definition` and canonical-to-placed `members`. Glue identifies a `member`, `face`,
`anchor`, `tangent` and `cutsOpening` flag. Tags hold `id`, `parent`, `name`, `folder`
and `visible`. Materials hold `id`, `name`, RGB `color`, `opacity` and `asset`.
Document asset records hold `id`, `name`, `mediaType`, `missing`, `bytes` and `data`
(null for external or missing payload; base64 for present inline payload).

The following versioned record contracts form part of this schema:

- [Hosted components](0054-hosted-components.md), [texture mapping](0061-affine-texture-mapping.md) and [face textures](0062-face-texture-records.md).
- [Model style](0068-model-styles.md), [saved scenes](0070-saved-scene-records.md), [sections](0073-section-records.md) and [scene sections](0075-section-scenes.md).
- [Annotations](0079-annotation-records.md), [editable text](0084-editable-text-persistence.md), [sun studies](0088-solar-study-records.md) and [reference images](0092-reference-image-records.md).

These contracts and their strict version-specific decoders define geometry and
cross-record invariants. Unknown required data is never treated as optional and
dropped. Model names, properties and asset metadata remain untrusted data.

## Compatibility and migration

Readers accept raw document versions 1–24 and the corresponding historical
container-v2 required-feature sets from document version 2 onward. Version 1 has
no stored revision or later scene hierarchy. Later versions introduced transforms
(2), topology (3), curves (4), guides (5), groups (6), face colors (7), components
(8), tags (9), materials (10), assets (11), display units (12), edge appearance (13),
glue (14), hosted components (15), texture mappings (16), model style (17), saved
scenes (18), section planes (19), section scene activation (20), annotations (21),
editable text (22), sun studies (23), and reference images (24). Absent historical
features receive their documented initial defaults; existing identities and payloads
survive. Deterministic topology migration for pre-v3 geometry is retained.

`sketchyup-cli --format-capabilities` prints the shipped machine-readable contract.
`--inspect-native FILE` and `--validate-native FILE` both fully decode, check and
report the file; they never rewrite it. `--migrate-native FILE --output NEW_FILE`
validates the input, encodes the public container, decodes it again and compares all
serialized records before writing a copy. It preserves document identity/revision,
asset bytes and retired allocation floors. CLI format modes are standalone.

Migration requires a new destination. Temporary bytes are written and synchronized
in the output directory, then atomically published with a non-replacing link. An
existing file, symlink or competing publication causes failure; in-place conversion
is unavailable. The containing directory is synchronized after publication. A
failure after publication explicitly reports uncertain durability and leaves the
complete copy in place. Ordinary document saving retains its separate backup policy.

Acceptance covers every historical fixture in `tests/fixtures`, all 24 raw version
branches, relocation/round trips, checksum/future-version failures, unknown required
features/chunks/records, same-path and symlink rejection, competing writers, and CLI
argument isolation. Originals are compared byte for byte after each migration.

## Schema 26: instanced component placements (R082.cc)

2026-10-10. The public schema advances to **sketchyup-document-v26** under the same
container version 2; see [instanced component storage](0159-instanced-component-storage.md).
Schema 25 added display precision ([R084.r](0018-document-units.md)). The schema-26
document chunk uses `json-v26` and `requiredFeatures` appends
`instanced-placements-v1` after `display-precision-v1`. Component placement members
are no longer stored in `bodies`: they are rebuilt from their definition and the
placement's explicit `members` map, so stored identities are unchanged. Instance
records add a required `floors` object mapping a scene member ID to
`[nextId, nextEdgeId]` for members whose allocator floors exceed their definition's;
it is otherwise empty. Every other record above is unchanged.

Readers accept raw document versions 1–26. Schemas 8–25 store expanded members;
each must equal its definition projection under the canonical comparison, or the
file is rejected with a diagnostic naming the member, component instance and
definition, leaving the source unchanged. Acceptance adds the retained
`instances-v24.sketchyup` and `instances-v25.sketchyup` writer fixtures to the
historical corpus.

## Schema 27: drawing sheets (R090.b)

2026-10-13. The public schema advances to **sketchyup-document-v27** under the same
container version 2; see [documentation sheets](0163-documentation-sheets.md). The
schema-27 document chunk uses `json-v27`, and `requiredFeatures` appends
`drawing-sheets-v1` after `instanced-placements-v1`. The document root adds four
required fields, and `allocatorFloors` adds the two allocators, which must equal the
root's:

- `drawingSheets`: records `{id, name, position, paper: {widthMm, heightMm}}`.
- `nextDrawingSheetId`.
- `drawingViews`: records `{id, name, sheet, source: {kind: "scene", scene},
  frame: {xMm, yMm, widthMm, heightMm}, offsetMm: [x, y], scaleDenominator,
  content: "technicalLines" | "rasterAppearance", hiddenLines,
  accepted: null | {revision, fingerprint}}`.
- `nextDrawingViewId`.

IDs, sheet and scene links, allocators and the accepted revision are canonical
decimal strings, and IDs are nonzero. Records are stored once each in ascending ID
order. Every field is required and unknown fields are rejected. The fingerprint is
64 lowercase hexadecimal digits. Names, paper and frame bounds, the scale range,
sheet order and the per-sheet and table limits are those of ADR 0163. A view whose
scene no longer exists is kept as a diagnosed broken link.

**The writer emits the lowest schema the content needs** (ADR 0163 decision 2). A
document with no drawing sheets and no drawing views is written as schema 26 in
exactly the 1.0 shape: `json-v26`, no `drawing-sheets-v1`, and none of the fields
above. Only the manifest's informational `writer` string names the newer release. SketchyUp 1.0 therefore still opens it. Any sheet or view makes the
file schema 27, which 1.0 refuses before reading it (its manifest check reports
`Unsupported container field`) without changing the file. Deleting every sheet
writes schema 26 again. Schema 26 does not store the drawing-sheet allocators, so
reopening such a file starts them at 1; no record or link can refer to a retired
sheet or view ID by then. `--inspect-native` reports `migrationNeeded: false` for a
sheet-free schema-26 container, and `--migrate-native` reports the schema it wrote
as `outputDocumentVersion`.

Readers accept raw document versions 1–27. Schemas 1–26 migrate with empty sheet
tables and allocators 1. Acceptance adds the retained `release-1.0.0-v26.sketchyup`
(unmodified 1.0.0 output) and `drawing-sheets-v27.sketchyup` writer fixtures.
When `SKETCHYUP_PRIOR_RELEASE_PACKAGE` names a released package (CI downloads
1.0.0-1 and checks its pinned SHA-256), package acceptance unpacks it beside the
lifecycle test. Its CLI must refuse, unchanged, every retained fixture newer than the
schemas it reads, and must accept a sheet-free save from the new package; the
package's `drawing_sheet_io` test also drives it through `SKETCHYUP_PRIOR_CLI`.

### Schema 27 addendum: page items (R091.b)

2026-10-14. Schema 27 has not shipped in a release, so it is extended in place
([ADR 0163](0163-documentation-sheets.md) decision 6) with the page items of
[ADR 0164](0164-sheet-layout-interface.md) §3. The container version, the
`json-v27` encoding and the `drawing-sheets-v1` feature are unchanged.

- Each `drawingSheets` record adds two required fields: `number` (0–32 bytes, no
  control characters, unique among sheets when nonempty; an empty string when the
  sheet has none) and `marginMm` (0–50, at most a quarter of the shorter paper
  side). Readers no longer supply defaults for them.
- The document root adds two required fields, and `allocatorFloors` adds
  `nextDrawingItemId`, which must equal the root's:
  - `drawingItems`: records `{id, sheet, z, kind, data}`. `z` is an integer, and
    the items of each sheet use each of `0`…`n-1` once, `0` at the bottom. `kind`
    is `text`, `label`, `dimension`, `line`, `rectangle` or `titleBlock`, and
    `data` holds exactly that kind's fields:
    - `text`: `{frame, text, font, align: "left" | "center" | "right"}`;
    - `label`: `{text, font, textAt: {xMm, yMm}, leader, target}`, where `target`
      is `{kind: "paper", xMm, yMm}` or `{kind: "view", view, anchor, lastMm: [x, y]}`;
    - `dimension`: `{view, anchors: [anchor, anchor], orientation: "aligned" |
      "horizontal" | "vertical", offsetMm, prefix, font, lastMm: [[x, y], [x, y]]}`;
    - `line`: `{from: [x, y], to: [x, y], strokeMm, dash: "solid" | "dashed"}`;
    - `rectangle`: `{frame, strokeMm, dash}`;
    - `titleBlock`: `{frame, font, fields: {project, drawingTitle, date, revision}}`.

    `frame` is `{xMm, yMm, widthMm, heightMm}` and `font` is `{family, sizePt}`.
    An `anchor` is the annotation anchor object of
    [ADR 0080](0080-annotation-workflows.md), limited to `vertex` and `edge` anchors
    in the `resolved` state. The title block stores only these four fields; the
    sheet number, "n of N" and the scale are derived.
  - `nextDrawingItemId`.

IDs, sheet and view links and the allocator are canonical nonzero decimal strings,
and items are stored once each in ascending ID order. Every field is required and
unknown fields are rejected at every level. Each item belongs to a live sheet, and
a bound view must exist on the item's own sheet. Stored geometry lies inside the
sheet's paper; the `lastMm` positions are only bounded. Text, font, stroke and count
limits are those of ADR 0164. A dimension or label whose anchor no longer resolves
is kept as stored; its broken state is derived when it is evaluated, never stored.

Items cannot exist without a sheet, so the writer policy is unchanged: a document
without drawing sheets is still written as schema 26 in the 1.0 shape, and deleting
every sheet, which deletes its items, writes schema 26 again. The
`drawing-sheets-v27.sketchyup` fixture was regenerated by the writer with items of
every kind, a sheet number and a nondefault margin; SketchyUp 1.0 still refuses it
unchanged.

### Schema 27 addendum: dimension format (R091.g)

2026-10-14. Schema 27 is still unreleased and is extended in place again. Each
`drawingSheets` record adds the required field `dimensionFormat`, how the sheet's
dimensions write their values ([ADR 0164](0164-sheet-layout-interface.md) R091.g):

- `unit`: `"mm"`, `"cm"`, `"m"` or `"ftIn"`;
- `decimals`: an integer from 0 to 4;
- `showUnit`: a boolean.

All three are required and unknown fields are rejected. A new sheet stores
`{unit: "mm", decimals: 0, showUnit: false}` explicitly; readers supply no default.
The value is derived text and never stored on the dimension. The
`drawing-sheets-v27.sketchyup` fixture was regenerated by the writer with a
nondefault format (`ftIn`, 2 decimals, unit shown), and the schema-27 item seeds of the
fuzz seeds and the parser fault corpus carry one too (`cm`, 1 decimal, unit shown).
SketchyUp 1.0 still refuses the fixture unchanged.

## Schema 28: component parameters (R093.c)

2026-10-16. The public schema advances to **sketchyup-document-v28** under the same
container version 2; see [constrained component parameters](0165-constrained-component-parameters.md)
§5. The schema-28 document chunk uses `json-v28`, and `requiredFeatures` appends
`component-parameters-v1` after `drawing-sheets-v1`: the feature set is exactly the
schema-27 set plus `component-parameters-v1`. A schema-28 root always carries the
schema-27 sheet tables and allocators, possibly empty. The document root adds three
required fields, and `allocatorFloors` adds `nextComponentDriverId`, which must equal
the root's:

- `componentParameters`: one record per parametric definition, in ascending
  `definition` order: `{definition, parameters, drivers}`.
  - `parameters`, in display order: `{name, label, type, mode, expression, min, max,
    step, options}`. `type` is `length`, `area`, `angle`, `number`, `count`, `boolean`
    or `choice`; `mode` is `input` or `derived`. `min`, `max` and `step` are SI numbers,
    or `null` when unset or when the type is `boolean` or `choice`. `options` is an
    array of `{value, label}` for a `choice` and `null` otherwise.
  - `drivers`, in ascending `id` order: `{id, kind, member, expression, bound, data}`.
    `kind` is `stretch`, `position`, `rotation`, `visibility`, `repeat` or
    `childParameter`, and `data` holds exactly that kind's fields: `stretch {axis, at}`,
    `position {axis}`, `rotation {axis}`, `visibility {}`,
    `repeat {axis, maxCount, spacing, firstMember}` and `childParameter {parameter}`.
    `axis` is `x`, `y` or `z`; `at` is in metres; `maxCount` is an integer from 1 to
    1,000.
- `componentParameterOverrides`: in ascending `instance` order, `{instance, values}`
  for each top-level placement with overrides. `values` is an object of 1–128 entries
  keyed by input parameter name: an SI number for a quantity, a boolean, or a choice
  value.
- `nextComponentDriverId`.

IDs, members, `firstMember` and the allocator are canonical nonzero decimal strings.
Numbers are SI in shortest round-trip form. `expression` and `spacing` are canonical
expression text (ADR 0165 §2); `bound` is 64 lowercase hexadecimal digits. Every field
is required and unknown fields are rejected at every level. A record must name a stored
definition, and overrides a stored top-level placement of a parametric definition.
Instance bindings (schema 26 `members`) list exactly the members the placement's
variant materializes, existing repeat copies included; no member body is stored.

On load the reader checks every parameter record as an edit would (names, labels,
types, bounds, canonical text, the acyclic graph, driver targets, unique driver IDs
below the allocator, the ADR 0165 limits), evaluates every placement once within the
document evaluation budget, and only then projects the placements from their variants.
Override values must be the canonical (quantized, in-range) values of input
parameters. The whole document then passes the ordinary restore validation. A file
that fails any check is refused before anything is published and is not modified.

**Writer policy** (extends decision 2 of ADR 0163): no sheets and no parameter records
→ schema 26 in the 1.0 shape; sheets without parameter records → schema 27 in the 1.1
shape; any parameter record → 28. Deleting every parameter record writes 27 or 26
again. The driver allocator is then not stored and restarts at 1 on reopen, which is
safe because overrides name parameters, not drivers. `--inspect-native` adds
`componentParameters` (parametric definitions), `componentDrivers` and
`componentParameterOverrides` (placements with overrides) counts and reports
`migrationNeeded: false` for a parameter-free schema-26 or schema-27 container.
Readers accept raw document versions 1–28; schemas 1–27 load with empty parameter
tables and driver allocator 1.

Component bundles ([ADR 0130](0130-contained-component-bundles.md)) capture the
parameter records of every captured definition and the source driver allocator; the
bundle's root placement must have no overrides. Insertion gives the drivers new IDs
above the destination's allocator, in source order, and keeps members, repeat blocks
and child parameters; a driver that was bound in the bundle is restamped over the
remapped records so it stays bound. Template bundles and instantiated templates keep
parameter records, overrides and the allocator. A bundle or template with parameters
holds a schema-28 model.

**Addendum: assembly identity (R094.b, 2026-10-16).** Schema 28 is still open (ADR 0165
open item 2), so this field is added without a schema 29. A `componentParameters`
record may carry one optional field, `assembly {id, version}`, naming the library
assembly the definition was built from ([ADR 0166](0166-configurable-component-interface.md)
§6.2). `id` matches `[a-z0-9_-]{1,64}(/[a-z0-9_-]{1,64})?` (for example
`sketchyup/interior-door`) and `version` is an integer from 1 to 2,147,483,647. It is
the only optional field in the tables: the writer omits it when the definition has no
identity, and a reader refuses `null`, a malformed `id` or `version`, or unknown fields
inside it. Component bundles capture it and insertion keeps it; making a placement
unique (including the ancestors `component.replace` makes unique) leaves it off the
copy, and removing a definition's last parameter and driver removes it with the
record. The `component-parameters-v28.sketchyup` fixture was regenerated once to carry
it on the room definition.

**Older readers.** SketchyUp 1.1.0 (package `sketchyup-1.1.0-1`) and 1.0.1
(`sketchyup-1.0.1-1`) refuse a schema-28 file at `decodeContainer`, before reading the
document, and leave it unchanged. Their manifest check sees the new allocator floor, so
`sketchyup-cli --validate-native` (and `--inspect-native`) exits 1 and writes exactly

```json
{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}
```

to stderr. Both open parameter-free saves from this build: 1.1.0 at schema 26 and 27,
1.0.1 at schema 26. Acceptance retains `release-1.1.0-v27.sketchyup` (unmodified 1.1.0
output) and the `component-parameters-v28.sketchyup` writer fixture. Package acceptance
keeps its generic rule (the prior package refuses, unchanged, every retained fixture
newer than it reads) and also passes the prior CLI to `component_parameter_io`.

## Schema 29: terrain and site origin (R095.a)

2026-10-18. The public schema advances to **sketchyup-document-v29** under the same
container version 2; see [terrain import and surfaces](0168-terrain-import-and-surfaces.md)
§2 and §4. The schema-29 document chunk uses `json-v29`, and `requiredFeatures` appends
`terrain-v1` after `component-parameters-v1`: the feature set is exactly the schema-28
set plus `terrain-v1`. A schema-29 root always carries the schema-27 and schema-28
tables and allocators, possibly empty, and adds two required fields:

- `site`: `null`, or `{easting, northing, elevation, crs, established {source, sha256}}`.
  The coordinates are metres within ±1e8; `crs` is an informational label of at most 64
  bytes without control characters; `source` is at most 256 bytes and `sha256` is empty
  or 64 lowercase hexadecimal digits.
- `terrainMesh`: `null` in a container (the mesh is its own chunk), or, in the inline raw
  JSON form, the base64 of the same bytes.

A body record may carry two new fields. A terrain has `kind: "terrain"` and `terrain
{sources, parameters, generator, provenance, meshDigest}`: `sources` is an array of
`{role, reference, points}` (`role` `contour`, `breakline`, `boundary`, `hole` or
`points`; `reference` at most 128 bytes; `points` `[x, y, z]` in the terrain frame; at
most 100,000 sources and 250,000 points in all), `parameters` is `{boundary,
maxEdgeLength, flipFlatTriangles, steinerFill}` (`boundary` `convex-hull`,
`max-edge-length` or `boundary`), `generator` is 1–64 bytes, `provenance` is `{sha256,
units, metresPerUnit}` and `meshDigest` is 64 lowercase hexadecimal digits. A terrain owns
no geometry, and its children are exactly its tiles. A tile is a `geometry` body with
`terrainTile: true` (no other value is accepted) whose `vertices`, `faces`, `edges` and
`edgeAppearances` are empty: the mesh is in the chunk. Every other body field (materials,
face paint, texture mappings, tag, visibility, lock, name, properties, allocators) is
stored as before. A terrain or tile cannot be a component definition member.

**Schema 29 addendum: terrain display (R095.e2).** 2026-10-19. Schema 29 and `terrain-v1`
had not shipped in a release (1.2.0 predates them), so the terrain record gains one optional
field without a new schema or feature: `display {contours, contourInterval, shading}`, each
member optional and written only when it differs from its default (`contours` false,
`contourInterval` 1 m, `shading` `material`; the other value is `elevation-bands`), and the
object written only when a member is. A record at its defaults therefore encodes exactly as
before, and existing schema-29 documents and digests are unchanged. `contourInterval` is 0.01 to
1,000 metres. Readers refuse unknown members, an empty `display` object and out-of-range values;
a default written explicitly is accepted. The settings are derived display: they are not part
of the mesh digest. See [ADR 0168](0168-terrain-import-and-surfaces.md#implementation-notes-r095e2).

**Schema 29 addendum: terrain editing (R096.a).** 2026-10-19. `terrain-v1` is amended in
place again (Schema 29 has still not shipped; owner decision 8 of
[ADR 0169](0169-terrain-editing-drape-stamp.md#7-persistence)); there is no schema 30 and no new
feature.

- Every source has a required `id`, an integer 1 to 2^32 − 1, unique within the terrain. A
  new terrain numbers its sources 1..n in their canonical stored order.
- The record has a required `tiling {origin: [x, y], cell}`: the minimum triangle centroid
  of the creation (or last Regenerate) mesh and the grid cell in metres, finite, origin within
  ±1e6 and `cell` above 0 and at most 4e6.
- The record has an optional `edits` array, written only when not empty (an empty array is
  refused): at most 4,096 entries in order, each `{id, kind, ...}` with `id` an integer 1 to
  2^32 − 1 unique in the list, `kind` at most 32 bytes, and the kind's own fields. The decoded
  entries are at most 8 MiB in all. A reader refuses a kind it does not know. The record's
  optional `nextEdit` (an integer 2 to 2^32 − 1, written when above its default 1) is the id the
  next entry takes, so ids are never reused; it must exceed every entry's id. R096.a defines
  no kind (its test-only kinds are not part of the format); R096.b–e add `sculpt`, `smooth`,
  `editSource`, `addSource`, `removeSource`, `stamp` and `drape` with their fields (R096.c's
  three, R096.b's two, R096.d's `stamp` and R096.e's `drape` are in the addenda below).
- A tile marker is `terrainTile: {key: [row, column, path, part]}` in place of `true`:
  `row` and `column` integers within ±2^53 (negative outside the original extent), `path` a
  string of at most 24 quadrant digits `0`–`3`, `part` an integer 0–9,999.
- `generator` is `cdt-2.0.0/terrain-v1+edit-v1` for terrains created or regenerated by this
  build; it stays a 1–64 byte label that readers do not interpret.
- `meshDigest` is the SHA-256 of the tile meshes in **tile-key order** (row, column, path,
  part), not body ID order. A terrain's tiles have distinct keys; two tiles of one terrain with
  one key are refused. For a terrain that was never edited the two orders coincide, so
  existing digests are unchanged.

The `terrain-mesh` chunk layout is unchanged: tiles still appear in strictly ascending body ID
order, which is a storage order only. A development document written before this addendum
(no source ids, no `tiling`, tiles marked `true`) still loads: the ids, the tiling and the keys
are derived from the stored mesh, and the document is saved in the new form. That loader branch
is removable before the F03 release. A terrain mixing the two forms is refused. See
[ADR 0169](0169-terrain-editing-drape-stamp.md#implementation-notes-r096a).

**Schema 29 addendum: source edits (R096.c).** 2026-10-20. The first production edit kinds of
the `edits` list ([ADR 0169](0169-terrain-editing-drape-stamp.md#implementation-notes-r096c)).
Each entry's fields after `id` and `kind` are exactly these (a reader refuses others):

- `editSource {source, role, points, before}`, `addSource {source, role, points}` and
  `removeSource {source, before}`.
- `source` is a source id, an integer 1 to 2^32 − 1. An added source's id must be the largest
  base source id plus the entry's own id (a reader refuses another), so it is above every base id
  and, because entry ids are never reused, never reused either. An entry naming a source that does
  not exist at its place in the list changes nothing and is skipped on replay with a notice.
- `role` is `contour`, `breakline` or `points`.
- `points` is a list of `[x, y, z]` in the terrain frame, finite and within ±1e6: 2 to 20,000
  points for a contour or breakline (one elevation for a contour; a closed polyline repeats its
  first point and has at least 4), 1 to 20,000 for points. They are the resolved geometry,
  exactly as the mesh holds them.
- `before` is the plan box `[minX, minY, maxX, maxY]` of the geometry the entry replaced or
  removed (finite, minimum first). It is for listing the edit; replay reads the replaced geometry
  from the current sources.

The base sources never change; the current sources are derived from them and the list. Source
coordinates are stored as +0.0 where an import produced −0.0, since JSON keeps no sign of zero.

**Schema 29 addendum: sculpt and smooth (R096.b).** 2026-10-20. Two more kinds
([ADR 0169](0169-terrain-editing-drape-stamp.md#implementation-notes-r096b)), each with exactly
these fields after `id` and `kind` (a reader refuses others):

- `sculpt {path, radius, falloff, amount, pieces?}` or
  `sculpt {region, falloffWidth, falloff, amount, pieces?}`; `smooth` has the same scope fields
  with `strength` and `iterations` in place of `amount`.
- `path` is a brush stroke of 1 to 256 plan points `[x, y]` in the terrain frame, finite and
  within ±1e6; `radius` is 0.01 to 1,000.
- `region` is a closed polygon of 3 to 20,000 such points (the closing edge implied, no point
  repeating the one before it, not all on one line, and at most 512 edges meeting any of its
  min(4,096, corners) equal horizontal bands); `falloffWidth` is 0 to 1,000.
- `falloff` is `smooth`, `linear` or `constant`.
- `amount` is a non-zero height of at most 1,000 in magnitude; `strength` is above 0 and at most
  1; `iterations` is an integer 1 to 8.

- `pieces` (written when not empty) is a list of 1 to 4,096 pairs `[source, piece]`: the
  further runs of the source lines the entry split when it was made, in order, `source` the line's
  id and `piece` a distinct id from the entry's own block (below), which a reader checks.

An entry stores its parameters and, as resolved geometry, its pieces' ids: the vertices it moves
and the source vertices it clips from the current sources are derived from the mesh and the
current sources at its place in the list. A source line a sculpt splits keeps its id for its
first remaining run; each further run takes the ids recorded for that line in order, and any run
beyond them the next unrecorded id of the entry's block, 2^32 − 1 − (entry id − 1) × 4,096 − n
(n = 0 to 4,095; entries up to 2^18), so these ids are never reused either. Base and added source
ids stay below 3,221,225,472 (a reader refuses a base id at or above it).

**Schema 29 addendum: building-pad stamp (R096.d).** 2026-10-20. One more kind
([ADR 0169](0169-terrain-editing-drape-stamp.md#implementation-notes-r096d)), with exactly these
fields after `id` and `kind` (a reader refuses others):

- `stamp {footprint, elevation, mode: "daylight", cutSlope, fillSlope, maxDistance, sourceBody?,
  sourceFace?, outline?, rays?, dropped?, pieces?}` or the same with `mode: "width"` and `width`
  in place of the two slopes and the distance.
- `footprint` is the pad's polygon: 3 to 20,000 plan points `[x, y]` in the terrain frame, finite
  and within ±1e6, the closing edge implied, no point repeating the one before it, running
  counter-clockwise around a positive area (a binary64 sum in point order), with at most 512
  edges meeting any of its min(4,096, corners) equal horizontal bands. `elevation` is the pad's
  Z in the terrain frame, finite and within ±1e6.
- `cutSlope` and `fillSlope` are rise over run, 0.01 to 100; `maxDistance` is above 0 and at most
  1,000; `width` is 0.01 to 1,000.
- `sourceBody` and `sourceFace` (decimal ID strings) name the body (and face) the footprint was
  read from, for Restamp only; a face needs its body. Nothing follows the body (decision 4).
- `outline` and `rays` are the resolved geometry, both present when the pad applied and both
  absent when it did not (a replay that skipped it clears them): `outline` is 3 to 52,768 plan
  points, the footprint with the start points of the edge rays, at `elevation`; `rays` is 3 to
  32,768 entries `[start, x, y, z]` in order around the outline, `start` an index into `outline`
  (never decreasing) and `[x, y, z]` the ray's daylight point (the start point at `elevation` where
  the pad meets the ground). The daylight line is those points in order, a point repeating the one
  before it (or the first) once.
- `dropped` (written when above 0) is how many rays the self-intersection repair dropped, an
  integer 1 to 131,072; only with resolved geometry.
- `pieces` (written when not empty) is as for `sculpt`, with ids from the entry's block from its
  third id on: the first two, 2^32 − 1 − (entry id − 1) × 4,096 and that minus 1, name the pad's
  outline and daylight line, which join the current sources as closed breaklines.

A pad's resolved geometry is recomputed whenever it applies, so a replay against a changed base
(Terrain settings) stores the new daylight line; the derived current sources and the pads other
edits must not enter are read from it.

**Schema 29 addendum: drape (R096.e).** 2026-10-20. One more kind
([ADR 0169](0169-terrain-editing-drape-stamp.md#implementation-notes-r096e)), with exactly these
fields after `id` and `kind` (a reader refuses others):

- `drape {lines, paint?, runs?, splits?}`. Only imprints are recorded; an overlay changes no
  terrain record.
- `lines` is 1 to 4,096 objects `{points, closed?}`: `points` 2 or more plan points `[x, y]` in the
  terrain frame (3 or more when closed), finite and within ±1e6, no point repeating the one before
  it, 20,000 points in all; `closed` is written only when true, and a closed line does not repeat
  its first point. These are the edge chains and face loops as projected (their own Z is not
  stored).
- `paint` (written when not empty) is 1 to 1,024 regions `{loops, material}`: `loops` one or more
  closed loops of 3 or more such points (20,000 in all, a region's outer loop and its holes,
  even-odd), each with at most 512 edges in any of its min(4,096, corners) equal horizontal bands;
  `material` a decimal material ID string, which document validation requires to exist (so a
  material a region names cannot be deleted). Over a whole terrain the loops' busiest bands sum
  to at most 4,096 edges.
- `runs` and `splits` are the resolved geometry, absent when the drape did not apply (a replay
  that skipped it clears them). `runs` is 1 to 4,096 polylines of 2 or more `[x, y, z]` points
  (65,536 in all), as the mesh holds them; a closed run repeats its first point. They join the
  current sources as breaklines with the ids 2^32 − 1 − (entry id − 1) × 4,096 − n for run n
  (the entry's block, as `sculpt` pieces). `splits` (written when not empty, only with `runs`) is
  1 to 65,536 objects `{a, b, points}`: an existing contour, breakline or pad-ray segment
  between the plan points `a` and `b` (`a` lexicographically smaller, each segment once) that new
  vertices split, and those vertices `[x, y, z]` in order from `a` (65,536 in all); the current
  sources holding the segment gain them, and a pad ray split this way is superseded.

A drape's resolved geometry is recomputed whenever it applies, so a replay against a changed base
stores the new runs; its paint regions apply only while it is applied. Every triangle of the
terrain whose centroid lies in a region (the last region wins) has that material on both sides
as per-face paint of its tile; this is derived from the record on every rebuild and is not a
separate field.

**The `terrain-mesh` chunk.** It follows the asset chunks, at most once and only with
`terrain-v1`: `{kind: "terrain-mesh", encoding: "f64-u32-le-v1", offset, bytes,
sha256}`, with the SHA-256 of its bytes in the manifest as for assets. Its bytes, all
little-endian, are `SKTMESH1`, a u32 tile count, then per tile `{u64 body ID, u32 vertex
count, u32 triangle count}`, then per tile every vertex as three f64 (`x, y, z`, in the
terrain frame) and every triangle as three u32 vertex indices. Tiles appear in strictly
ascending body ID order and must be exactly the bodies marked `terrainTile`. Vertex and
face IDs are implicit: vertex *i* has ID *i* + 1 and triangle *j* has face ID *n* + *j* +
1 for *n* vertices; edge IDs come from `Topology::rebuild`, and every interior edge
(two faces) is soft and smooth. Limits, checked from the header before any tile is
allocated: 1–10,000 tiles; 1–2,048 triangles and 3 to 3× as many vertices per tile;
250,000 triangles in the document (the terrain limit class, apart from the modelling
limits); and an exact length. The chunk is at most 21,160,012 bytes, and
`containerBytes` grows by that bound. Indices must be in range and distinct within a
triangle, and coordinates finite within ±1e6.

**Writer policy** (extends decision 2 of ADR 0163 and the schema-28 policy): a document
with a terrain or a site origin is written as 29; any other document keeps 28, 27 or 26
exactly as before, byte for byte. Exploding the last terrain (with no site origin) writes
the lower schema again. Readers accept raw document versions 1–29; schemas 1–28 load with
no site origin and refuse terrain fields.

**Older readers.** SketchyUp 1.2.0 (package `sketchyup-1.2.0-1`) and 1.1.0 refuse a
schema-29 file at `decodeContainer`, before reading the document, and leave it
unchanged: `sketchyup-cli --validate-native` exits 1 and writes exactly

```json
{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}
```

to stderr. Package acceptance also passes its prior CLI (the pinned 1.1.1 package) to
`terrain_io`, which checks the same refusal.

**Schema 30 addendum: camera paths and render presets (R099.a).** 2026-10-25. The public
schema advances to **sketchyup-document-v30** under container version 2; see
[camera paths and batch output](0173-camera-paths-batch-output.md) §2 and §3. The schema-30
document chunk uses `json-v30`, and `requiredFeatures` appends `presentation-v1` after
`terrain-v1`. The root adds one required object, `presentation`:

- `cameraPaths`: an array of at most 64 records `{id, name, position, keyframes, timing}`.
  `id` is a canonical nonzero decimal string, `name` follows the saved-scene rule (1 to
  1,024 bytes, no edge spaces, no control characters, unique), `position` is unique and
  below the count, `keyframes` is 2 to 32 scene identities, and `timing` is
  `{framesPerSecond 1–60, transitionSteps 1–600, holdSteps 0–600}`. The planned frame count
  `1 + N·hold + (N−1)·transition` is at most 240.
- `renderPresets`: an array of at most 16 records `{id, name, settings}`, where `settings` is
  `{width, height, samples, seed, engine}` with the `RenderSettings` bounds and `engine`
  `cycles` or `eevee`.
- `nextCameraPathId` and `nextRenderPresetId`: canonical nonzero decimal strings.

A keyframe that names no scene is not a decode error: the path loads broken. A schema-29
root refuses the object, and a schema-31 root refuses with the newer-format error. A document
without camera paths or presets still writes its lower schema.
