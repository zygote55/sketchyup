# Release versioning

Date: 2026-10-13. Prepares the release-candidate freeze in
[M9 acceptance](../M9_ACCEPTANCE.md) (roadmap [R089](../PR_ROADMAP.md#r089)).

## Decision

**1.0.0 is the first release.** Every earlier build was the `0.1.0` development
version; Arch packages `0.1.0-1` through `0.1.0-3` were development packages, not
releases. The release candidate is version 1.0.0, Arch package `1.0.0-1`.

**One source of truth.** `project(SketchyUp VERSION x.y.z)` in `CMakeLists.txt` is the
version. CMake generates `sketchyup/version.hpp` (`SKETCHYUP_VERSION`,
`SKETCHYUP_WRITER`) in the build tree; C++ code and tests use those macros and never
spell the number. `packaging/arch/PKGBUILD` must state the same `pkgver`;
`scripts/release-version.sh` prints the version and fails when the two differ, and
`scripts/package-source.sh` and `scripts/release-artifacts.sh` call it. Package
acceptance checks the archived `CMakeLists.txt` against `pkgver` again.

**Reported version.** `sketchyup-cli --version` and `sketchyup --version` print it, and
the MCP server info reports it.

**Semantic versioning intent.** After 1.0.0, a major version may drop reading support
for a native format or break a versioned public API (MCP, recipe, session,
inspection, extension and tool-reference schemas in `docs/api`). A minor version may
add commands, schemas or format features; a newer minor may write documents that
older readers reject through `requiredFeatures`, as [ADR 0108](0108-public-native-format.md)
specifies, but it keeps reading every format the previous minor read. A patch version
fixes defects without changing schemas or the written format. The native format and
API schemas keep their own version numbers; the product version does not replace them.

**pkgrel policy.** `pkgrel` starts at 1 for each new `pkgver` and increases only for
packaging-only changes built from the same source version. Package acceptance builds
the previous package from the same source and upgrades from it: `pkgrel - 1` of the same
version; for the first `pkgrel` of a new version, the newest earlier release in the
`released_packages` table of `packaging/arch/verify-package.sh` (so `1.0.0-1` upgraded from
the development package `0.1.0-3`, `1.0.1-1` from `1.0.0-1`, and `1.1.0-1` from `1.0.1-1`).
pacman must order each release after its predecessor. Separately, the published package of
the previous release is downloaded with a pinned SHA-256, and its CLI must refuse newer
document formats unchanged and accept what the new release writes for older content
(added for 1.1.0).

**Writer string.** The native container manifest's `writer` field is
`SketchyUp/<version>` (`SKETCHYUP_WRITER`), so saves from 1.0.0 write
`SketchyUp/1.0.0` and a version change changes saved bytes. The field stays
informational: readers accept any writer string, and retained fixtures that record
`SketchyUp/0.1.0` are not rewritten. Tests derive the expected writer from the macro.

## Consequences

- Changing the version is a two-line change (CMake and `pkgver`) plus `pkgrel=1`.
- `scripts/release-artifacts.sh` produces the source archive, `SHA256SUMS`,
  `THIRD_PARTY_NOTICES` and `MANIFEST.json` from a clean checkout; packages still
  come from the pinned container. It creates no tags or releases.
- Historical evidence records keep the package versions they verified.
- The frozen provider corpus pins `inputSha256` over generated container bytes, so a
  version change changes those eight values. They are regenerated with
  `provider_trial --describe`, prompts unchanged, as R082.cc and R084.r did for
  schema changes.

## Implementation notes (1.4.0 release preparation)

- The prior release for the 1.4.0-1 package is the published `sketchyup-1.3.0-1-x86_64.pkg.tar.zst`
  (SHA-256 `5f5c0cf8…a833b`, the `gh release view v1.3.0` digest). Downloaded into its own directory and
  unpacked without pacman, its `sketchyup-cli --format-capabilities` reports `documentVersion` 29 and
  `readDocumentVersions` 1 through 29, so it refuses schemas 30 to 33 with "Unsupported container field"
  and leaves each file unchanged.
- Each prior-release test was run locally against that CLI (`SKETCHYUP_PRIOR_CLI`): `component_parameter_io_tests`
  (899 checks), `drawing_sheet_io_tests`, `geographic_site_tests`, `material_texture_size_tests`,
  `presentation_io_tests`, `style_bundle_tests` and `terrain_io_tests` all pass. `terrain_io` now expects the
  schema-29 file to open, because 1.3.0 is the first release to read schema 29.
- The writer string is part of the fixture bytes. Bumping it to `SketchyUp/1.4.0` changed the provider
  corpus `inputSha256` values (all eight tasks in each of six manifests, regenerated with
  `provider_trial --describe`) and the bytes of the two schema fixtures whose reopen test compares the file
  itself (`geographic-site-v32`, `material-texture-size-v33`, regenerated with `--write-fixture`).
  The geographic fixture's document identity also changed, because that test does not fix it.
- The IFC export goldens (`tests/fixtures/ifc/export/*.ifc`, compared byte for byte by `ifc_export_tests`)
  carry the application version twice: in `FILE_NAME`'s originating system and in `IFCAPPLICATION`. The six
  files were moved to 1.4.0 on those two lines only, after the first CI run on this branch failed G6. The
  Python worker tests build their own package with version 1.3.0 and are unaffected. A later bump must move
  these lines too.
- The catalog fixtures (`tests/fixtures/catalog/*.sketchycat`) embed native documents, so they carry the
  writer string and the chunk digests over it; `catalog_tests` compares them with its generator byte for
  byte. They were regenerated with `catalog_tests --write-fixtures <new empty directory>` after the
  package and development jobs failed `catalog`. Only the writer string and the digests changed. The
  `catalog`, `catalog_fuzz`, `catalog_import` and `style_catalog_cli` tests pass on them.
- Contradiction, not changed: `tests/provider_release_corpus_tests.py` says in its header that v5 is
  authoritative, but its `CURRENT` manifest is v6 (added by R099.c). The header is stale.

## Implementation notes (1.5.0 release preparation)

- The prior release for the 1.5.0-1 package is the published `sketchyup-1.4.0-1-x86_64.pkg.tar.zst`
  (SHA-256 `0d3dd1503fae3ef372956bd40f6d8d0a176d813b4bd8e71595cc978048c4a3d7`, checked against the
  `gh release download v1.4.0` asset). Unpacked into a private directory without pacman, its
  `sketchyup-cli --format-capabilities` reports `documentVersion` 33 and `readDocumentVersions` 1 through 33.
  It refuses the schema-34 GeoTIFF terrain with "Invalid required features" and leaves the file unchanged.
- The split package's prior release needs no pin. `verify-package.sh` unpacks only the `sketchyup` package, and
  it rebuilds the preceding version from the same source. No script reads the published `sketchyup-ifc-1.4.0-1`
  package; its SHA-256 (`2a78005e…4d344e622`) was checked against the release asset anyway.
- The `released_packages` table and `known_release_sha256` gained 1.4.0-1. Both the 1.4.0 and 1.5.0 entries are
  now in the table, so `vercmp` orders the pair and 1.5.0-1 upgrades from 1.4.0-1.
- Prior-release tests (`SKETCHYUP_PRIOR_CLI`). The 1.3.0 reader refused schemas 30 to 33, but the 1.4.0 reader
  opens them. `presentation_io`, `style_bundle`, `geographic_site` and `material_texture_size` therefore check that
  the released reader opens their schema-30 to 33 fixture unchanged. `component_parameter_io` accepts a prior that
  reads 26 to 33. `terrain_io` checks that the released reader opens the schema-29 file and refuses the
  schema-34 file `tests/fixtures/terrain-dem-v34.sketchyup` unchanged; that file is a GeoTIFF terrain
  (R107.b1), written by `terrain_import_tests --write-fixture` from the synthetic
  `geotiff/plane-f32-strip-none-le.tif`. This build reads it back bit for bit.
- The writer string is part of the fixture bytes. Bumping it to `SketchyUp/1.5.0` changed `geographic-site-v32`
  and `material-texture-size-v33` (regenerated with `--write-fixture`), the five catalog fixtures (regenerated with
  `catalog_tests --write-fixtures` into a new directory; only the writer string and the digests changed), and the
  eight `inputSha256` values in each of the six corpus manifests (`provider_trial --describe gpt-6-astra` for each
  task, twice, prompts unchanged).
- The IFC export goldens (`tests/fixtures/ifc/export/*.ifc`) changed on their `FILE_NAME` and `IFCAPPLICATION`
  lines only, regenerated with `IFC_UPDATE_GOLDENS=1 ifc_export_tests`. The helper was built against the
  IfcOpenShell 0.9.0 prefix that an earlier build made from the same vendored commit (`7ba70197`), mounted
  read-only, not rebuilt. CI builds its own prefix.
- The corpus stays at v6. The assistant's tool surface did not change since 1.4.0: the baseline file was last
  re-pinned in `cb7a536a` (R099.c), before 1.4.0, and `assistant_tool_surface_tests` passes against it.
- Contradiction, not changed: `tests/provider_release_corpus_tests.py` still says in its header that v5 is
  authoritative, while its `CURRENT` manifest is v6 (see the 1.4.0 notes above).

## Implementation notes (1.6.0 release preparation)

- The prior release for the 1.6.0-1 package is the published `sketchyup-1.5.0-1-x86_64.pkg.tar.zst` (SHA-256
  `92c36696e47eddfc5d3fba2d98f8dc56405f737779bc4fafe78f43b4951dd668`, checked against the `gh release download v1.5.0`
  asset and its `SHA256SUMS`). Unpacked into a private directory without pacman, its `sketchyup-cli --format-capabilities`
  reports `documentVersion` 34 and `readDocumentVersions` 1 through 34. It refuses the schema-35 reference image
  `tests/fixtures/reference-geo-v35.sketchyup` with `{"code":"INVALID_OPERATION","error":"Invalid required features","status":"failed"}`,
  exit status 1, and leaves the file unchanged. It opens the schema-34 terrain `terrain-dem-v34.sketchyup` with exit status 0. The
  text is the one the package acceptance's `.status == "failed"` check reads; it was not derived from the source.
- The split package's prior release needs no pin in `verify-package.sh`, as in 1.5.0. The script unpacks only the `sketchyup`
  package. The published `sketchyup-ifc-1.5.0-1` (SHA-256 `5378248f…5b82`) was checked against `SHA256SUMS`, and no script reads it.
- The `released_packages` table and `known_release_sha256` gained 1.5.0-1. `vercmp` orders the table, and 1.6.0-1 upgrades from 1.5.0-1.
- Prior-release tests (`SKETCHYUP_PRIOR_CLI`). The 1.5.0 reader opens schema 34, so `terrain_io` now checks that the released reader
  opens `terrain-dem-v34.sketchyup` unchanged; it had checked that 1.4.0 refused it. `component_parameter_io` accepts a prior that
  reads 26 to 34. `native_format` already requires the released reader to refuse the schema-35 file (R107.b2.1), and that check
  now runs against 1.5.0.
- The writer string is part of the fixture bytes. Bumping it to `SketchyUp/1.6.0` changed these fixtures, each regenerated with its
  generator into a new directory, compared, and copied in:
  - `geographic-site-v32.sketchyup` (`geographic_site_tests --write-fixture`) and `terrain-dem-v34.sketchyup`
    (`terrain_import_tests --write-fixture`): the writer, and the document identifier, which the generators create anew;
  - `material-texture-size-v33.sketchyup` (`material_texture_size_tests --write-fixture`): one byte, the writer; its identifier is fixed;
  - `reference-geo-v35.sketchyup` (`sketchyup-cli --import-reference-image tests/fixtures/reference/north-up.png --reference-zone 30N
    --output`): the writer, and the identifier;
  - the five catalog fixtures (`catalog_tests --write-fixtures` into a new directory). The committed fixtures had to match the generator
    byte for byte, so the catalog test failed until they were replaced. Only the writer string changed.
  The four `.sketchyup` fixtures keep their schemas (32, 33, 34 and 35). Their document chunk and identifier change with the writer,
  so `validate-native` reports new hashes for them.
- The eight `inputSha256` values in each of the six corpus manifests were regenerated for the `SketchyUp/1.6.0` writer. Each task was
  described with `provider_trial --describe gpt-6-astra` twice, and the input file's SHA-256 matched the describe output. The prompt
  hashes did not change. The corpus stays at v6: the assistant's tool surface did not change since 1.4.0, which the corpus freeze
  reproduces (`toolsSha256`, the allowed commands and both instruction hashes), and the baseline file is unchanged since before 1.4.0.
- The IFC export goldens (`tests/fixtures/ifc/export/*.ifc`) change on their `FILE_NAME` and `IFCAPPLICATION` lines, regenerated with
  `IFC_UPDATE_GOLDENS=1 ifc_export_tests` against the helper built from the vendored IfcOpenShell tree in this worktree.
- Contradiction, not changed: `tests/provider_release_corpus_tests.py` still says in its header that v5 is authoritative, while its
  `CURRENT` manifest is v6 (see the 1.4.0 notes above).

## Implementation notes (1.6.1 release preparation)

- The prior release for the 1.6.1-1 package is the published `sketchyup-1.6.0-1-x86_64.pkg.tar.zst` (SHA-256
  `f701d61f7a45acd8efb243d56871344d0bf357f0cf0090ce0743b6c55baafcad`, which the 1.6.0 `SHA256SUMS` asset lists). Unpacked into a
  private directory without pacman, its `sketchyup-cli --format-capabilities` reports `documentVersion` 35 and `readDocumentVersions`
  1 through 35. It opens `tests/fixtures/reference-geo-v35.sketchyup` with exit status 0 and leaves the file unchanged.
- The split package's prior, `sketchyup-ifc-1.6.0-1` (SHA-256 `4f9d8f2f601f92e8b5ebc53071d7487bef5d25e1a11109c394d3a48c13cc4612`, the
  1.6.0 `SHA256SUMS`), needs no pin in `verify-package.sh`, as in 1.5.0 and 1.6.0: the script unpacks only the `sketchyup` package.
- The `released_packages` table gained 1.6.0-1 and `known_release_sha256` its SHA-256. `vercmp` orders the table, and 1.6.1-1
  upgrades from 1.6.0-1 by the patch rule.
- The downgrade window. The 1.6.0 reader opens schema 35, the highest schema, so no retained fixture is newer than it and the
  refusal loop in `verify-package.sh` has nothing to check. The script now saves a georeferenced reference image with the new
  package's CLI (`--import-reference-image tests/fixtures/reference/north-up.png --reference-zone 30N --output`), requires
  `documentVersion` 35, and requires the released CLI to open that save unchanged. The same commands were run by hand on the dev
  build and the published 1.6.0-1 CLI, with the result the script expects. The package acceptance itself needs the pinned
  container as root, so it was not run here; the RC runs it.
- Prior-release tests. `native_format` now requires the 1.6.0 reader to open `reference-geo-v35.sketchyup` unchanged; it had
  required a refusal from 1.5.0. `component_parameter_io` accepts a prior that reads 26 to 35, and `drawing_sheet_io`'s comment names
  1.6.x. Against the published 1.6.0-1 CLI, `native_format`, `geographic_site`, `presentation_io`, `material_texture_size`,
  `style_bundle`, `terrain_io`, `component_parameter_io` and `drawing_sheet_io` pass, and their prior-release checks ran.
- The writer string is part of the fixture bytes. Bumping it to `SketchyUp/1.6.1` changed these fixtures. Each was regenerated
  with its generator and compared with the 1.6.0 bytes:
  - `material-texture-size-v33.sketchyup` (`material_texture_size_tests --write-fixture`): one byte differs, the writer. Its
    document identifier and chunk digest are unchanged.
  - `geographic-site-v32.sketchyup` (`geographic_site_tests --write-fixture`), `terrain-dem-v34.sketchyup`
    (`terrain_import_tests --write-fixture`) and `reference-geo-v35.sketchyup` (`sketchyup-cli --import-reference-image`): the
    generator creates a new document identifier on each run, and the document chunk's digest covers the writer, so both change
    (for example `geographic-site-v32`: identifier `7842a558…` to `ad34baff…`, chunk digest `17614017…` to `bb038ea6…`). The
    terrain and reference files also keep their asset chunk, which is unchanged. The CLI refuses an existing `--output` file, so
    the two CLI-written files were moved aside before their regeneration. Sizes are unchanged.
  - the five catalog fixtures (`catalog_tests --write-fixtures` into an existing empty directory): sizes unchanged; 61 to 119
    bytes differ, the writer string and the bytes that depend on it.
  The CLI writes new files with mode 0600, so the two it wrote were set back to 0644, as the tracked files are.
- The eight `inputSha256` values in each of the six corpus manifests (`provider-release-corpus.json` and v2 to v6) were regenerated
  for the `SketchyUp/1.6.1` writer. Each task was described with `provider_trial --describe gpt-6-astra` twice; the two describes
  agree; the retained input's SHA-256 equals the describe's value; the prompt and `promptSha256` are unchanged; and the input
  contains the writer `SketchyUp/1.6.1`. Each manifest's task entries match the newest, as `provider_release_corpus` requires.
  The corpus stays at v6: the assistant's tool surface did not change since 1.4.0 (`assistant_tool_surface` passes against the
  baseline, last re-pinned in `cb7a536a`, before the `v1.4.0` tag).
