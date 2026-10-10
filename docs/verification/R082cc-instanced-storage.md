# R082.cc — Instanced component storage (schema 25)

2026-10-10. First of the R082 instancing layers. This layer changes persistence only;
see [ADR 0159](../decisions/0159-instanced-component-storage.md) and the appended
schema-25 section of the [public native format](../decisions/0108-public-native-format.md).

## What changed

- Native model JSON advances from schema 24 to 25. Component placement members
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
- Schemas 8–24 still load. Before restore, every stored member and top-level
  placement root is compared with its projection by `matchesComponentProjection`
  (the existing canonical rule, now shared). A mismatch rejects with, for example,
  `Stored member 2 of component instance 1 differs from its definition 1 projection`;
  nothing is written and the source is unchanged.
- Container v2: chunk encoding `json-v25`; required feature
  `instanced-placements-v1` appended, so older readers reject. Manifest allocator
  floors still list every resolved scene record.
- `docs/api/native-format-v1.json` was regenerated from
  `sketchyup-cli --format-capabilities` (schema `sketchyup-document-v25`, read
  versions 1–25). `scripts/generate-tool-reference.py --check` reports that both
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

| 1,000 placements | Schema 24 (main `8534bb67`) | Schema 25 | Ratio |
| --- | ---: | ---: | ---: |
| Model JSON | 6,975,132 B | 538,239 B | 12.96× smaller |
| Container | 7,021,923 B | 585,055 B | 12.00× smaller |
| Encode process peak (build + encode) | 128,328 KiB | 45,384 KiB | 2.83× |
| Load process peak (read + decode, 3 runs) | 124,636–125,520 KiB | 44,912–45,020 KiB | 2.78× |
| Decode time (3 runs) | 1,300–1,542 ms | 769–826 ms | |
| Encode time | 448–872 ms | 35–44 ms | |

Building the 1,000-placement document alone peaks at about 37 MB in both versions.

10,000 placements cannot be decoded today: they expand to 20,000 bodies and
520,000 vertices, above `DocumentLimits` (10,000 bodies, 100,000 vertices), and this
layer does not change limits. The model JSON size was instead measured with a
test-only path (`--measure 1000 replicate`) that replicates the last placement's
stored records to 10,000 placements with fresh identities, without decoding:

| 10,000 placements (replicated, not decoded) | Schema 24 | Schema 25 |
| --- | ---: | ---: |
| Model JSON | 69,769,642 B (2.08× the 32 MiB bound) | 5,380,748 B (16% of the bound) |

The schema-24 figure reproduces the 69.7 MB measured on main. The v25 container
manifest at 10,000 placements is extrapolated, not measured: about 47 KB of
per-body floors per 1,000 placements, so roughly 0.47 MB, under the 1 MiB manifest
limit. Encode/decode peaks at 10,000 were not measured; R082.dd–gg must measure
them once in-memory instancing and limits allow such a document.

## Tests

New `instanced_storage` suite (`tests/instanced_storage_tests.cpp`):

- Round trips of 0 placements (empty and geometry-only), 1 placement, 1,000
  placements and a mixed document (shared, hidden, tagged, locked and mirrored with
  properties, a nested assembly with a second placement, and a make-unique
  sibling). Each writes schema 25 with no member bodies, reopens byte-exact, and
  matches every body (including allocator floors), member map, `nextId`, revision,
  canonical container and clean history, through both container and raw JSON.
- Immutable save snapshot and recovery checkpoint round trips.
- Retained `tests/fixtures/instances-v24.sketchyup` (unmodified v24 writer output
  from main, picked up by `native_format_tests` and
  `scripts/verify-installed-migrations.py` through their fixture globs) migrates
  exactly: every expanded v24 body record is rebuilt identically, and the original
  file is unchanged.
- A corrupted copy (one member vertex moved) is rejected by `loadDocument`,
  `inspectNativeFile`, `migrateNativeFile` and raw decoding with the bounded
  diagnostic naming instance and definition; the file is unchanged and no output
  is created. A member whose floor is below its definition's is rejected the same way.
- A raised member floor survives v25 as exactly one floor record.
- Strict v25: missing or malformed `floors`, a floor on the root or not above the
  definition floor, an incomplete member map, a dangling definition, a stored
  projected member, a v25 chunk without `instanced-placements-v1`, and a v25 tree
  labelled `json-v24` are all rejected.
- 1,000 placements must save below 1.5 MB (measured 585,055 B).

Updated expectations (format-required only): schema-version assertions 24→25 in
eleven I/O suites; the future-version probe in `native_format_tests` is now
`nativeDocumentVersion + 1`; downgrade helpers in `component_glue_io_tests` and
`hosted_components_io_tests` drop `instanced-placements-v1` and re-insert expanded
members when building older documents; the texture-mirror v16 migration expectation
in `model_style_io_tests` omits member bodies and adds empty `floors`.

The schema bump changes the container bytes of the eight deterministic provider
trial inputs, so their `inputSha256` values in `tests/provider-release-corpus.json`
were regenerated with `provider_trial --describe`; every prompt and `promptSha256`
is unchanged.

Commands and results:

```sh
cmake --preset dev && cmake --build --preset dev --parallel 3     # no warnings
ctest --preset dev -j3                     # 183 tests: 171 passed, 11 opt-in *_real skipped,
                                           # provider_release_corpus failed before its hash update;
ctest --preset dev -R provider_release     # then 2/2 passed
python3 scripts/generate-tool-reference.py --cli build/dev/sketchyup-cli --root . --check
                                           # 2 generated reference artifacts match
python3 scripts/verify-installed-migrations.py --cli build/dev/sketchyup-cli \
    --fixtures tests/fixtures --output OUT  # 26 historical fixtures migrated; all checks passed
cmake --preset sanitize -DSKETCHYUP_BUILD_CLI=ON
cmake --build --preset sanitize --parallel 3 --target <31 suites below> sketchyup-cli
ctest --test-dir build/sanitize -j3 -R '^(reference_image|solar|hosted_components|component_glue|
  component_placement|components|component_records|reference_image_commands|reference_image_io|
  solar_commands|solar_io|text_source_io|native_format|face_textures|mcp|automation_session|
  transaction_dispatch|component_scope|hosted_components_io|component_glue_io|instanced_storage|
  annotations_io|sections_io|saved_scenes_io|model_style_io|document_units_io|recovery_cli|
  recovery|m4_persistence|asset_io|persistence)$'   # 31/31 passed under ASan/UBSan
```

## Remaining

- R082.dd: shared in-memory member storage and viewport instancing.
- R082.ee: inference over instanced placements.
- R082.ff: placement construction without per-instance expansion.
- R082.gg: public limits for the 10,000-placement fixture, with measured
  encode/decode peaks at that size.
