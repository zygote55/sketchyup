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