- The IFC export goldens (`tests/fixtures/ifc/export/*.ifc`) change on their `FILE_NAME` and `IFCAPPLICATION` lines only (12 lines in
  six files). They were regenerated with `IFC_UPDATE_GOLDENS=1 ifc_export_tests` in `sketchyup-ci-pinned:r097a-ifc`, against a helper
  and IfcOpenShell prefix built in this worktree from the vendored tree, not copied from a cache. That image has no `mold`, which
  the `ifc-app` preset inherits from `ci-fast`, so that build used the system linker; the goldens are output bytes, so the linker
  does not change them.
- Contradiction, not changed: ADR 0023, ADR 0025 and the open-defect triage give the outcome budget as 256.3 MiB. 268,812,472 bytes
  is 256.36 MiB, which rounds to 256.4 MiB. The 1.6.1 release notes and the support matrix give the byte count and "about 256 MiB".
- Contradiction, not changed: `tests/provider_release_corpus_tests.py` still says in its header that v5 is authoritative (carried
  from 1.4.0).

## Implementation notes (1.7.0 release preparation)

- The prior release for the 1.7.0-1 package is the published `sketchyup-1.6.1-1-x86_64.pkg.tar.zst` (SHA-256
  `414e7851862af157353ccf1066acce94f4008952b86c06eaa1b7e0773fe05dc9`, which the 1.6.1 `SHA256SUMS` lists). Unpacked
  with `bsdtar`, without pacman, its `sketchyup-cli --format-capabilities` reports `documentVersion` 35,
  `readDocumentVersions` 1 through 35 and `fileLimitBytes` 134217728. It opens `tests/fixtures/reference-geo-v35.sketchyup`
  with exit status 0 and leaves the file unchanged (`native_format_tests`).
