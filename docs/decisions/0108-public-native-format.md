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
and `chunks`. The current writer identifies itself as `SketchyUp/0.1.0`; readers
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
