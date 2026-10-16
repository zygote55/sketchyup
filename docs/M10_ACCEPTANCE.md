# M10 Extended release acceptance

This procedure defines the gate for one Extended release; it is not an acceptance
result. M10 delivers Extended workflows "in separately gated releases"
([BUILD_PLAN.md](BUILD_PLAN.md#5-milestones-and-dependencies)), so each Extended row,
or a named group of rows, is accepted on its own release candidate with its own
record. The first is scope row F01, documentation sheets ([SCOPE.md](SCOPE.md)),
released as SketchyUp 1.1.0. Its record is `docs/verification/F01-1.1.0.md`.

The gate builds on the accepted 1.0 gate ([M9 acceptance](M9_ACCEPTANCE.md),
[record](verification/M9.md)). It does not re-audit M0–M9. It shows that the
release candidate (RC) still passes every automated release check, that the Extended
row meets its acceptance text, and that the upgrade and format promises of
[ADR 0162](decisions/0162-release-versioning.md) hold against the previous release.
Every roadmap entry for the row must merge, with every CI job passing at each merged
head, before the record is accepted. For F01 those are [R090](PR_ROADMAP.md#r090),
[R091](PR_ROADMAP.md#r091) and [R092](PR_ROADMAP.md#r092). An Extended row counts as
delivered only once its record is accepted.

## Release-candidate freeze

Name one commit on `main` as the RC. Its version is set from its single source
(`project(SketchyUp VERSION x.y.0)` and `pkgver`, `pkgrel=1`;
`scripts/release-version.sh` passes). Build its source archive with
`scripts/package-source.sh` and the Arch package from that archive in the pinned
image. Record the commit, the archive SHA-256, the package name and version, the
package SHA-256 and the image digest. All release evidence is taken against that
commit and those artifacts. Evidence from earlier revisions, including the records
linked below, is supporting only; it does not show that the RC passes.

The record lists which checks were re-run at the RC and which earlier evidence is
relied on unchanged, with the reason. A later change to source, fixtures, corpus,
build image or packaged files invalidates every check that depends on it. Re-run
those checks, or name a new RC. A provider product or prompt change also requires a
new corpus version and a complete new evaluation, as R086.a states.

The release tag (`v1.1.0` for F01) points at the RC commit itself. Records written
after the RC, such as verification records and the release record, are evidence
about the RC, not part of its artifacts, so adding them does not invalidate it. The
installed documentation (user guide, support matrix), the release notes and the
changelog are therefore fixed before the RC is named, and the regenerated provider
corpus `inputSha256` values for the new writer string are part of the RC.

## Owner waiver of hardware evidence

[ADR 0161](decisions/0161-m9-hardware-evidence-waiver.md) applies to Extended
releases the same way it applied to 1.0. The physical and hardware checks it waives
(Intel campaigns, the sharing-off memory control, X11 output hotplug,
suspend/resume, a physical 150% output, physical pointer capture, input methods,
D-020 on Hyprland, automatic X11 output placement, an interactive Orca session and
the manual walkthrough on the installed RC) stay **untested**, not passed, and the
owner covers them in daily use. Everything below is required at the RC, including
every automated check.

## Owner trim of the local walkthrough

On 2026-10-15 the owner decided, at the 1.1.0 RC, that the local walkthrough does not
repeat what CI runs. The rule applies to every later M10 release:

- CI at the exact RC commit is the evidence for every step the workflow runs with the
  same invocation: the `ctest` presets and opt-in consumers, the native lines, the
  sanitize job, Blender interoperability and the CI package acceptance. Those steps are
  not repeated locally. A CI step that did not pass at the RC, or ran with a different
  invocation, is not covered and runs locally.
- The local walkthrough runs only what CI does not run: the release artifacts, the clean
  machines, the native cells that are not CI lines, and the row's own detail checks.
  When the sanitize job passed in CI, the local sanitized build is skipped.
- The release record lists which steps CI covered and which ran locally. Anything neither
  ran is stated as not run. It is not claimed to pass.

The 1.1.0 package shipped the text of this file without the trim, because the installed
documents are fixed at the RC. The 1.1.0 record
([F01-1.1.0](verification/F01-1.1.0.md)) applies the rule.

## Required evidence at the RC

| Area | Required at the RC | Frozen inputs and supporting evidence |
| --- | --- | --- |
| CI | All four jobs pass at the RC commit: development build and native tests, Blender interoperability, sanitized builds, and Arch package acceptance (which includes the real previous-release check below) | `.github/workflows/native.yml` |
| Regression and sanitizer suites | `ctest --preset dev` and `ctest --preset sanitize` pass on the RC, without suppressed application findings, with the opt-in `*_real` consumers that CI runs | [M9 walkthrough](M9_ACCEPTANCE.md#combined-release-candidate-walkthrough) |
| Native matrix | Every native suite in `native.yml`, with its exact invocation, on X11 and Wayland at 1× and 2×: the row's suites (for F01 `sheet_workspace_input_tests`, `sheet_layout_input_tests`, `sheet_annotation_input_tests`, `sheet_acceptance_input_tests`, `sheet_export_input_tests` and `visible_focus_input_tests --workspace=sheets`) and every existing suite, the AT-SPI bridge test, keyboard reachability and the all-dialog keyboard audit. The fractional 1.5× cells that CI runs also pass | Isolated Xvfb and Weston sessions with software rendering; they do not prove physical display, GPU or compositor behavior |
| Package acceptance | `packaging/arch/verify-package.sh` passes in the pinned image: install, desktop launch, MIME, recipes, GLB, reopen, the installed migrations, preferences and removal, with the upgrade from the previous release (for 1.1.0-1, from 1.0.1-1). With `SKETCHYUP_PRIOR_RELEASE_PACKAGE` set to the published previous release (SHA-256 pinned in the script), its CLI refuses every retained fixture newer than it reads, unchanged, and accepts a sheet-free document saved by the RC package | [Packaging README](../packaging/arch/README.md), [ADR 0163](decisions/0163-documentation-sheets.md) (downgrade window) |
| Clean machines | The RC package installs and opens migrated documents (`scripts/verify-installed-migrations.py`) on a clean machine from the pinned image and on a fresh current Arch image. A Qt 6.8 reference machine is optional; the record states whether it ran, and the support matrix's Qt range claims only what ran | [R087.u](verification/R087u-clean-machine-packages.md) |
| Provider corpus | The owner approves a run of the supported provider profile against the frozen corpus at the RC (the corpus `inputSha256` values match the RC's writer). It is scored by `scripts/score-provider-release.py` from a named review; the thresholds and blocking rules of [M9 acceptance](M9_ACCEPTANCE.md) row R086 apply. The offline provider tests pass in `ctest` | [Corpus v2](verification/R086by-corpus-v2.md), [ADR 0160](decisions/0160-provider-scope-for-1-0.md) |
| Row acceptance | The row's acceptance text in [SCOPE.md](SCOPE.md) passes as a whole on the RC. For F01: multi-page sheets, title blocks, scaled linked views, dimensions, annotations, print/PDF and model-update propagation with regression fixtures. Each roadmap entry's Verify line has a passing record, and the record re-runs the row's suites at the RC | For F01: [R090.e](verification/R090e-drawing-sheets.md), [R091.h](verification/R091h-sheet-layout.md) and the R092 acceptance record; [ADR 0163](decisions/0163-documentation-sheets.md), [ADR 0164](decisions/0164-sheet-layout-interface.md) |
| Triage | A triage of every open defect at the RC shows that none is release-blocking. New defects found by the gate are added to the list | [Open-defect list](verification/R089a-defect-triage.md) |
| Documentation | The release notes record the actual version, package, format, API and provider versions, the upgrade and rollback notes and the known limitations. The support matrix declares the release, its package and the delivered row with its limits, and claims no other Extended row. The user guide describes the row's workflow | [Release notes](RELEASE_NOTES.md), [support matrix](SUPPORT_MATRIX.md), [user guide](USER_GUIDE.md) |

## Release record

`docs/verification/<row>-<version>.md` (for F01, `docs/verification/F01-1.1.0.md`),
with a machine-readable summary, records:

- the RC commit, artifacts and hashes, and the image digest;
- the PRs per roadmap entry, and CI at the RC;
- tool, platform and Qt versions, and the result of each check above, with the
  commands and evidence hashes;
- which checks were re-run at the RC and which earlier evidence is relied on, with
  the reason, and which steps CI covered, which ran locally and which did not run (see
  the owner trim above);
- the provider model, configuration, latency and cost (or the R086.a reason it is
  unreported);
- the triage result, the unresolved limits and the checks waived by ADR 0161.

No credential or private provider artifact belongs in public evidence. The owner
approves the tag and the release after the record is accepted.