- The split package's prior, `sketchyup-ifc-1.6.1-1` (SHA-256 `c665a849aef6a94a11ac784599d9b922c32a1f49919808483070a474ad3c8594`,
  the 1.6.1 `SHA256SUMS`), is not pinned, as before: the script unpacks only the `sketchyup` package.
- The `released_packages` table gained 1.6.1-1 and `known_release_sha256` its SHA-256. `vercmp` orders the table, and
  1.7.0-1 upgrades from 1.6.1-1 by the `x.y.0` rule.
- The downgrade window. The schema-35 reference-image check is unchanged: the 1.6.1 reader opens the 1.7.0 save unchanged.
  The new check in `verify-package.sh` makes, at check time, a model of 153,000 unique vertices: one face of 9,000 points
  (under the 10,000-vertex loop limit) and a linear `geometry.array_selection` of its body, count 16, which gives 17 bodies.
  The 1.7.0 CLI writes it at `documentVersion` 26, with a document chunk of 9,654,097 bytes and a file of 9,655,465 bytes.
  The published 1.6.1 CLI refuses it, unchanged, with exit status 1 and
  `{"code":"INVALID_OPERATION","error":"Document load exceeds document unique geometry limits: vertices would be 153,000 of 100,000","status":"failed"}`.
  This was run by hand in the pinned image. The package acceptance itself needs pacman as root in that image, so it was not run here.
