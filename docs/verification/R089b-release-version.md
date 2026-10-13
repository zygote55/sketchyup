# R089.b — Release version 1.0.0

2026-10-13. Source revision `d297f19cf74576d28cf07deeceb0154b9e2d53d1`
(branch `release/r089b-version-1.0.0`, from `main` at `d75b4035`). Decision:
[ADR 0162](../decisions/0162-release-versioning.md).

## Change

- `CMakeLists.txt` declares `project(SketchyUp VERSION 1.0.0)`. CMake generates
  `sketchyup/version.hpp` (`SKETCHYUP_VERSION`, `SKETCHYUP_WRITER`) in the build tree.
- The MCP server info, the native container `writer` (`SketchyUp/1.0.0`), and
  `--version` on `sketchyup-cli` and `sketchyup` use the macros.
- `packaging/arch/PKGBUILD` is `1.0.0-1`. `scripts/release-version.sh` fails when
  `pkgver` and CMake disagree. `package-source.sh` uses it and now writes a
  deterministic archive. `verify-package.sh` upgrades from the development package
  `0.1.0-3` to `1.0.0-1`.
- `scripts/release-artifacts.sh` writes `SHA256SUMS`, `THIRD_PARTY_NOTICES` and
  `MANIFEST.json`. `CHANGELOG.md` holds a placeholder.

The writer string is document bytes. Retained fixtures under `tests/fixtures`
keep `SketchyUp/0.1.0` and are unchanged. `native_format_tests` now checks three
things: a fresh save names `SKETCHYUP_WRITER`, a container whose writer is rewritten
to another string still loads to the same document, and `--version`. `mcp_tests`
checks the server version. The frozen provider corpus hashes generated container
bytes, so its eight `inputSha256` values were regenerated with
`provider_trial --describe` in both corpus files. Prompts and `promptSha256` are
unchanged, the same procedure as R082.cc and R084.r. The PNG hashes in
`tests/fixtures/performance/README.md` are rendered images and do not depend on
writer bytes.

## Results

| Check | Command | Result |
| --- | --- | --- |
| Development build | `cmake --preset dev && cmake --build --preset dev` | Builds |
| Development tests | `ctest --preset dev` | 179 pass and 11 opt-in `*_real` tests skip. Before the corpus hashes were regenerated, `provider_release_corpus` failed with "Frozen corpus drift: measure/inputSha256"; it passes after |
| Headless tests | `ctest --preset headless` | 62/62 pass |
| Version | `build/dev/sketchyup-cli --version`; `build/dev/sketchyup --version` | `sketchyup-cli 1.0.0`; `SketchyUp 1.0.0` |
| Fresh save | `sketchyup-cli --script examples/room-shell.json --output …` | `"writer":"SketchyUp/1.0.0"` |
| Migrations | `scripts/verify-installed-migrations.py --cli build/dev/sketchyup-cli --fixtures tests/fixtures --output build/r089b-migrations` | 28 fixtures; all preservation checks pass |
| Arch package acceptance | `scripts/run-container-check.sh r089b-package-acceptance sketchyup-native-fonts-check:20261008 bash /work/package/verify-package.sh` | Exit 0. The `0.1.0-3` build passed 190/190 CTest. Install, desktop launch, MIME, recipes, GLB and upgrade `0.1.0-3` → `1.0.0-1` pass, and so do reopen, 28 installed migrations, preferences and removal |
| Release artifacts | `scripts/release-artifacts.sh build/release-1.0.0` with the built package copied in | See below. Two runs at one commit produced identical archives. Dirty-tree, version-mismatch and stale-package inputs fail |

Both acceptance packages are built from the same 1.0.0 source; the `0.1.0-3` label
exercises the version-ordered upgrade and is not the historical 0.1.0 binary.

`MANIFEST.json` at `d297f19c`:

```json
{
  "name": "sketchyup",
  "version": "1.0.0",
  "pkgver": "1.0.0",
  "pkgrel": 1,
  "gitCommit": "d297f19cf74576d28cf07deeceb0154b9e2d53d1",
  "date": "2026-10-13T15:08:09Z",
  "buildImage": "mirror.gcr.io/library/archlinux:base-devel@sha256:51dd3d24f7fba779e7c471caeee7804c50e8c134ad948e19685a1c83a42facc3",
  "sourceArchive": {"file": "sketchyup-1.0.0.tar.gz", "sha256": "0e31348b8fc2044297dcef57f2b8fcd805decb1743883e0d509e7114df821a3d"},
  "packages": [{"file": "sketchyup-1.0.0-1-x86_64.pkg.tar.zst", "sha256": "8db0e17cc3480aebc21b57cf80d0ccef3af521d274bca69827914a6a193a5981"}],
  "thirdPartyNotices": {"file": "THIRD_PARTY_NOTICES", "sha256": "af2bcca99c0b80740c3b811af410196d938cba684608cbe8995af7ec394cf10a"}
}
```

This is development evidence, not the release-candidate record. The local package
was built in `sketchyup-native-fonts-check:20261008` rather than the CI image digest
named in the manifest. The RC freeze names its own commit and artifacts.
