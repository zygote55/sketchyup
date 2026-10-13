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
the previous package from the same source: `pkgrel - 1` of the same version, or, for
`1.0.0-1`, the last development package `0.1.0-3`. pacman orders `1.0.0-1` after
`0.1.0-3`.

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
