# R089.d — Release-candidate walkthrough for 1.0.0

2026-10-13. The release candidate is `main` at
`7b01bb7a1d3fa19b6b5cc7f1a4c27a2ad5cd03a2` (merge of PR #307). Every check below
ran against this commit or its artifacts. The procedure is
[M9_ACCEPTANCE.md](../M9_ACCEPTANCE.md), "Release-candidate freeze" and "Combined
release-candidate walkthrough". [Machine-readable record](R089d-release-candidate.json).

## RC identity

| Item | Value |
| --- | --- |
| Commit | `7b01bb7a1d3fa19b6b5cc7f1a4c27a2ad5cd03a2` (tree `a8a95ad9e1140c72038525acf543dcd6bac2ef08`) |
| Source archive | `sketchyup-1.0.0.tar.gz`, SHA-256 `c3cef6a9870b69dfc55fc82189d212bb44c73a057e834fa46dbbb936303b24ed` (1,124 files) |
| Package | `sketchyup-1.0.0-1-x86_64.pkg.tar.zst`, name `sketchyup`, version `1.0.0-1`, SHA-256 `9405d89c87c27443527973f7637794fe73455e1d4034a71029d7b23c103d57f7` |
| Third-party notices | `THIRD_PARTY_NOTICES`, SHA-256 `af2bcca99c0b80740c3b811af410196d938cba684608cbe8995af7ec394cf10a` |
| Pinned image (package job, `MANIFEST.json`) | `mirror.gcr.io/library/archlinux:base-devel@sha256:51dd3d24f7fba779e7c471caeee7804c50e8c134ad948e19685a1c83a42facc3` |
| Local package build image | `sketchyup-build:20261002`, `sha256:a4f0465cd5526d080bc6686368f27a38dd09dd666dbc6f0a356e8138b409b71c` |
| Local native test image | `sketchyup-native-fonts-check:20261008`, `sha256:873ba0787bccd851a46f068e265f7d2922f90df0857e782bbf6d441f0c51bd08` |
| Toolchain in both local images (2026-10-02 archive) | GCC 16.2.1+r23, CMake 4.4.3, Ninja 1.13.2, Qt 6.11.2 (qt6-base 6.11.2-3, qt6-wayland 6.11.2-1), Mesa 26.2.3 (llvmpipe, LLVM 23.1.1), Wayland 1.26.0 |
| Native test tools | Weston 15.0.1, Xvfb (xorg-server-xvfb) 21.1.24, at-spi2-core 2.60.7, Python 3.14.7, FFmpeg 9.0.2 |

`scripts/release-artifacts.sh` ran twice in a clean detached checkout of the RC.
The first run wrote the archive, notices, `SHA256SUMS` and `MANIFEST.json`. The
package acceptance below then built the package. After the package was copied in,
the second run produced the same archive hash and added the package to
`SHA256SUMS` and `MANIFEST.json`. No tag or release was created.

## Re-run at the RC

| Area | Run | Result |
| --- | --- | --- |
| Arch package acceptance | `verify-package.sh` in `sketchyup-build:20261002` through `scripts/run-container-check.sh` (run `rc101`) | Exit 0. The `0.1.0-3` build passed 190/190 CTest. Install, desktop launch, MIME, recipes, GLB, upgrade `0.1.0-3` → `1.0.0-1`, reopen, 28 installed migrations, preferences and removal pass |
| Clean machine, pinned image | R087.u lifecycle, `--network none` | Pass. 60 new packages; no Blender, Python, Node.js, libsecret or provider SDK. Xvfb launches pass. 28 migrations pass. Removal leaves no `/usr` file |
| Clean machine, fresh image | Same, `archlinux:base-devel` `sha256:061679925e5d…14ef1`, 2026-10-12 archive (Qt 6.12.0, Mesa 26.2.4) | Pass, same package file |
| Clean machine, reference | Same, `base-devel-20241013.0.269705` `sha256:291198370afb…14c2`, 2024-10-15 archive (Qt 6.8.0, Mesa 24.2.4) | Pass, with a package built there from the RC archive |
| Development build and tests | `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev` | 190/190 pass; 11 opt-in `*_real` tests skip. The desktop-without-CLI configure also succeeds |
| Native suites, development build | 84 suites from `native.yml`, each on X11 and Wayland at 1× and 2× (336 cases) | 333 pass on the first attempt. Two fail once and pass on re-run. One times out three times on one CPU and passes with six. See Failures |
| AT-SPI bridge | `tests/accessibility_bridge_test.py` on X11 and Wayland at 1× and 2× | All four pass, 565 accessible nodes each |
| All-dialog keyboard audit | Inventory, then 16 groups on X11 and Wayland at 1× and 2× | 64/64 runs pass. 102 dialogs, 408 dialog checks, none failed; no uncovered "…" action |
| Keyboard layouts and fractional scale | `us`, `de`, `fr` on both platforms; 1.5 and 1.75 on five suites, both platforms | 26/26 pass |
| Sanitized build and tests | Every step of the CI sanitize job: `ctest --preset sanitize`, the bounded inspection/CLI list, shipped recipes and discovery, the private Wayland client fix, the sanitized native build and `texture_cache` | 62/62, 97/97, 7/7 and 1/1 pass. The Wayland client patch tests and leak probes pass |
| Native suites, sanitized build | 46 suites from the sanitize job, each on X11 and Wayland at 1× and 2× (184 cases), with leak detection at its default | 184/184 pass on the first attempt |
| M5 study | `scripts/verify-m5-offline.sh build/dev <new dir>` | Pass; deterministic checks pass, no live provider, no Blender |
| M6 study | `scripts/verify-m6-offline.sh build/dev <new dir>` | First attempt exit 1; second attempt passes. See Failures |
| M7 study | `ctest -R '^m7_presentation$'`; `m7_presentation_input_tests` on X11 and Wayland at 1× and 2× | 1/1 headless test passes; four native runs pass. Each wrote its evidence folder |
| M8 study | `QT_QPA_PLATFORM=offscreen … build/dev/m8_exchange_tests <new dir>` | Pass. The study wrote glTF, OBJ, both STL encodings, DXF, PDF, SVG and library outputs |

Where `native.yml` has an exact line for a suite at a backend and scale, that line
ran verbatim. The workflow does not run every suite on every backend and scale.
Missing cells used the same job's pattern: `xvfb-run -a env QT_QPA_PLATFORM=xcb
QT_SCALE_FACTOR=N LIBGL_ALWAYS_SOFTWARE=1 timeout Ts` with the suite's CI timeout,
or `SKETCHYUP_TEST_SCALE=N scripts/test-wayland.sh`. Sanitized X11 cells have no CI
line; they used a 90 s timeout. 289 of 336 development cells and 46 of 184
sanitized cells are CI lines. The case lists record the origin of each cell.

**Blender.** The local images have no Blender. Development `*_real` Blender tests
skip, and the X11 1× `render_input_tests` line ran without
`SKETCHYUP_BLENDER_TEST`. M5 and M6 ran without the Blender argument. These are
covered by the CI run at the RC below.

## Earlier evidence relied on unchanged

- **CI at the RC.** Run [38435734443](https://github.com/zygote55/sketchyup/actions/runs/38435734443)
  on `7b01bb7a` passed all four jobs: development build and native tests, Blender
  interoperability, sanitized builds and Arch package acceptance. It covers the
  Blender-dependent steps, the MCP schema validation and the other CI steps not
  repeated here.
- **Prior package.** The upgrade on clean machines starts from the R087.u
  `0.1.0-3` packages: `e6f9cce8…6512434` (pinned and fresh) and `5754e7a7…f1875f`
  (reference), both from source `da680896`. They are the last development
  packages, so they are not rebuilt.
- **Hardware, provider and performance.** This walkthrough ran no GPU, display,
  provider or timing check. The R082, R085 and R086 records listed in
  M9_ACCEPTANCE.md stand as they are, and ADR 0161 waives what it names. Their
  inputs (fixtures, corpus, provider profile) did not change in R089.b or R089.c.

## Failures

1. **`visible_focus_input_tests`, Wayland, 2×** (CI line
   `SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/visible_focus_input_tests`).
   Three attempts each end with `Timed out after 60 s: build/dev/visible_focus_input_tests`
   from `test-wayland.sh` (60.2, 60.3 and 60.3 s). The test printed no failed check.
   Locally the same suite takes 29 s on Wayland 1× and 48 s on X11 2×. The runner gave
   it one CPU. The CI run at the RC passed the same line. After the matrix, the
   same CI lines ran once more on the wide runner (six CPUs) with the same build
   (run `rc-vf-wide`): Wayland 1× passed in 14.4 s and Wayland 2× in 28.4 s,
   under the 60 s limit. Classification: a resource limit of the one-CPU local
   runner, not an application failure.
2. **`annotation_input_tests`, Wayland, 2×** (CI line). Attempt 1 failed the
   check "Annotation window exposed" (exit 1). Attempt 2 passed. Classification: flaky.
3. **`interaction_tests`, Wayland, 2×** (pattern cell; CI runs this suite on X11
   only). Attempt 1 exited 2, the test's code for a window that is not exposed and
   active within 5 s. Attempt 2 passed. Classification: flaky.
4. **M6 study.** Attempt 1 completed every step and then failed in the script's
   final summary: `FileNotFoundError: [Errno 2] No such file or directory: 'git'`.
   The local image has no `git`, and `verify-m6-offline.sh` does not handle its
   absence; `verify-m5-offline.sh` does. The 190/190 regression log of attempt 1 is
   kept. Attempt 2 installed `git` from the same pinned archive and passed. The
   source copy has no `.git`, so `sourceCommit` records as null. Classification:
   deterministic without `git`. It is a local environment gap, not an application
   failure. No code was changed.

## Runs and limits

The runs used two runners. None of them are timing checks.

- **Standard runner** (`scripts/run-container-check.sh`: one CPU, 4 GiB, no extra
  swap, `-j1`): package acceptance, the development build and CTest, the development
  native matrix, the dialog audit, the studies, and the sanitize build up to the
  bounded CLI, recipe and Wayland client steps. Runs are pinned to P-core 4 through
  the caller's CPU affinity.
- **Wide runner** (a copy of that script outside the repository: six CPUs 6–11,
  10 GiB, no extra swap, `-j4`, a 10 GiB free-memory precondition): the rest of the
  sanitized native build and `texture_cache`, the sanitized native matrix, M6
  attempt 2 and the `visible_focus_input_tests` Wayland re-run. The owner asked for more CPU. The sanitize run was stopped on purpose
  during the native build, then resumed from the same build directory. Steps that
  had already finished were not re-run. The copy still prints the standard
  "one CPU, 4 GiB" banner; the recorded container limits show the wide values.
- **Clean machines.** These used the R087.u method. Reference build: four CPUs,
  4 GiB, two compiler jobs, 1,267 s. Lifecycles: two CPUs, 2 GiB, no network.

The first package acceptance attempt (`rc100`) was stopped at build step 438/643.
Its runner had pinned it to low-power core 21. It was restarted as `rc101` on
P-core 4. That stop is not a failure, and its log is kept.

Large logs and evidence are in `~/sketchyup-verification/rc-1.0.0/` and are not
published. The JSON record lists the SHA-256 of each one it cites.

This record does not cover the manual user-guide pass or the hardware checks. ADR
0161 waives both. It does not change application code, fixtures or the build image.