- Not done: the refusal of a document chunk over 32 MiB. At the 1.6.1 unique limits, geometry does not reach that size (the
  300,000-vertex model is 35.0 MiB, per the R110 notes), so the check would need bulk that is not geometry. None was built.
- Fixtures regenerated with the writer `SketchyUp/1.7.0`. The nine writer-bearing fixtures keep their sizes; only the writer
  and the bytes that depend on it change.
  - `material-texture-size-v33.sketchyup` and `geographic-site-v32.sketchyup`: their `--write-fixture` generators, in place.
  - `terrain-dem-v34.sketchyup`: `terrain_import_tests --write-fixture` to a scratch path, then copied, because the CLI
    refuses an existing `--output` file.
  - `reference-geo-v35.sketchyup`: `sketchyup-cli --import-reference-image ... --output` to a scratch path, then copied, for the same reason.
  - the five catalog fixtures: `catalog_tests --write-fixtures DIR` into a new directory, then copied.
  The copied files were set back to mode 0644.
- IFC export goldens: `IFC_UPDATE_GOLDENS=1 ifc_export_tests` in `sketchyup-ci-pinned:r097a-ifc`, against the helper and the
  IfcOpenShell prefix built in this worktree (`build/ifc`, then `build/ifc-app`, with `CMAKE_LINKER_TYPE=DEFAULT`, since that
  image has no mold). Only the `FILE_NAME` and `IFCAPPLICATION` lines changed: 12 lines in six files. `ifc_export_tests` passes after the update.
