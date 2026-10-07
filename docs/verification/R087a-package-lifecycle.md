# R087.a — Development package lifecycle

2026-10-07. Source `8f93b4fca51a52a6cfd77e25366dc2c57430b178` passes
install, desktop-entry launch with a spaced document path, MIME detection with
and without an extension, default association, CLI recipes/export, package
upgrade, document reopen and removal in the disposable pinned Arch container.
[Evidence and package/source hashes](R087a-package-lifecycle.json) identify the
exact build. The runtime uses Qt 6.11.2 and Mesa 26.2.3 with llvmpipe on X11.
[Installed launch](R087a-launcher.json) and
[upgraded application smoke](R087a-upgraded-smoke.json) report ready renderer,
ready text and zero GL errors. No host packages or user preferences were changed.

The package test phase registered 181 tests: [170 passed and 11 optional external
consumer checks skipped](R087a-package-ctest.txt), in 107.77 seconds. Those skipped
checks are not package passes; the prior [R085.b full integration run](R085b-native-surface-color.md)
records all 181 passing with configured consumers at its own stated source.
The second package build uses the existing documented `--nocheck` upgrade path;
this is one test execution, not two.

The new `SKETCHYUP_NATIVE_FIXTURES_IN_ALL` option defaults to ON for development
and CI. Packaging sets it OFF to omit 78 GUI fixtures/benchmarks from the default
build: the package test phase never invokes them. The installed app and all
registered test dependencies remain built. Configuration comparisons verify
identical 181-test lists with the option ON/OFF, explicit native targets remain
buildable, and CLI-disabled configuration retains its 173 tests. Native CI
coverage and workflow triggers are unchanged.

Both package releases use the same source (`0.1.0-1` to `0.1.0-2`). Upgrade keeps
the document byte-equivalent on CLI inspection and preserves six seeded
preferences, including the valid integer Light-theme value. Package integrity
checks report 240 files with none altered. Removal checks every package-owned
non-directory path is absent while the private test document, preferences,
data and cache sentinels remain. This proves package lifecycle behavior; it does
not prove historical native-format migration or upgrade from a prior application
implementation. Optional Blender/credential integrations are documented without
becoming mandatory launch dependencies.

The initial attempt at source `5459f41` was stopped deliberately before its test
phase. It used an invalid textual theme fixture and spent minutes linking each
unused native GUI test. Its source, script, PKGBUILD, log and hashes remain in
private `build/r087a-initial-attempt/`; container exit 137 followed the explicit
stop timeout and is not classified as a product crash or confirmed OOM. The
corrected attempt reused build objects and completed with exit zero. This is
not evidence of a clean-from-empty build.

R087 remains in progress. Current/reference clean-machine runs, historical
migration, final release-candidate checks, remote CI and ordered merges remain
open. No release artifact or tag is accepted by this development check.
