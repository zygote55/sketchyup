# R082.cc — Instanced component storage (schema 26)

2026-10-10. First of the R082 instancing layers. This layer changes persistence only;
see [ADR 0159](../decisions/0159-instanced-component-storage.md) and the appended
schema-26 section of the [public native format](../decisions/0108-public-native-format.md).

## What changed

- Native model JSON advances from schema 25 (R084.r display precision, merged first)
  to 26. Component placement members
  (every scene record a placement binds, other than that placement's own root) are
  no longer written to `bodies`. Top-level placement roots and all non-component
  records are still written in full.
- Instance records keep `root`, `definition` and the explicit member identity map,
  and add a required `floors` object. It records `[nextId, nextEdgeId]` only for a
  member whose scene allocator floors exceed its definition's; otherwise it is `{}`.
- Load rebuilds members with `projectComponentPlacement`, which runs the editor's
  own `Projection::resolve` over the stored bindings. It validates definitions
  first, bounds the expansion by the document editing limits before allocating,
  never allocates identities, and rejects stored members, colliding records,
  incomplete or mismatched bindings and non-canonical floor records.
- Schemas 8–25 still load. Before restore, every stored member and top-level
  placement root is compared with its projection by `matchesComponentProjection`
  (the existing canonical rule, now shared). A mismatch rejects with, for example,
  `Stored member 2 of component instance 1 differs from its definition 1 projection`;
  nothing is written and the source is unchanged.
- Container v2: chunk encoding `json-v26`; required feature
  `instanced-placements-v1` appended after `display-precision-v1`, so a v26 file
  requires both and older readers reject. Manifest allocator
  floors still list every resolved scene record.
- `docs/api/native-format-v1.json` was regenerated from
  `sketchyup-cli --format-capabilities` (schema `sketchyup-document-v26`, read
  versions 1–26). `scripts/generate-tool-reference.py --check` reports that both
  generated tool-reference artifacts still match.

In-memory expansion, viewport, inference, construction and all limits are
unchanged. IDs, geometry, allocator floors, revision and the clean history
baseline are identical after reload (asserted record by record and by canonical
container bytes).

## Sizes and peaks

Benchmark definition: the `real_model_benchmark` repeated 26-sided prism
(2 bodies, 100 triangles per placement). Debug build (`build/dev`), Qt 6 / GCC on the
development machine while other builds ran; times are indicative only. Peaks are
whole-process `ru_maxrss` in separate processes
(`instanced_storage_tests --measure 1000 encode OUT` and `--measure-load FILE`).

| 1,000 placements | Schema 24 (main `8534bb67`) | Schema 25 (main `44eca11e`) | Schema 26 | v25 → v26 |
| --- | ---: | ---: | ---: | ---: |
| Model JSON | 6,975,132 B | 6,975,154 B | 538,261 B | 12.96× smaller |
| Container | 7,021,923 B | 7,021,968 B | 585,100 B | 12.00× smaller |
| Encode process peak (build + encode) | 128,328 KiB | 129,280 KiB | 46,192 KiB | 2.80× |
| Load process peak (read + decode, 3 runs) | 124,636–125,520 KiB | 125,608–126,504 KiB | 44,728–45,120 KiB | 2.80× |
| Decode time (3 runs) | 1,300–1,542 ms | 1,387–1,409 ms | 775–782 ms | |
| Encode time | 448–872 ms | 449 ms | 35 ms | |

Schema 25 differs from 24 only by the `displayPrecision` field (22 bytes). Its
column was measured by building the same test against `origin/main` at `44eca11e`;
its load peaks reopen that build's 1,000-placement container with the schema-26
reader (members are verified against their projections, then kept). Building the
1,000-placement document alone peaks at about 37 MB in every version.

10,000 placements cannot be decoded today: they expand to 20,000 bodies and
520,000 vertices, above `DocumentLimits` (10,000 bodies, 100,000 vertices), and this
layer does not change limits. The model JSON size was instead measured with a
test-only path (`--measure 1000 replicate`) that replicates the last placement's
stored records to 10,000 placements with fresh identities, without decoding:

| 10,000 placements (replicated, not decoded) | Schema 24 | Schema 25 | Schema 26 |
| --- | ---: | ---: | ---: |
| Model JSON | 69,769,642 B | 69,769,664 B (2.08× the 32 MiB bound) | 5,380,770 B (16% of the bound) |

The schema-24 figure reproduces the 69.7 MB measured on main. The v26 container
manifest at 10,000 placements is extrapolated, not measured: about 47 KB of
per-body floors per 1,000 placements, so roughly 0.47 MB, under the 1 MiB manifest
limit. Encode/decode peaks at 10,000 were not measured; R082.dd–gg must measure
them once in-memory instancing and limits allow such a document.

## Tests

New `instanced_storage` suite (`tests/instanced_storage_tests.cpp`):

- Round trips of 0 placements (empty and geometry-only), 1 placement, 1,000
  placements and a mixed document (shared, hidden, tagged, locked and mirrored with
  properties, a nested assembly with a second placement, and a make-unique
  sibling). Each writes schema 26 with no member bodies, reopens byte-exact, and
  matches every body (including allocator floors), member map, `nextId`, revision,
  canonical container and clean history, through both container and raw JSON.
- Immutable save snapshot and recovery checkpoint round trips.
- Retained `tests/fixtures/instances-v24.sketchyup` and `instances-v25.sketchyup`
  (unmodified v24 and v25 writer output from main at `8534bb67` and `44eca11e`,
  picked up by `native_format_tests`, `display_precision_io_tests` and
  `scripts/verify-installed-migrations.py` through their fixture globs) migrate
  exactly: every expanded body record is rebuilt identically, and the originals
  are unchanged.
- For each golden file, a corrupted copy (one member vertex moved) is rejected by `loadDocument`,
  `inspectNativeFile`, `migrateNativeFile` and raw decoding with the bounded
  diagnostic naming instance and definition; the file is unchanged and no output
  is created. A member whose floor is below its definition's is rejected the same way.
- A raised member floor survives schema 26 as exactly one floor record.
- Strict v26: missing or malformed `floors`, a floor on the root or not above the
  definition floor, an incomplete member map, a dangling definition, a stored
  projected member, a v26 chunk without `instanced-placements-v1`, and a v25 tree
  inside a `json-v26` chunk are all rejected.
- 1,000 placements must save below 1.5 MB (measured 585,100 B).

Updated expectations (format-required only): current-schema assertions now say 26
in eleven I/O suites (they said 25 after R084.r); `native_format_tests` uses
`nativeDocumentVersion` for the migrated version, the raw-version loop and the
future-version probe; downgrade helpers in `component_glue_io_tests` and
`hosted_components_io_tests` drop `instanced-placements-v1` and re-insert expanded
members when building older documents; the texture-mirror v16 migration expectation
in `model_style_io_tests` omits member bodies and adds empty `floors`;
`display_precision_io_tests` now treats `display-precision-v25.sketchyup` as a
historical file (it checks the stored `json-v25` encoding and an exact migration
instead of byte-identical re-encoding) and expects the current encoding for new
containers.

The schema bump changes the container bytes of the eight deterministic provider
trial inputs, so their `inputSha256` values in `tests/provider-release-corpus.json`
were regenerated with `provider_trial --describe` on top of R084.r's values; every
prompt and `promptSha256` is unchanged.

Commands and results (after merging `origin/main` at `44eca11e`):

```sh
cmake --preset dev && cmake --build --preset dev --parallel 3     # no compiler warnings
ctest --preset dev -j3                     # 185 tests: 174 passed, 11 opt-in *_real skipped
python3 scripts/generate-tool-reference.py --cli build/dev/sketchyup-cli --root . --check
                                           # 2 generated reference artifacts match
python3 scripts/verify-installed-migrations.py --cli build/dev/sketchyup-cli \
    --fixtures tests/fixtures --output OUT  # 28 historical fixtures migrated; all checks passed
cmake --preset sanitize -DSKETCHYUP_BUILD_CLI=ON
cmake --build --preset sanitize --parallel 3 --target <32 suites below> sketchyup-cli
ctest --test-dir build/sanitize -j3 -R '^(reference_image|solar|hosted_components|component_glue|
  component_placement|components|component_records|reference_image_commands|reference_image_io|
  solar_commands|solar_io|text_source_io|native_format|face_textures|mcp|automation_session|
  transaction_dispatch|component_scope|hosted_components_io|component_glue_io|instanced_storage|
  annotations_io|sections_io|saved_scenes_io|model_style_io|document_units_io|recovery_cli|
  recovery|m4_persistence|asset_io|persistence|display_precision_io)$'   # 32/32 passed under ASan/UBSan
```

## Remaining

- R082.dd: shared in-memory member storage and viewport instancing.
- R082.ee: inference over instanced placements.
- R082.ff: placement construction without per-instance expansion.
- R082.gg: public limits for the 10,000-placement fixture, with measured
  encode/decode peaks at that size.