- Corpus. The eight `inputSha256` values in each of the six manifests were regenerated for the `SketchyUp/1.7.0` writer. Each task
  was described with `provider_trial --describe gpt-6-astra` twice, and the two describes agree. For each task the retained
  input's SHA-256 equals the describe's value, the prompt and `promptSha256` are unchanged, the `allowedCommands`, `toolsSha256`
  and instruction hashes equal the v6 pins, and the input contains `SketchyUp/1.7.0` and not `SketchyUp/1.6.1`. So the six
  pinned assistant surfaces did not change, and the corpus stays at v6. `provider_release_corpus_tests.py`,
  `provider_release_score_tests.py` and `assistant_tool_surface_tests` pass. No live provider run was made here.
- Generated artifacts. `generate-tool-reference.py --check` passes. Of `generate-api-schemas.py`, the eight CLI-side artifacts match.
  Its two desktop-side artifacts (`inspection-desktop-v1.json`, `mcp-native-2026-07-28.json`) were not checked, because they
  need the desktop binary, which this preparation did not build. No file in `docs/api` names a version.
- Suites run in the pinned image, with `SKETCHYUP_PRIOR_CLI` set to the published 1.6.1 CLI where a suite reads it:
  `catalog_tests`, `catalog_import_tests`, `catalog_fuzz_tests`, `reference_image_io_tests`, `terrain_io_tests`,
  `native_format_tests`, `document_limits_io_tests`, `document_limits_tests`, `asset_io_tests`, `presentation_io_tests`,
  `style_bundle_tests`, `geographic_site_tests`, `material_texture_size_tests`, `drawing_sheet_io_tests`,
  `component_parameter_io_tests`, `purge_unused_tests`, `terrain_import_tests` and `unique_limits_acceptance_tests` all exit 0.
