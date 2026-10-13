# R087.u — Current and reference Arch clean machines

2026-10-13. The package built from `main` at
`da68089648bbdf5ae71b30cadd604975d3400917` completes the installed lifecycle on
three clean Arch environments. Each environment is a fresh disposable container
started from its base image, with no network.
[Per-environment summary](R087u-clean-machine-packages.json).

| Environment | Base image | Archive snapshot | pacman | Qt | Mesa (llvmpipe) | Package SHA-256 (0.1.0-3) |
| --- | --- | --- | --- | --- | --- | --- |
| Current, pinned CI image | `sha256:51dd3d24…facc3` | 2026-10-02 | 7.1.0 | 6.11.2 | 26.2.3 | `e6f9cce8…6512434` |
| Current, fresh image | `archlinux:base-devel` `sha256:06167992…4ef1` (created 2026-10-12) | 2026-10-12 | 7.1.0 | 6.12.0 | 26.2.4 | `e6f9cce8…6512434` |
| Reference, declared minimum | `base-devel-20241013.0.269705` `sha256:29119837…14c2` | 2024-10-15 | 7.0.0 | 6.8.0 | 24.2.4 | `5754e7a7…f1875f` |

**Reference.** No document declares a minimum Arch snapshot. The
[support matrix](../SUPPORT_MATRIX.md) states the declared Qt minimum, and
`CMakeLists.txt` requires `Qt6 6.8`. The PKGBUILD's dependencies carry no versions.
The reference is therefore the first archive snapshot with `qt6-base 6.8.0` in
`extra`. That is 2024-10-15, since 2024-10-14 still has 6.7.3. The base image is the
newest official image before that date. Package signatures are checked as of
2024-10-16. Several 2024 packager keys have since expired or been retired, so
checking them against today's date rejects packages that were valid in that snapshot.

**Builds.** The source archive (`f4cf81f6…5f55`, 1,147 entries) comes from
`scripts/package-source.sh`. `makepkg --nocheck` builds it in the pinned image on the
2026-10-02 snapshot (GCC 16.2.1, 1,433 s). The same archive also builds in the
reference image on the 2024-10-15 snapshot (GCC 14.2.1, CMake 3.30.5, Qt 6.8.0,
1,303 s). Current `main` compiles and runs against Qt 6.8.0. The check phase is
skipped; [R087.g](R087g-clean-current-package.md) holds the CTest record.

Both current environments install the **same** package file. The package built on
2026-10-02 also runs unchanged on the fresh image's Qt 6.12.0. It **cannot** run on
the reference system. `pacman -U` accepts it, but both executables fail to start
with `Qt_6.11` and `GLIBC_2.43` symbol versions not found. The PKGBUILD's
dependencies carry no versions, so pacman does not reject the mismatch. The
reference row therefore uses packages built in the reference environment.

## Lifecycle in each environment

The network check comes first, in a container started with `docker run --network
none`. The container has only a loopback interface, and a TCP connect to the
archive fails. Before the run, a separate networked container downloads the
snapshot upgrade, the package dependencies and the test tools to a cache. The
offline run then upgrades to the snapshot and installs only from that cache.

1. **Install and dependency closure.** `pacman -U` adds the package and its
   dependencies: 60 packages (64 on the reference). None of them is Blender,
   Python, Node.js, libsecret or a provider SDK. The executables link directly only
   to Qt Core/Gui/Network/OpenGL/OpenGLWidgets/Widgets, `libcrypto`,
   `libwayland-client`, and the C and C++ runtimes. No library is unresolved. The
   package ships no systemd or D-Bus unit. `pacman -Qkk` and
   `desktop-file-validate` pass.
   Qt's own dependencies bring in libraries: `libproxy` links `libcurl`, and
   `libcups` brings the `avahi` package. Neither starts a service, and nothing
   needs the network.
2. **Open and save a demo model.** The installed CLI opens the installed example
   `m6-study-after.sketchyup` (42 bodies) and saves a copy, `Saved demo.sketchyup`.
   Inspection of the saved copy is identical to the original. The desktop app has
   no non-interactive save path, so saving goes through the installed CLI.
3. **Launch.** Under `xvfb-run` (X11, llvmpipe), these runs pass, each with ready
   renderer and text and zero GL errors:
   - `--smoke`, including picking.
   - `--demo --capture`.
   - Opening the saved copy. Its identity, revision and body count match the CLI.
   - A normal interactive start, which stays up for 15 s.

   Under `QT_QPA_PLATFORM=offscreen`, inspection discovery works, and a normal
   interactive start that opens the saved copy stays up for 15 s. Qt's offscreen
   platform gives no OpenGL context without a display. Rendered frames are
   therefore checked only under Xvfb; an offscreen `--smoke` times out waiting
   for frames.
4. **Historical migrations.** `verify-installed-migrations.py` runs against
   `/usr/bin/sketchyup-cli` on all **28** retained fixtures (25 at R087.g, plus 3
   since). Every one passes in every environment, including the byte-exact
   ordinary-save comparison and the rejected overwrite, in-place and truncated
   cases.
5. **Removal.** `pacman -R` removes every package-owned file. `find /usr -iname
   '*sketchyup*'` is empty afterwards, and the saved user document remains.
6. **Upgrade from the prior package.** `verify-package-upgrade.py` installs prior
   `0.1.0-2` and seeds preferences: theme, text size, reduced motion, trackpad
   navigation, recovery interval, field of view and an unknown section. It also
   seeds **customized shortcut bindings** in `shortcuts/v1`:
   - `file.importDxf` → Ctrl+Alt+K
   - `edit.shortcuts` → Ctrl+Alt+J
   - the unknown `future.command` → Ctrl+Shift+F9

   The prior CLI authors a document, and the prior desktop app opens it. The
   upgrade to `0.1.0-3` changes both executables. The new CLI and desktop app reopen
   the document with the same identity, revision and bodies. Every preference and
   all three bindings survive, after both applications have rewritten the settings
   file. Removal leaves no `/usr` file and preserves the document, settings, data
   and cache.

   In the current environments, the prior package is the retained R087.g `0.1.0-2`
   (`7f1276f4…b976`, source `64e6e5ed`). On the reference, the same source is
   rebuilt with pkgrel 2 (`64ffbdcc…6809`). That source predates the shortcut
   editor, so it treats the bindings as unknown settings. The check therefore
   proves the bindings are kept, not that the new build applies them.

## Changes and attempts

`scripts/verify-package-upgrade.py` now seeds and checks the shortcut bindings. Its
inspection check now requires every field reported by the prior CLI to stay the
same. Fields the new CLI adds are allowed and recorded. The first pinned run had
failed because of this: the candidate adds the top-level field
`displayPrecision: "full"` (R084.r), which the old byte-equality check rejected. The
first reference download failed on the signature trust issue described above.
Both failed attempts are retained privately with their logs.

Resources: builds used four CPUs, two compiler jobs, 4 GiB of memory, no extra swap
and 512 processes. Lifecycle containers used two CPUs, 2 GiB, no swap, 256 processes
and no network. Builds ran one at a time under the shared local-check lock. No host
package changed and no provider was called. Logs, caches, evidence trees and the
harness are kept in `build/local-checks/r087u`.

This closes the current and reference clean-machine clause of R087 for development
packages. It is not release acceptance. Still open: the final release-candidate
package, an upgrade from a published release, checksums and notices for release
artifacts, exact-head CI and ordered merges. The PKGBUILD's unversioned
dependencies remain, and pacman accepts a package on an older system that cannot
run it. Arch does not support partial upgrades, so this only matters when a
package is installed outside its repository snapshot.
