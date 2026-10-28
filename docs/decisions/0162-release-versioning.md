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