- Contradictions, not changed:
  - The R110 compatibility note (ADR 0005) says a model within the 1.6.1 bounds is written with the bytes 1.6.1 writes. The
    writer string is part of those bytes, and it now names 1.7.0, so the bytes differ in that field, as the fixture regeneration shows.
  - `tests/provider_release_corpus_tests.py` still says in its header that v5 is authoritative (carried from 1.4.0).

## Implementation notes (1.8.0 release preparation)

- The prior release for the 1.8.0-1 package is the published `sketchyup-1.7.0-1-x86_64.pkg.tar.zst` (SHA-256
  `8b51842e3ab746b9b766f0e489d7de5e1a9f9497eea98d316a54759b4cc3170d`, which the 1.7.0 `SHA256SUMS` lists). Unpacked with
  `bsdtar`, without pacman, its `sketchyup-cli --version` prints `sketchyup-cli 1.7.0`, and its `--format-capabilities`
  is the same as the 1.8.0 output (document version 35, reads 1 through 35, file limit 268435456).
  Its `--capabilities` differs from 1.8.0 only by the two new `limits` fields, `readSnapshotBytes` and `glbVisibleFaces`.
- The split package's prior, `sketchyup-ifc-1.7.0-1` (SHA-256 `938bb054c5ba90f3a38927802e545d0c81fcf076d09b284e0bf8a0d48f77804b`),
  is not pinned, as before: the script unpacks only the `sketchyup` package.
- The `released_packages` table gained 1.7.0-1 and `known_release_sha256` its SHA-256. `vercmp` orders the table, and
  1.8.0-1 upgrades from 1.7.0-1 by the `x.y.0` rule. `native.yml` pins the same package and digest, and
  `ci-line-inventory.txt` was regenerated with `scripts/ci-line-inventory.py extract`. Only the seven lines that name the
  prior package changed, and `ci-line-inventory.py check` passes.
- The R110 check changed. With 1.7.0 as the prior, its unique limit is 300,000 vertices, so a model of 153,000 unique
  vertices (17 bodies, documentVersion 26) is now required to open unchanged. Checked by hand with the published CLI:
  exit status 0, `valid` true, 17 bodies, and the file's bytes unchanged. The 1.6.1 refusal text is retired.
- The R112 check (new). A component of one 9,000-sided prism, placed 17 times, has 153,000 visible faces and 598,212
  visible triangles. The 1.8.0 CLI validates it at `documentVersion` 26, and its GLB export succeeds (exit status 0).
  The published 1.7.0 CLI validates it (exit status 0, unchanged bytes) and refuses the export with exit status 1 and
  `{"code":"INVALID_OPERATION","error":"GLB has too many visible faces","status":"failed"}`, creating no directory.
  The check `verify-package.sh` makes is the same recipe, generated by its Python step.
