# M10 Extended release acceptance

This procedure defines the gate for one Extended release; it is not an acceptance
result. M10 delivers Extended workflows "in separately gated releases"
([BUILD_PLAN.md](BUILD_PLAN.md#5-milestones-and-dependencies)), so each Extended row,
or a named group of rows, is accepted on its own release candidate with its own
record. The first is scope row F01, documentation sheets ([SCOPE.md](SCOPE.md)),
released as SketchyUp 1.1.0. Its record is `docs/verification/F01-1.1.0.md`. The second
is scope row F02, configurable components, released as SketchyUp 1.2.0 on 2026-10-17. Its record
is `docs/verification/F02-1.2.0.md`. The third is scope row F03, terrain and site tools, released as
SketchyUp 1.3.0 on 2026-10-22. Its record is `docs/verification/F03-1.3.0.md`. The fourth is
a named group of rows, the 1.4.0 group: scope rows F04 (IFC and STEP exchange), F05 (camera paths and
batch presentations), F06 (custom styles and asset catalogs) and F07 (quantities, reports and schedules);
the `.skp` import of C01, import only ([ADR 0176](decisions/0176-skp-import.md)), on the narrowed claim of owner decision 11 (2026-10-28); the delivered parts of C03 (R104.a) and C04 (R105.a), as owner decision 12 records (2026-10-28); the offline coordinates
of C06, R107.a only ([ADR 0181](decisions/0181-maps-geolocation-feasibility.md)); and the helper process
sandbox, R109 ([ADR 0183](decisions/0183-helper-process-sandbox.md)). It is one release, SketchyUp 1.4.0,
with one record, `docs/verification/G-1.4.0.md`, written at its release candidate. It was released as
SketchyUp 1.4.0 on 2026-10-29. The fifth is SketchyUp 1.5.0, which follows the same gate and is set out in
[The 1.5.0 release](#the-150-release) below. Its record is `docs/verification/G-1.5.0.md`, written at its
release candidate. It was released as SketchyUp 1.5.0 on 2026-10-31. The sixth is SketchyUp 1.6.0, which follows
the same gate and is set out in [The 1.6.0 release](#the-160-release) below. Its record is
`docs/verification/G-1.6.0.md`, written at its release candidate. It was released as SketchyUp 1.6.0 on 2026-10-31.

The gate builds on the accepted 1.0 gate ([M9 acceptance](M9_ACCEPTANCE.md),
[record](verification/M9.md)). It does not re-audit M0–M9. It shows that the
release candidate (RC) still passes every automated release check, that the Extended
row meets its acceptance text, and that the upgrade and format promises of
[ADR 0162](decisions/0162-release-versioning.md) hold against the previous release.
Every roadmap entry for the row must merge, with the CI verdict green at each merged
head (the fast tier, [ADR 0170](decisions/0170-ci-tiers.md)), before the record is
accepted; the full tier must then pass at the RC (below). For F01 those are [R090](PR_ROADMAP.md#r090),
[R091](PR_ROADMAP.md#r091) and [R092](PR_ROADMAP.md#r092); for F02,
[R093](PR_ROADMAP.md#r093) and [R094](PR_ROADMAP.md#r094); for F03,
[R095](PR_ROADMAP.md#r095) and [R096](PR_ROADMAP.md#r096). For the 1.4.0 group, those are
[R097](PR_ROADMAP.md#r097) and [R098](PR_ROADMAP.md#r098) (F04), [R099](PR_ROADMAP.md#r099) (F05),
[R100](PR_ROADMAP.md#r100) (F06), [R101](PR_ROADMAP.md#r101) (F07), [R102](PR_ROADMAP.md#r102) (C01), [R104](PR_ROADMAP.md#r104) (only R104.a, for C03), [R105](PR_ROADMAP.md#r105) (only R105.a, for C04),
[R107](PR_ROADMAP.md#r107) (only R107.a, for C06) and [R109](PR_ROADMAP.md#r109). An Extended row counts as
delivered only once its record is accepted. The group counts as delivered only once the 1.4.0 record is
accepted, and that record accepts or does not accept each of its rows. The IFC record, R097.f, and the
STEP record, R098.d, are not accepted on their own.

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

The release tag (`v1.1.0` for F01, `v1.2.0` for F02, `v1.3.0` for F03, `v1.4.0` for the 1.4.0 group, `v1.5.0` for 1.5.0, `v1.6.0` for 1.6.0) points at the RC commit itself. Records written
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
| CI | The full tier ([ADR 0170](decisions/0170-ci-tiers.md); run it with `gh workflow run native.yml --ref <branch> -f tier=full`, or it runs on the `v*` tag and `release/*` branches) passes at the RC commit, every job: the development build and checks, the six native test jobs (X11 and Wayland at 1× and 2×, audit and accessibility, and fractional scaling with the Blender worker and M5), Blender interoperability, sanitized builds, the Qt 6.8 reference compile (compile only, no tests), and Arch package acceptance (which includes the real previous-release check below). Releases up to 1.1.0 ran the same lines as four jobs | `.github/workflows/native.yml` |
| Regression and sanitizer suites | `ctest --preset dev` and `ctest --preset sanitize` pass on the RC, without suppressed application findings, with the opt-in `*_real` consumers that CI runs | [M9 walkthrough](M9_ACCEPTANCE.md#combined-release-candidate-walkthrough) |
| Native matrix | Every native suite in `native.yml`, with its exact invocation, on X11 and Wayland at 1× and 2×: the row's suites (for F01 `sheet_workspace_input_tests`, `sheet_layout_input_tests`, `sheet_annotation_input_tests`, `sheet_acceptance_input_tests`, `sheet_export_input_tests` and `visible_focus_input_tests --workspace=sheets`; for F02 `sidebar_resize_input_tests`, `component_parameter_input_tests`, `component_authoring_input_tests`, `component_parameter_state_input_tests`, `variant_instancing_input_tests`, `assembly_library_input_tests` and `component_acceptance_input_tests`; for F03 `terrain_import_dialog_tests`, `terrain_desktop_tests`, `terrain_site_desktop_tests` and `terrain_edit_input_tests`; for the 1.4.0 group `ifc_import_dialog_tests`, `ifc_export_dialog_tests`, `step_import_dialog_tests`, `step_export_dialog_tests`, `skp_import_dialog_tests`, `quantities_input_tests`, `presentation_input_tests`, `presentation_batch_input_tests`, `render_input_tests` and `style_catalog_input_tests`, with `material_input_tests` for the textured presets) and every existing suite, the AT-SPI bridge test, keyboard reachability and the all-dialog keyboard audit. The fractional 1.5× cells that CI runs also pass | Isolated Xvfb and Weston sessions with software rendering; they do not prove physical display, GPU or compositor behavior |
| Package acceptance | `packaging/arch/verify-package.sh` passes in the pinned image: install, desktop launch, MIME, recipes, GLB, reopen, the installed migrations, preferences and removal, with the upgrade from the previous release (for 1.1.0-1, from 1.0.1-1; for 1.2.0-1, from 1.1.1-1; for 1.3.0-1, from 1.2.0-1; for 1.4.0-1, from 1.3.0-1). With `SKETCHYUP_PRIOR_RELEASE_PACKAGE` set to the published previous release (SHA-256 pinned in the script), its CLI refuses every retained fixture newer than it reads, unchanged, and accepts a sheet-free document saved by the RC package. For 1.2.0 the installed package also contains the six assembly bundles in `/usr/share/sketchyup/assemblies`, each byte-identical to a rebuild from its packaged source with the packaged CLI, and removal deletes the folder. For 1.3.0 the published 1.2.0-1 reads schema 28 and no retained fixture is newer, so `check()` carries the format window: `terrain_io` requires the 1.2.0-1 CLI to refuse a schema-29 (terrain) file unchanged, and `component_parameter_io` requires it to open schema-28 files unchanged. For 1.4.0 the published 1.3.0-1 CLI reads schema 29 and refuses schemas 30 to 33 (presentation-v1, edge-styles-v1, geographic-site-v1 and material-texture-size-v1), so `check()` requires it to refuse each of those files unchanged, and `terrain_io` requires it to open the schema-29 file unchanged | [Packaging README](../packaging/arch/README.md), [ADR 0163](decisions/0163-documentation-sheets.md) and [ADR 0165](decisions/0165-constrained-component-parameters.md) and [ADR 0168](decisions/0168-terrain-import-and-surfaces.md) (downgrade window) |
| Clean machines | The RC package installs and opens migrated documents (`scripts/verify-installed-migrations.py`) on a clean machine from the pinned image and on a fresh current Arch image. A Qt 6.8 reference machine is optional (the full tier's `qt68` job already compiles every target on Qt 6.8.0, but runs no tests); the record states whether it ran, and the support matrix's Qt range claims only what ran | [R087.u](verification/R087u-clean-machine-packages.md) |
| Provider corpus | The owner approves a run of the supported provider profile against the frozen corpus at the RC (the corpus `inputSha256` values match the RC's writer). It is scored by `scripts/score-provider-release.py` from a named review; the thresholds and blocking rules of [M9 acceptance](M9_ACCEPTANCE.md) row R086 apply. The offline provider tests pass in `ctest` | Corpus v6 (from 1.4.0: the v5 tasks, prompts, limits and instructions, with the tool surface re-pinned to add the three presentation queries, R099.c); corpus v5 (from 1.3.0: the v4 tasks, prompts, limits and instructions, with the tool lists that add the three terrain queries; [ADR 0168, R095.g notes](decisions/0168-terrain-import-and-surfaces.md#implementation-notes-r095g)), [corpus v4](verification/R086e-corpus-v4.md) (1.2.0, native 15-minute task limit; [v2](verification/R086by-corpus-v2.md) for 1.1.x; [v3](verification/R086d-corpus-v3.md) is superseded without a live result), [ADR 0160](decisions/0160-provider-scope-for-1-0.md) |
| Row acceptance | The row's acceptance text in [SCOPE.md](SCOPE.md) passes as a whole on the RC. For F01: multi-page sheets, title blocks, scaled linked views, dimensions, annotations, print/PDF and model-update propagation with regression fixtures. For F02: editable parameters, constrained formulas, an attribute inspector and reusable architectural and furniture assemblies, with compatibility defined separately from SketchUp Dynamic and Live Components. For F03: contour triangulation, terrain sculpting, drape and stamp, and site coordinates with large-coordinate precision tests. For the 1.4.0 group: the acceptance text of each row in [SCOPE.md](SCOPE.md) for F04 to F07, C01 (import only) and C06 (offline coordinates only), and R109's helper sandbox. Each roadmap entry's Verify line has a passing record, and the record re-runs the row's suites at the RC | For F01: [R090.e](verification/R090e-drawing-sheets.md), [R091.h](verification/R091h-sheet-layout.md) and the R092 acceptance record; [ADR 0163](decisions/0163-documentation-sheets.md), [ADR 0164](decisions/0164-sheet-layout-interface.md). For F02: [R093.e](verification/R093e-component-parameters.md), [R094.g](verification/R094g-variant-instancing.md) and the R094.h acceptance record; [ADR 0165](decisions/0165-constrained-component-parameters.md), [ADR 0166](decisions/0166-configurable-component-interface.md). For F03: [R095.f](verification/R095f-terrain.md) and [R096.g](verification/R096g-terrain-editing.md); [ADR 0168](decisions/0168-terrain-import-and-surfaces.md), [ADR 0169](decisions/0169-terrain-editing-drape-stamp.md). For the 1.4.0 group: [R099.e](verification/R099e-presentation.md), [R100.g](verification/R100g-custom-styles-assets.md) and [R101.f](verification/R101f-quantities-reports.md) (accepted), and the IFC and STEP records [R097.f](verification/R097f-ifc-exchange.md) (with R097.g) and [R098.d](verification/R098d-step-exchange.md) (with R098.h), accepted by the 1.4.0 record; [ADR 0171](decisions/0171-ifc-exchange-subset.md), [ADR 0172](decisions/0172-step-exchange-subset.md), [ADR 0173](decisions/0173-camera-paths-batch-output.md), [ADR 0174](decisions/0174-custom-styles-asset-authoring.md), [ADR 0175](decisions/0175-quantities-reports-schedules.md), [ADR 0176](decisions/0176-skp-import.md), [ADR 0181](decisions/0181-maps-geolocation-feasibility.md) and [ADR 0183](decisions/0183-helper-process-sandbox.md) |
| Triage | A triage of every open defect at the RC shows that none is release-blocking. New defects found by the gate are added to the list | [Open-defect list](verification/R089a-defect-triage.md) |
| Documentation | The release notes record the actual version, package, format, API and provider versions, the upgrade and rollback notes and the known limitations. The support matrix declares the release, its package and the delivered row with its limits, and claims no other Extended row. The user guide describes the row's workflow | [Release notes](RELEASE_NOTES.md), [support matrix](SUPPORT_MATRIX.md), [user guide](USER_GUIDE.md) |

## Release record

`docs/verification/<row>-<version>.md` (for F01, `docs/verification/F01-1.1.0.md`; for F02,
`docs/verification/F02-1.2.0.md`; for F03, `docs/verification/F03-1.3.0.md`; for the 1.4.0 group,
`docs/verification/G-1.4.0.md`), with a machine-readable summary, records:

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

## The 1.5.0 release

SketchyUp 1.5.0 is one release with one record, `docs/verification/G-1.5.0.md`, written at its release
candidate, and it was released on 2026-10-31. It follows the gate above, with the differences below. It is not a new Extended row. It
carries three things:

- the 1.5 follow-ups of the 1.4.0 owner decisions: R098.i (STEP restart-and-exclude), R098.j (STEP
  speed), R098.k (the STEP entity cap), R099.f (render limits for terrains and large captures), R100.h (the
  Styles dialog gaps) and R100.i (catalog import progress and Cancel);
- the GeoTIFF elevation import of R107.b, as a part of C06: R107.b1, R107.b1.1, R107.b1.2, R107.b3 and
  R107.b4, at document schema 34 (`terrain-dem-v1`);
- the source credits of R107.c (R107.c1 in the data exports, R107.c2 on visible sheets, drawings and frames),
  the documentation of R107.e, and the defect fixes D-068 to D-075 (and D-076 to D-081, numbered at the RC1 triage) from the [open-defect triage](verification/R089a-defect-triage.md).

SCOPE C06 stays Investigate until R107.d is accepted ([ADR 0181](decisions/0181-maps-geolocation-feasibility.md)
§11). R107.b2 (orthophoto placement) and R107.d (geocoding) are not in 1.5.0. The buildingSMART hosted
validation of the IFC export is the owner's own task and is not a gate here.

**Roadmap entries and records 1.5.0 rests on.** [R098](PR_ROADMAP.md#r098) (its section 13 to 15 in
[R098d](verification/R098d-step-exchange.md)), [R099](PR_ROADMAP.md#r099) (section 12 of
[R099e](verification/R099e-presentation.md)), [R100](PR_ROADMAP.md#r100) (the R100.h and R100.i rows in
[R100g](verification/R100g-custom-styles-assets.md)), [R107](PR_ROADMAP.md#r107) (the ADR 0181 implementation
notes for R107.b1 to R107.e), and the defect triage [R089a](verification/R089a-defect-triage.md). The
prior release is the 1.4.0 record, [G-1.4.0](verification/G-1.4.0.md). Every merged head of those entries
must have a green CI verdict (the fast tier) before the record is accepted.

**What the 1.5.0 release record must show, in addition to the required evidence above:**

| Area | Required for 1.5.0 |
| --- | --- |
| RC | Named on `main` after this preparation merges, with `release-version.sh` printing `1.5.0`, `pkgver` 1.5.0 and `pkgrel` 1. The record gives the commit, the source-archive and package SHA-256 values and the image digest |
| CI | The full tier passes at the RC (`full-ci` label, `gh workflow run native.yml --ref <branch> -f tier=full`, or the `v*` tag). Its Arch package acceptance uses the published 1.4.0-1 as the prior release (`SKETCHYUP_PRIOR_RELEASE_PACKAGE`, SHA-256 `0d3dd150…`, pinned in `verify-package.sh` and `native.yml`) |
| Downgrade window | The released 1.4.0 CLI refuses the schema-34 GeoTIFF terrain, `tests/fixtures/terrain-dem-v34.sketchyup`, and leaves it unchanged (`terrain_io`), and opens the schema-27 to 33 fixtures unchanged. The release notes say that a document with a GeoTIFF terrain does not open in 1.4.0 |
| Split-package rollback | With `sketchyup-ifc` 1.5.0-1 installed, `pacman -U` of the 1.4.0-1 package is refused, so the record shows the documented steps (remove `sketchyup-ifc` first, then install `sketchyup-1.4.0-1`) run on a machine with both packages installed (D-071's lesson) |
| Corpus | The owner approves a fresh live run of the supported provider profile against corpus v6 at the RC. The 1.4.0 approval does not carry over. The offline checks (`provider_release_corpus`, `assistant_tool_surface`) pass in CI at the RC. The corpus `inputSha256` values match the RC's writer |
| Clean machines | The RC package installs and opens migrated documents on a clean machine, as for 1.4.0 ([R087.u](verification/R087u-clean-machine-packages.md)) |
| Accepted misses | The STEP speed budgets missed at 1.4.0 (import with a native file 10.17 s against 10 s; export 9.96 s against 5 s, from R098.j) stay accepted limits, and the record says so. The render limits of R099.f stay as measured (a 10,000-placement capture is refused by name; a 19,998-body capture prepares over the 1 s budget) |
| Triage | Every open defect at the RC is not release-blocking. D-068 to D-075 are fixed (and D-076 to D-081, numbered at the RC1 triage), and the triage count is recorded at the RC |
| Documentation | The release notes, the support matrix, the user guide, the changelog (its date set at the tag), SCOPE, the roadmap entry and ADR 0162's notes match the record. The changelog's date is set when the tag is made |

## The 1.6.0 release

SketchyUp 1.6.0 is one release with one record, `docs/verification/G-1.6.0.md`, written at its release candidate, and it was released on 2026-10-31 from RC2 (RC1 was replaced, D-088).
It follows the gate above, with the differences below. It is not a new Extended row. It carries:

- the georeferenced orthophoto as a reference image, R107.b2 (R107.b2.1 for the engine and CLI, and R107.b2.2 for the
  desktop dialog), at document schema 35 (`reference-geo-v1`). It is viewport only in 1.6.0;
- the frame credit in the camera view, R107.c3 and R107.c3.1, with the batch's **Burn source credits into frames** box;
- the user guide viewer and the guide text of R107.e2;
- the CI flake work of the 1.6.0 scope (PRs #547 and #548). No flake was fixed by evidence: D-019 became a Limitation,
  D-007's focus race was removed from ten dialog waits, D-040 is further mitigated, and D-006, D-008, D-031 and D-034 are
  diagnosable ([open-defect triage](verification/R089a-defect-triage.md)).

SCOPE C06 stays Investigate until R107.d is accepted ([ADR 0181](decisions/0181-maps-geolocation-feasibility.md) §11).
R107.d (geocoding) is held and is not in 1.6.0. The buildingSMART hosted validation of the IFC export is the owner's own
task and is not a gate here.

**Roadmap entries and records 1.6.0 rests on.** [R107](PR_ROADMAP.md#r107) (the ADR 0181 implementation notes for
R107.b2.1, R107.b2.2, R107.c3, R107.c3.1, R107.e2 (viewer) and R107.e2 (guide text), and the 1.6.0 design section), the
roadmap entry "1.6.0 CI flake fixes" in [PR_ROADMAP.md](PR_ROADMAP.md), and the defect triage
[R089a](verification/R089a-defect-triage.md). The prior release is the 1.5.0 record, [G-1.5.0](verification/G-1.5.0.md).
Every merged head of those entries must have a green CI verdict (the fast tier) before the record is accepted.

**What the 1.6.0 release record must show, in addition to the required evidence above:**

| Area | Required for 1.6.0 |
| --- | --- |
| RC | Named on `main` after the preparation merges, with `release-version.sh` printing `1.6.0`, `pkgver` 1.6.0 and `pkgrel` 1. The record gives the commit, the source-archive and package SHA-256 values and the image digest |
| CI | The full tier passes at the RC (`full-ci` label, `gh workflow run native.yml --ref <branch> -f tier=full`, or the `v*` tag). Its Arch package acceptance uses the published 1.5.0-1 as the prior release (`SKETCHYUP_PRIOR_RELEASE_PACKAGE`, SHA-256 `92c36696…`, pinned in `verify-package.sh` and `native.yml`) |
| Downgrade window | The released 1.5.0 CLI refuses the schema-35 reference image, `tests/fixtures/reference-geo-v35.sketchyup`, with `{"code":"INVALID_OPERATION","error":"Invalid required features","status":"failed"}` and exit status 1, and leaves the file unchanged (`native_format`, and `verify-package.sh` for every retained fixture newer than the released reader). It opens the schema-27 to 34 fixtures unchanged. The release notes say that a document with a georeferenced image does not open in 1.5.0 |
| Split-package rollback | With `sketchyup-ifc` 1.6.0-1 installed, `pacman -U` of the 1.5.0-1 package is refused, so the record shows the documented steps (remove `sketchyup-ifc` first, then install `sketchyup-1.5.0-1`) run on a machine with both packages installed (D-071's lesson). `verify-package.sh` does not install or read the prior `sketchyup-ifc` package, so its SHA-256 is not pinned there |
| Corpus | The owner approves a fresh live run of the supported provider profile against corpus v6 at the RC. The 1.5.0 approval does not carry over. The offline checks (`provider_release_corpus`, `assistant_tool_surface`) pass in CI at the RC. The corpus `inputSha256` values match the RC's writer, `SketchyUp/1.6.0` |
| Orthophoto | The desktop import suites pass on X11 and on Wayland at 1× and 2× (`reference_image_input_tests`, and the `reference-georeference` case of `native_accessibility_audit`), and the CLI suites `reference_image_commands`, `reference_image_io`, `geotiff` and `world_file` pass in the pinned image |
| Frame credit | The in-view suites pass with the real Blender in the CI image (`presentation_export`, `presentation_export_real`, `blender_job --real`, `presentation_batch_input_tests`, `presentation_store` and `presentation_cli`). The record states the cost of the view test against the per-frame budget of R099.f, measured, or says that it was not measured |
| Clean machines | The RC package installs and opens migrated documents on a clean machine, as for 1.5.0 ([R087.u](verification/R087u-clean-machine-packages.md)) |
| Accepted misses | No new budget miss is recorded for the 1.6.0 items. The STEP speed budgets and the render limits accepted at 1.5.0 carry over as accepted limits, and the record says so. Any budget miss the RC shows is recorded as a miss, and the owner decides it |
| Triage | Every open defect at the RC is not release-blocking. The triage holds 22 open defects (0 release-blocking, 0 Major, 7 Minor and 15 Limitation) at the 1.6.0 preparation, and the record gives the count at the RC |
| Documentation | The release notes, the support matrix, the user guide, the changelog (its date set at the tag), SCOPE, the roadmap entry and ADR 0162's notes match the record. The changelog's date is set when the tag is made |
