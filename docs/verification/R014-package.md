# R014: disposable Arch package acceptance

Date: 2026-10-03. Development package 0.1.0-2, not a public release.

The pinned Arch image and 2026/10/02 archive mirror built the package from source
as an unprivileged builder. Release compilation and all six CTest targets passed.
Clean installation, package integrity, desktop-file validation, MIME glob and
binary magic, desktop launch with a spaced filename, upgrade, reopen and removal
passed. The final run used a fresh container and fresh build directory. CI now
runs the same acceptance script after native/core checks.

The first visual inspection caught missing fonts in the minimal runtime: rendering
and file loading passed but UI text showed replacement boxes. The package now
requires DejaVu fonts, and capture/smoke checks require Latin/ellipsis font coverage.
The final [launcher capture](R014-launcher.png) has readable text and the expected
wall-ring model. [Capture metadata](R014-launcher.json) matches the installed CLI's
document identity, revision and body count. llvmpipe reported OpenGL 4.6 and no GL
error. This test adds capture/exit flags through a PATH shim while retaining the
installed desktop file and its `%f` argument expansion.

The harness also installs the MIME query helper and includes Arch's `vendor_perl`
directory in its non-login PATH. Without it, generic `xdg-mime` falls back to `file`,
which does not consult the desktop MIME database. GIO independently recognized the
registered type. This is a test-environment dependency, not a custom desktop setting.

Both package releases in the upgrade fixture use identical current source; they
test package lifecycle hooks. Historical file migration is verified separately by
R012 fixtures. `pacman -Qkk` found 19 package files and zero alterations at install
and upgrade. Removal deleted package-owned executables, launcher, icon and MIME
XML; the saved model and settings/data/cache sentinels survived.

Artifacts from this run, under ignored `build/`:

- `package-r014-final/sketchyup-0.1.0-2-x86_64.pkg.tar.zst`
- Package SHA-256: `38fbc9427a03fc13648c216ced3cdcb23671e2b4d3908519467432a8a2dfe2fc`
- Source SHA-256: `2006e51d19b51dc8cdadf54eee17435e56c848acaebd7771daf4212c0e970803`
- `package-r014-final-evidence/`: full acceptance log, installed CLI descriptions,
  launcher arguments/report/capture, upgraded smoke report, preserved user files.

Reproduction and user-path ownership are in
[packaging/arch/README.md](../../packaging/arch/README.md). No host packages,
preferences, desktop entries or MIME associations were changed by this test.