- Not checked: the 1.7.0 refusal of a model over its 256 MiB read-snapshot budget. A charge over 256 MiB needs, by estimate
  from the R112 acceptance figure (285.3 MiB for 291,994 faces), about 260,000 or more charged face copies, while 1.7.0
  refuses GLB above 100,000 visible faces first. A model that charges that much with at most 100,000 visible faces needs
  hidden bodies, which was not built. Left out as not cheap, so the release notes state that this refusal's text was not measured.
- Fixtures regenerated with the writer `SketchyUp/1.8.0`. The nine writer-bearing fixtures keep their sizes; only the writer and,
  for the geographic site, the terrain and the reference image, the document identifier that each generator creates anew change.
  - `geographic-site-v32.sketchyup` (`geographic_site_tests --write-fixture`), `material-texture-size-v33.sketchyup`
    (`material_texture_size_tests --write-fixture`) and `terrain-dem-v34.sketchyup` (`terrain_import_tests --write-fixture`),
    each to a scratch path and then copied, as the generators need a new output file.
  - `reference-geo-v35.sketchyup`: `sketchyup-cli --import-reference-image ... --reference-zone 30N --output` to a scratch path.
  - the five catalog fixtures: `catalog_tests --write-fixtures` into a new empty directory, then copied.
  The copied files have mode 0644. Every regenerated file differs from its predecessor only in the writer string and the
  digests and identifiers that depend on it.
- IFC export goldens: `IFC_UPDATE_GOLDENS=1 ifc_export_tests` in `sketchyup-ci-pinned:r097a-ifc`, with the helper and the
  IfcOpenShell prefix built in this worktree (preset `ifc`, then `ifc-app` with `CMAKE_LINKER_TYPE=DEFAULT`, since that image
  has no mold) at two compile jobs. Only the `FILE_NAME` and `IFCAPPLICATION` lines changed: 12 lines in six files. The
  test passes after the update, without the variable.
- Corpus. The eight `inputSha256` values in each of the six manifests were regenerated for the `SketchyUp/1.8.0` writer. Each
  task was described twice with `provider_trial --describe gpt-6-astra`; the two describes agree, and the retained input's
  SHA-256 equals the describe's value. For each task the prompt, `promptSha256`, `allowedCommands`, `toolsSha256`, the two
  instruction hashes, the session, the draft lifetime and the task lifetime equal the v6 pins, and the input names
  `SketchyUp/1.8.0` and not 1.7.0. So the six pinned assistant surfaces did not change, and the corpus stays at v6.
  `provider_release_corpus_tests.py` (offline; 24 replayed runs, no provider request) passes, `provider_release_score_tests.py`
  runs 129 tests (5 skipped) and passes, and `assistant_tool_surface_tests` passes. No live provider run was made here.
- Generated artifacts. `generate-tool-reference.py --check` passes (2 artifacts). `generate-api-schemas.py --check`, run with
  the built desktop binary as well as the CLI, passes for all 10 discovery schemas. No file in `docs/api` names a version.
- Suites run in the local build (`ci-fast` flags, Debug with `-g0`, shared libraries, default linker), with
  `SKETCHYUP_PRIOR_CLI` set to the published 1.7.0 CLI where a suite reads it: `catalog_tests`, `catalog_import_tests`,
  `catalog_fuzz_tests`, `native_format_tests`, `reference_image_io_tests`, `terrain_io_tests`, `document_limits_io_tests`,
  `document_limits_tests`, `asset_io_tests`, `geographic_site_tests`, `material_texture_size_tests`, `terrain_import_tests`,
  `glb_export_tests`, `presentation_io_tests`, `style_bundle_tests`, `drawing_sheet_io_tests`, `component_parameter_io_tests`,
  `purge_unused_tests`, `unique_limits_acceptance_tests`, `snapshot_glb_limits_acceptance_tests`, `command_tests`,
  `transaction_dispatch_tests`, `history_command_tests`, `scene_input_tests`, `automation_session_tests` and
  `assistant_tool_surface_tests` all exit 0. The build-tier CTest run of the fast tier (275 tests, the CI exclusions applied, two jobs,
  Debug) passes: 258 pass and 17 are skipped, because they need an external oracle or corpus (Blender, the DXF, PDF, OBJ, STL
  and glTF references, and the `.skp` corpus), which the CI runs in its own lines with those variables set.
- Contradictions, not changed:
  - `scripts/screenshots.Dockerfile` defaults to `SKETCHYUP_VERSION=1.7.0`, and `docs/images/README.md` names the 1.7.0 image.
    The screenshots are regenerated at a release publication, so they stay as they are until then.
  - The README's `v=1.7.0` install snippet and its "current release" line also change at a release publication, not at preparation.
  - The R089a rows D-094 to D-096 still say "in this PR" (the branch that fixed them); the rows are history and are not reworded.
