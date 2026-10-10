# M9 release hardening acceptance

This procedure defines the 1.0 gate; it is not an acceptance result. M8 is accepted
([record](verification/M8.md)). Every required layer of R082–R088 must merge, with
every CI job passing at each merged head, before M9 acceptance is recorded. Local
overlap on stable interfaces does not waive those prerequisites. An M9 entry counts
as delivered only once the M9 record is accepted. R089 audits the M0–M9 gates,
unresolved defects, artifact manifests and rollback/upgrade notes, writes the
accepted `docs/verification/M9.md`, and only then creates the tagged release.

## Release-candidate freeze

Name one commit on `main` as the release candidate (RC). Build its source archive
with `scripts/package-source.sh` and the Arch package from that archive. Record the
commit, archive SHA-256, package name and version from `packaging/arch/PKGBUILD`,
package SHA-256 and the pinned image digest. All release evidence is taken against
that commit and those artifacts. Evidence from earlier revisions, including every
record linked below, is supporting only; it does not show that the RC passes.

The record lists which checks were re-run at the RC and which earlier evidence is
relied on unchanged, with the reason. A later change to source, fixtures, corpus,
build image or packaged files invalidates every check that depends on it. Re-run
those checks, or name a new RC. A provider product or prompt change also requires
a new corpus version and a complete new evaluation, as R086.a states.

## Required evidence per entry

| Entry | Required at the RC | Frozen inputs and supporting evidence |
| --- | --- | --- |
| R082 | Integrated (Intel) and discrete (AMD) reference reports meet the frame, picking/inference, edit and 100 MB open/save targets in [BUILD_PLAN.md](BUILD_PLAN.md#initial-performance-budgets), using `real_model_benchmark` and `native_persistence_benchmark`. Long edit/undo sessions stay within the [memory declaration v1](verification/R082w-memory-budget-v1.md). The 1M-triangle row needs an explicit measured result or a documented decision. Any budget revision has a written rationale; no fixture is shrunk and no limit raised to pass. | [Fixture hashes](../tests/fixtures/performance/README.md); [R082.i](verification/R082i-complete-frame.md), [R082.j](verification/R082j-viewport-reuse.md), [R082.m](verification/R082m-native-file-timing.md), [R082.o](verification/R082o-edit-frame-timing.md), [R085.g](verification/R085g-current-discrete-gpu.md) |
| R083 | Fuzzed parsers and edit sequences, including the coverage-guided parser fuzzing being added as R083.e. Every minimized failure is a retained regression; `scripts/minimize-geometry-failure.py` covers geometry sequences. The 13-suite failure matrix passes normally and under ASan/UBSan. Unknown commit outcomes reconcile safely, originals survive migration failures, and no known data-loss defect remains. | [R083.d matrix](verification/R083d-local-failure-matrix.md) |
| R084 | Representative tasks complete without a mouse: keyboard modeling, Outliner, shortcut editing and dialogs. Contrast, text size and `tests/accessibility_bridge_test.py` (AT-SPI) pass on both backends at 1× and 2×. Viewport screen-reader limitations are recorded with the navigable Entity info/Outliner alternative. Old preferences migrate without losing user bindings. | [R084.c](verification/R084c-preference-compatibility.md), [R084.l](verification/R084l-linux-accessibility-bridge.md), [R084.p](verification/R084p-dialog-keyboard.md) |
| R085 | Omarchy/Hyprland native Wayland as primary and Qt X11 as a separate fallback. Supported GPU/driver configurations; NVIDIA is tested where hardware exists, otherwise stated untested. Mixed DPI and multi-monitor moves (`viewport_tests --screens` with `scripts/test-hyprland-output.py`), hotplug, suspend/resume and input methods. The 640 px and wide layouts keep actions and Measurements at fractional scale. Tested versions, recordings and known unsupported configurations are published. | [R085.b](verification/R085b-native-surface-color.md), [R085.h](verification/R085h-physical-outputs.md), [R085.i](verification/R085i-assisted-output-transitions.md) |
| R086 | Each supported provider profile meets the thresholds in `tests/provider-release-corpus.json`, scored by `scripts/score-provider-release.py` from a named review. Wrong-target mutation, false completion, unreported partial commit or unknown outcome blocks that profile. A failing profile is labeled unsupported. Unavailable and offline manual controls pass. | [R086.a corpus](verification/R086a-frozen-provider-corpus.md), [R086.b](verification/R086b-remote-evaluation.md), [R086.c](verification/R086c-local-evaluation.md) |
| R087 | Clean current and reference Arch machines install the RC package and open migrated documents (`scripts/verify-installed-migrations.py`). Upgrade from the prior package keeps preferences (`scripts/verify-package-upgrade.py`). Launch needs no provider, Blender or network. Removal, checksums and notices are checked. | [Packaging README](../packaging/arch/README.md), [R087.g](verification/R087g-clean-current-package.md) |
| R088 | The [user guide](USER_GUIDE.md) workflows run on the installed RC. A Core parity report links each of the 59 Core rows in [SCOPE.md](SCOPE.md) to passing RC evidence. Release notes record actual API, format, provider and Blender versions. | [R088.a](verification/R088a-development-guides.md), [Core evidence index](verification/R088b-core-evidence-index.md) |

## Combined release-candidate walkthrough

Build the RC with the `dev` and `sanitize` presets, then run `ctest --preset dev`
and `ctest --preset sanitize`. Run every native suite with the exact invocations in
`.github/workflows/native.yml`, on X11 and Wayland at 1× and 2×, for example:

```sh
xvfb-run -a env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 LIBGL_ALWAYS_SOFTWARE=1 \
  build/dev/keyboard_modeling_input_tests
SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/keyboard_modeling_input_tests
```

These run isolated Xvfb and Weston sessions with software rendering. They do not
prove physical display, GPU or compositor behavior. Also re-run the integrated
studies: `scripts/verify-m5-offline.sh`, `scripts/verify-m6-offline.sh`, the M7
`m7_presentation` tests and `m8_exchange_tests`. Each takes a new evidence directory.

Then install the RC package and work through every user-guide workflow by hand on
Omarchy/Hyprland and under Qt X11, each at 1× and 2×. Include a keyboard-only pass,
a save/recover/migrate cycle on real documents, and the assistant with a supported
provider and with none configured. Record each outcome, including failures.

## Required evidence across the gate

- Complete regression suite and dedicated ASan/UBSan tests, without suppressed
  application findings.
- All four CI jobs pass at the RC: development build and native tests, Blender
  interoperability, sanitized builds, and Arch package acceptance.
- The M9 exit criteria in [BUILD_PLAN.md](BUILD_PLAN.md#5-milestones-and-dependencies):
  all Core acceptance rows pass, no known data-loss/recovery or critical security
  defect remains, performance budgets pass, and the support matrix and release
  evidence are published.
- A triage list of every open defect, showing that none is release-blocking.
- A published [support matrix](SUPPORT_MATRIX.md) and known limitations,
  including Extended and Investigate gaps. Do not claim full ecosystem parity.

The accepted record must identify the RC source revision, PRs per entry, actual
tool, platform, driver and hardware versions, provider model, configuration,
latency and cost (or the R086.a reason it is unreported), fixture artifacts with
hashes, timing scope, manual outcomes and unresolved limits. No credential or
private provider artifact belongs in public evidence.
