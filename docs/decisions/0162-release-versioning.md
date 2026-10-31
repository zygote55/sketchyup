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