- Resolved at the publication (2026-11-07). The README's `v=1.8.0` install snippet and its "current release" line name 1.8.0. The R089.a rows D-094 to D-098 name the PRs that fixed them (#569, #571, #573 and #575), so their wording changed at publication, against the last item above. [#579](https://github.com/zygote55/sketchyup/pull/579) moved the screenshot Dockerfile and `docs/images/README.md` to 1.8.0 and regenerated the images from the published package.

## Implementation notes (1.9.0 release preparation)

- The prior release for the 1.9.0-1 package is the published `sketchyup-1.8.0-1-x86_64.pkg.tar.zst` (SHA-256
  `d44e2f6632712232d889e5deec0e8178cc670df8def1502249a8fb571e21dd34`, which the 1.8.0 `SHA256SUMS` lists). Unpacked with
  `bsdtar`, without pacman, its `sketchyup-cli --version` prints `sketchyup-cli 1.8.0`. Its `--capabilities` has the same key
  paths as the 1.9.0 output (25,134 paths each, compared with `jq`); only the values of the `limits` fields differ.
- The version is set in `CMakeLists.txt` and `pkgver`; `scripts/release-version.sh` prints `1.9.0`. `pkgrel` is 1.
- No document schema changed: `nativeDocumentVersion` is 35 (`src/io/native_format.hpp`).
- The downgrade checks use the 1.8.0 CLI, which refuses a file over its limits with its own message and leaves it unchanged:
  1,025 component definitions (`Too many component records`), 306,000 unique vertices and 612,000 materialized vertices.
  A 17-placement prism model (153,034 visible faces) opens and exports to GLB in 1.8.0. The 1.8.0 CLI refuses a file at open,
  not at export: a file it can open has at most 600,000 materialized vertices and so at most 600,000 visible faces, so the
  1.8.0 visible-face cap cannot refuse one. The checks are in `packaging/arch/verify-package.sh`, and the messages are as the
  1.8.0 CLI printed them.
- Two defects that the preparation found are fixed in it and recorded in the triage: D-111, `limits.componentDefinitions`
  reported the literal 1,024 (the enforced limit is 4,096, from R117); and D-112, the Blender worker's GLB paired-triangle bound
  was R113's 1,800,000 (the export's bound is 14,400,000, from R115).
- The writer-bearing fixtures were regenerated with their generators for the `SketchyUp/1.9.0` writer: the geographic site,
  the material texture size and the schema-34 terrain (their `--write-fixture` tests), the schema-35 reference image (the CLI
  import, as the fixtures README gives it), the five catalog fixtures (`catalog_tests --write-fixtures`), and the six IFC
  export goldens (`IFC_UPDATE_GOLDENS=1 ifc_export_tests`). Each golden differs from its 1.8.0 version only in the version
  strings. The 1.8.0 CLI opens every regenerated native fixture unchanged.
- The corpus stays at v6. The eight `inputSha256` values in each of the six manifests were re-pinned from the real
  `provider_trial --describe` output for the 1.9.0 writer. The eight trials' `toolsSha256` values are unchanged, so the
  surfaces did not move. `provider_release_corpus_tests.py`, `provider_release_score_tests.py` and `assistant_tool_surface_tests`
  pass. The R115 change to `tests/assistant-tool-surface-baseline.json` (the every-command-with-clarification surface) is not
  pinned by v6.
- The release-candidate build used the pinned image without `mold`, which the `ci-fast` and `ifc-app` presets ask for, so the
  preparation configured with `-DCMAKE_LINKER_TYPE=DEFAULT`. The IFC sub-build of IfcOpenShell runs `--parallel 4` unless
  `SKETCHYUP_IFC_JOBS` is set; the preparation set it to 2, because four compile jobs were killed under the 4 GiB container cap.
- The README `v=` snippet, its "current release" line and the screenshots stay at 1.8.0 until the release publication, as the
  1.8.0 entry above says.
- Resolved at the publication (2026-11-10). The README's `v=1.9.0` install snippet and its "current release" line name 1.9.0. The screenshot Dockerfile (`scripts/screenshots.Dockerfile`, default `SKETCHYUP_VERSION=1.8.0`) and `docs/images/README.md` still name 1.8.0: the screenshots are another PR's, which completes them. The R089.a rows D-102 to D-112 have no "in this PR" or "on main" wording, so none changed at publication.
