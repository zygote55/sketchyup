# R094.h — Configurable components: acceptance

2026-10-17. Branch `build/r094h-acceptance`, based on `f02/integration` at `0f966c17` (R093.a–e,
R094.a–g, the sidebar teardown fix and the 1.2.0 release preparation) and merged with
`f02/integration` at `c6265f77` (the hosted-opening fix that validates solids at exact loop
corners). Everything below ran at **`8dbf91f9`**, that merge, unless a section says otherwise:
the final native matrix, the sanitized lines, the host build and CTest, and the package. The
earlier runs at `5b45a404`/`f3b3a9d8` are kept under [Earlier runs](#earlier-runs).
[Summary](R094h-configurable-components.json).

R094 delivers the configurable component interface of
[ADR 0166](../decisions/0166-configurable-component-interface.md) over the records and queries of
[ADR 0165](../decisions/0165-constrained-component-parameters.md) (R093): R094.a the resizable
sidebar, R094.b the core and command additions, R094.c Entity info parameters, R094.d definition
authoring, drivers and the Preview ghost, R094.e states, refusals and dialogs, R094.f the six
assemblies and the Components tab, R094.g instanced variants, and this layer. This record checks
the roadmap's Verify line. It is local verification: CI is not claimed and runs once at the end
of F02.

## The Verify line

| Clause | Evidence | Result |
| --- | --- | --- |
| Overrides and shared defaults behave as specified across undo, redo, save and reopen | The [§10 script](#the-adr-0166-10-script), steps 2–4 and 6–7, in every cell | pass |
| Regeneration keeps the edit scope and materials | Step 3: after the default Width changes, the context and the selection are kept, the top regenerates to 1000 mm and keeps its paint | pass |
| A hand edit inside the definition shows Changed with Rebind | Step 3: moving the shelf makes Repeat Shelf and Visibility Shelf Changed; the strip names them and offers Rebind; Rebind all clears every Changed driver in one entry | pass |
| The ADR records R094.a–h as complete | [ADR 0166](../decisions/0166-configurable-component-interface.md) status and "Implementation notes (R094.h)"; the [roadmap](../PR_ROADMAP.md#r094) R094 status | done |

The R094 row's own Verify line ("Instance overrides and shared defaults behave predictably;
regeneration preserves edit scope/materials and warns about incompatible manual changes") is the
same three clauses.

## Changes in R094.h

- **`component_acceptance_input_tests`** (new; `sketchyup_native_fixture`): the
  [§10 script](#the-adr-0166-10-script). It runs in `native.yml`'s X11 1× and 2× lines (120 s
  deadline), the Wayland 1× and 2× lines, and the sanitized Wayland step (target list and line).
  `.github/ci-line-inventory.txt` is regenerated; `ci-line-inventory.py check` reports
  "Inventory matches: 511 lines (507 distinct)".
- **`keyboard_reachability_tests`** gains the Components tab case that R094.f left out: View ›
  Components shows the tab and focuses Show; Tab from the Tray's tab bar enters the page and
  Shift+Tab returns; the section header, Show and the cards are named Tab stops and the cards are
  one stop; arrows move between cards; Enter on a card opens the values form, whose fields, Use
  defaults and Cancel are named Tab stops; Esc ends placement without losing focus. The suite
  depends on `sketchyup_assemblies` when the CLI is built.
- **`drawing_sheet_io_tests`** (fix, found by package acceptance): with the 1.1.1-1 CLI as
  `SKETCHYUP_PRIOR_CLI`, the test expected the prior release to refuse a schema-27 document with
  items. 1.1.1 reads schema 27, so `check()` failed and the whole package job failed at the base.
  The test now asks the prior release which schemas it reads (`--format-capabilities`): one that
  reads 27 must open the sheet document and the v27 item fixture unchanged; an older one must
  still refuse them unchanged. Checked with the extracted 1.1.1 CLI ("reads up to 27") and with no
  prior CLI.
- **`accessibility_bridge_test.py`** (fix): it looked up the "has its own values" dialog's
  buttons in the whole application. R094.f's Components tab has its own "Cancel" in its hidden
  values form, so the lookup found two and the test failed in all four cells (first run below).
  The buttons are now found inside the dialog's subtree. No product change.
- **[UX_DESIGN](../UX_DESIGN.md)** §14 "Configurable components" (the Info tab's sections,
  definition authoring, a placement with its own values, the Components tab and assemblies,
  accessibility) and `Shift+D` in the §4.2 shortcut table; §5's Components row points to it.
- **Release documents.** Every `<!-- R094.f: confirm -->` marker is resolved; see
  [Release-document markers](#release-document-markers).
- **ADR 0166** status records R094.a–h as complete, with R094.h implementation notes; the
  roadmap marks R094 delivered, CI not yet claimed.

## The ADR 0166 §10 script

`tests/component_acceptance_input_tests.cpp`. The model holds the ADR 0165 §1 table fixture
(`parameter_fixture::makeTable`: "Table" at its defaults, one with Width 1200 mm, Shelves 3 and
Finish Ash, and one nested in a room whose Child parameter driver sets its width) and an 8 m
wall, 200 mm thick and 2.7 m high, in millimetres. Keys go to the focused widget; pointer input
is real mouse events on the canvas.

1. **Place the dining table with values.** Components tab, arrows and Enter on Dining table,
   Width 1800 in the values form, `[7000,4000,0]` in the Measurements box: one entry, "Place
   Dining table", holding only `width = 1.8 m`; the Tray shows Entity info with "1800 mm".
2. **Override and reset.** On the table at its defaults, Entity info: Width 1500 Enter ("Change
   Width"), Shelves ↑ ("Change Shelves", 2); the other placements and the shared definition are
   untouched; the Shelves reset button ("Reset Shelves") leaves Width.
3. **Edit definition at defaults.** Enter on that table opens "Table has its own values" ("Width
   1500 mm differs from the defaults. …"); Enter on the default button suspends the values ("Edit
   Table at defaults") and the banner says they return. Inside: paint the top; change the default
   Width to 1000 mm ("Edit parameter Width": the context, the selection and the top's colour are
   kept, the top regenerates to 1.0 m); Shift+D on the right back leg, Visibility, `open` ("Add
   Visibility driver"); move the shelf by hand with the Move tool; the strip names Repeat Shelf
   and Visibility Shelf and offers Rebind, and Drivers shows them Changed; Rebind all ("Rebind 2
   drivers"; "Rebind 3 drivers" before the D-038 fix, see [Findings](#findings)); Esc closes
   the definition and restores the table's Width ("Restore Table values").
4. **Make unique and edit.** Enter on the table with its own values, Shift+Tab, Space on Make
   unique and edit ("Make Table unique"): a new definition whose default Width is 1.2 m and no
   overrides; Shelves' default 2 in the copy ("Edit parameter Shelves"); closing adds nothing and
   the shared definition keeps Shelves 1.
5. **Openings.** Interior door, Width 1000, the ghost on the wall face, a click: "Place Interior
   door" cuts a 1000 mm opening. Casement window, a click: "Place Casement window" cuts 1200 mm.
   Door Width 1100 in Entity info: "Change Width" re-cuts to 1100 mm; the window's opening is
   unchanged; the wall holds two openings.
6. **History.** The sixteen entries, in order: Place Dining table · Change Width · Change Shelves
   · Reset Shelves · Edit Table at defaults · Edit shared component definition (paint) · Edit
   parameter Width · Add Visibility driver · Edit shared component definition (move) · Rebind 2
   drivers · Restore Table values · Make Table unique · Edit parameter Shelves · Place Interior
   door · Place Casement window · Change Width. Sixteen Undos return the records to the opened
   model's (the encoded document without the revision and allocator marks, which undo keeps by
   design); sixteen Redos return them to the finished session's, with the values, the unique copy
   and both openings.
7. **Save and reopen.** File › Save; reopen: the records are byte-identical to the finished
   session's, and the values and openings reopen. The file is removed and saved again: the
   second file is byte-identical to the first (64,600 bytes in every cell).

| Platform | Scale | Result | Seconds | Checks | Entries | Second save identical |
| --- | --- | --- | ---: | ---: | ---: | --- |
| X11 | 1× | pass | 6.0 | 180 | 16 | yes |
| X11 | 2× | pass | 7.9 | 180 | 16 | yes |
| Wayland | 1× | pass | 5.1 | 180 | 16 | yes |
| Wayland | 2× | pass | 7.8 | 180 | 16 | yes |
| Wayland 2×, sanitized | 2× | pass | — | 180 | 16 | yes |

Seconds are the container's wall time, including start-up. Each run prints a JSON summary; the
file digests differ between runs because each run saves a new document identity.

**How the script reads the ADR.** The script's "dining table with values" is the bundled Dining
table, placed with a value from the Components tab. The bundled dining table has no shelves, so
the steps on "width and shelves", the hand-edited shelf and the three placements use the ADR 0165
table, the only definition with both a Shelves parameter and a shelf driven by Repeat and
Visibility. Its three placements are the fixture's three tables. ADR 0166's "Implementation
notes (R094.h)" record this.

## Native matrix

Every `native.yml` line of the F02 suites, the extended suites and `hosted_input_tests`
(the hosted-opening fix's suite), verbatim, one container per line, in the pinned image
`sketchyup-ci-pinned:r094a`, three lanes, `--cpus=4 --memory=8g`, as uid 1000. Host load
average 2–13 during the run; 22 CPUs.

**Result: 74 of 74 lines pass.** No cell needed a rerun for a test failure. The four bridge
cells first exited 127 because `dbus-run-session` cannot start as a uid with no passwd entry in
the image (environment); they were run again as root, as the CI job runs, and passed.

| Suite | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
| --- | ---: | ---: | ---: | ---: |
| `sidebar_resize_input_tests` | 6.2 | 9.0 | 5.1 | 8.6 |
| `component_parameter_input_tests` | 6.0 | 6.1 | 3.7 | 4.2 |
| `component_authoring_input_tests` | 8.0 | 9.8 | 6.7 | 9.9 |
| `component_parameter_state_input_tests` | 7.2 | 8.5 | 6.2 | 12.9 |
| `variant_instancing_input_tests` | 12.2 | 13.4 | 13.3 | 15.3 |
| `assembly_library_input_tests` | 6.9 | 7.5 | 4.6 | 6.7 |
| `component_acceptance_input_tests` | 6.0 | 7.9 | 5.1 | 7.8 |
| `hosted_input_tests` | 5.1 | 5.7 | 3.1 | 4.6 |
| `entity_info_input_tests` | 4.5 | 4.8 | 2.4 | 5.0 |
| `text_size_input_tests` | 7.0 | 8.8 | 5.2 | 9.6 |
| `theme_input_tests` | 4.2 | 4.5 | 2.1 | 2.1 |
| `keyboard_reachability_tests` | 7.4 | 17.1 | 14.8 | 30.4 |
| `shortcut_input_tests` | 5.0 | 7.5 | 4.0 | 10.3 |
| `visible_focus_input_tests` (Model) | 10.4 | 27.5 | 18.3 | 42.4 |
| `visible_focus_input_tests --workspace=sheets` | 8.5 | 18.3 | 13.2 | 32.1 |
| `keyboard_panel_tabs_tests` | 5.0 | 7.7 | 4.6 | 7.0 |
| `library_input_tests` | 4.3 | 4.5 | 2.3 | 3.4 |
| `accessibility_bridge_test.py` | 8.9 | 9.4 | 7.5 | 11.8 |

Seconds of container wall time, including start-up.

- **`component_parameter_input_tests` at 1.5** (the fractional lines): Wayland 3.7 s and X11
  6.0 s, both pass.
- **All-dialog keyboard audit** (`native_accessibility_audit`, the CI step verbatim): the
  inventory is clean (108 dialogs, no uncovered "…" action; the only unexercised surface is the
  file-operation progress tooltip, by design), and all 17 groups pass on X11 2× and Wayland 1×
  (154.9 s). In the earlier run the `components` group, which holds the parameters-scope dialog,
  also passed on X11 1× and Wayland 2× (extra cells).
- **AT-SPI bridge:** all four cells read the parameter field, header, buttons, the values dialog
  ("Shelf unit has its own values", its description and three buttons), the authoring sections,
  trees and row, the Preview field and the sidebar handle.
- **Wayland harness limit:** the longest Wayland cell is `visible_focus_input_tests` at 2× (42.4 s
  of Weston's fixed 60 s), as R091.h and R094.g recorded.

### Earlier runs

At `5b45a404` (binaries) with the bridge test of `f3b3a9d8`, before the merge: 70 of 70 lines
passed in the same configuration, with the same results for every suite (the acceptance suite
7.9, 10.1, 5.9 and 9.8 s), after the four bridge cells first failed on the duplicate "Cancel"
(fixed in the test) and passed on their rerun; the dialog audit passed (178.8 s); both sanitized
lines passed with no reports; `ctest` passed 198/198; and package acceptance failed in `check()`
on `drawing_sheet_io` (fixed, see above) and then passed at `bb2533a1`.

## Sanitizers

The CI step's build, in the pinned image, as uid 1000: `scripts/ci-wayland-client.sh` built the private
Wayland 1.26.0 client with the destroyed-proxy fix (its probe passes: "Client/server and every
owned proxy destroyed"); `cmake --preset sanitize -DSKETCHYUP_BUILD_CLI=ON
-DSKETCHYUP_BUILD_DESKTOP=ON` and the two targets. Both lines ran as the step runs them, with
`SKETCHYUP_WAYLAND_CLIENT_LIBRARY` set and `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`:

| Line (Wayland 2×, ASan/UBSan) | Result | `runtime error` lines | LSan or ASan reports |
| --- | --- | ---: | ---: |
| `build/sanitize/component_acceptance_input_tests` | pass, 180 checks | 0 | 0 |
| `build/sanitize/component_parameter_input_tests` | pass, 232 checks | 0 | 0 |

## CTest

| Check | Where | Result |
| --- | --- | --- |
| `cmake --build --preset dev` | host, GCC 16.2.1, Qt 6.11.2-3, `-DSKETCHYUP_NATIVE_FIXTURES_IN_ALL=OFF` | builds (10.9 min, 8 jobs) |
| `ctest --preset dev -E "_real$"` | host, `-j6` | 198/198 pass |

The host tree left the native fixtures out of `ALL` because the shared disk was nearly full
(down to 214 MB free during the first build); every native fixture the matrix uses was built in
the pinned image instead, from the same commit. No CTest test is a native fixture.

## R094.g measurements

Unchanged by R094.h, which adds no product code; see the
[R094.g record](R094g-variant-instancing.md). In short: framebuffer and pick hashes are equal
with sharing on and off in ten states; at 1,000 and 10,000 placements the variant family's
body-cache and GPU payloads equal the default family's (ratio 1.00, against the 10 % criterion);
software frame and edit p95 at 10,000 placements are within run-to-run noise of the repeated
family, and neither family meets the hardware budgets on llvmpipe (the AMD session is waived,
ADR 0161). `variant_instancing_input_tests` passes in all four cells above.

## Installed smoke

The `native.yml` "Arch package acceptance" job's steps, at `8dbf91f9`, in the pinned image
`sketchyup-ci-pinned:r094a` (cmake 4.4.3-2, gcc 16.2.1, qt6-base 6.11.2-3, qt6-wayland
6.11.2-1), as root with `SKETCHYUP_PACKAGE_SANDBOX=1`, `CMAKE_BUILD_PARALLEL_LEVEL=8`, and
`SKETCHYUP_PRIOR_RELEASE_PACKAGE` set to the published `sketchyup-1.1.1-1-x86_64.pkg.tar.zst`
(downloaded from the v1.1.1 release; SHA-256 `58ddfed5…9c2b` checked by the job's pin and by
`verify-package.sh`). Source archive `sketchyup-1.2.0.tar.gz` `2e458255…c532`.
**Result: pass** ("Install, desktop launch, MIME, upgrade, reopen and removal acceptance passed").

- `check()`: CTest 210 tests, 100 % passed, the 12 optional `*_real` consumers skipped;
  `drawing_sheet_io` and `component_parameter_io` drive the 1.1.1 CLI ("Prior release
  sketchyup-1.1.1-1-x86_64.pkg.tar.zst: sketchyup-cli 1.1.1").
- The 1.1.1-1 baseline package installs, then 1.2.0-1 upgrades it ("upgrading sketchyup…",
  261 files, 0 altered).
- **"Installed 6 configurable assemblies, byte-identical to a rebuild from their sources."**
  The six bundles are in `/usr/share/sketchyup/assemblies`, owned by the package, and each equals
  a rebuild from the packaged source with the installed `sketchyup-cli --build-assembly`.
- "Prior release reads schemas up to 27: refused 1 newer fixtures unchanged and accepted this
  package's sheet-free save."
- "Installed CLI migrated 32 historical fixtures; all preservation checks passed."
- Removal deletes the package and `/usr/share/sketchyup/assemblies`.

The earlier attempts are under [Earlier runs](#earlier-runs): at `f3b3a9d8` `check()` failed on
`drawing_sheet_io`; at `bb2533a1` the whole job passed with the same lines.

## Release-document markers

Each `<!-- R094.f: confirm -->` claim was checked against the merged R094.f code and its notes.

| File | Claim | Outcome |
| --- | --- | --- |
| `CHANGELOG.md` | Six assemblies in a Components tab; doors and windows cut their opening; `--build-assembly` | Correct. Added View → Components, "placed with values" and reuse of the in-model definition |
| `docs/RELEASE_NOTES.md`, Assemblies | Choose, set values, click to place; openings in the same step; reuse | Correct. Added View → Components, the status bar, snapping to the wall face in whole millimetres, and Enter at a selected face's centre |
| `docs/RELEASE_NOTES.md`, CLI | `--build-assembly SOURCE OUTPUT.sketchylib` | Correct. Added that the output must be a new path and that builds are byte-identical |
| `docs/RELEASE_NOTES.md`, Known limitations | "The Outliner's Enter opens such a placement without asking" | **Wrong**: since R094.f, Enter or a double-click in the Outliner opens the same dialog (`OrganizationPanel::enter` calls `Viewport::openContext`; `component_parameter_state_input_tests` checks it). Rewritten |
| `docs/USER_GUIDE.md` | Placing an assembly | Correct. Added View → Components, the status bar, millimetre snapping, the face's centre, and the Measurements box (`[x,y,z]`, a wall anchor or an inset, and amending) |
| `docs/SUPPORT_MATRIX.md` | Six bundled assemblies in the Components tab | Correct. Added the installed folder, `/usr/share/sketchyup/assemblies` (the desktop looks in `../share/sketchyup/assemblies` beside its executable, then the build tree) |
| `docs/M10_ACCEPTANCE.md`, native matrix | `assembly_library_input_tests` in the matrix | Correct; it and `component_acceptance_input_tests` are `native.yml` lines |
| `docs/M10_ACCEPTANCE.md`, package | The installed package contains the six bundles | Correct. Added the folder, the byte-identical rebuild with the packaged CLI, and removal |

R094.f's other deviations need no release-document change: the ghost comes from a private dry
run (an implementation detail; the documents say only that a ghost follows the pointer).

## Findings

- **A paint makes a driver Changed.** A driver's `bound` digest covers its target member's whole
  record (ADR 0165 §4), colour included. Painting the top makes Stretch Top Changed, and the
  strip says it "now starts from a changed shape". This is the specified state; the strip's
  wording speaks of shape where only the colour changed. The script expects it (three Changed
  drivers, "Rebind 3 drivers"). Whether a colour change should count, or the sentence should
  change, is for the owner. **Fixed by D-038 (2026-10-17):** a paint changes no shape, so it
  restamps the drivers on the painted member, which stay bound
  ([ADR 0165 addendum](../decisions/0165-constrained-component-parameters.md#addendum-d-038-2026-10-17-a-paint-keeps-its-drivers-bound)).
  The script now expects no strip after the paint, two Changed drivers after the move, and
  "Rebind 2 drivers".
- **The bridge test fails at the base too.** The duplicate "Cancel" comes from R094.f's
  Components tab, which R094.h does not change, so `0f966c17` fails the same way (inferred from
  the cause; the base was not rerun). Fixed in the test.

- **Package acceptance failed at the base.** `drawing_sheet_io_tests` still expected a 1.0.x
  prior release; with the 1.1.1-1 baseline `check()` failed. Fixed in the test (above).
- **Millimetre wall anchors.** R094.f's placing mode moves a door or window's anchor in whole
  millimetres to avoid a core refusal that the merged hosted-opening fix (`c6265f77`) now
  removes. The snapping is still in the viewport, so the release documents describe it as
  shipped; whether to keep it is a product decision outside R094.h.

## Commands

Build (pinned image; the Debug tree, only the targets the matrix runs):

```sh
docker run --rm --cpus=6 --memory=12g -u 1000:1000 -v "$WORKTREE":/w -v "$BUILD":/w/build/dev -w /w \
  -e CCACHE_DIR=/ccache -e CMAKE_C_COMPILER_LAUNCHER=ccache -e CMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -v "$CCACHE":/ccache sketchyup-ci-pinned:r094a \
  bash -c 'cmake --preset dev -DSKETCHYUP_BUILD_CLI=ON -DSKETCHYUP_BUILD_DESKTOP=ON &&
           cmake --build --preset dev --parallel 6 --target sketchyup sketchyup-cli sketchyup_assemblies blender_job_tests <suites>'
```

Native line (one container per `native.yml` line, verbatim):

```sh
docker run --rm -u 1000:1000 -e HOME=/tmp --cpus=4 --memory=8g --cap-add=SYS_PTRACE -e QT_FORCE_STDERR_LOGGING=1 \
  -v "$WORKTREE":/w -v "$BUILD":/w/build/dev -w /w sketchyup-ci-pinned:r094a bash -c "$LINE"
```

Sanitized lines:

```sh
export SKETCHYUP_WAYLAND_CLIENT_LIBRARY=$PWD/build/ci-wayland-client/lib \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
QT_WAYLAND_DISABLE_WINDOWDECORATION=1 QT_QPA_PLATFORMTHEME=generic QT_STYLE_OVERRIDE=Fusion \
  QT_IM_MODULE=compose SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/sanitize/<suite>
```

(The bridge lines ran without `-u`, as root: `dbus-run-session` needs a passwd entry.)

Package acceptance (the `native.yml` job's steps, in the pinned image as root):

```sh
docker run --rm --cpus=8 --memory=16g -e CMAKE_BUILD_PARALLEL_LEVEL=8 -e SOURCE_DATE_EPOCH="$EPOCH" \
  -e SKETCHYUP_PRIOR_RELEASE_PACKAGE=/work/release/sketchyup-1.1.1-1-x86_64.pkg.tar.zst \
  -v "$WORKTREE":/src:ro -v "$RELEASE":/work/release:ro -w /src sketchyup-ci-pinned:r094a \
  bash -c 'useradd --create-home builder; scripts/package-source.sh /work/package;
           cp packaging/arch/verify-package.sh /work/package/;
           SKETCHYUP_PACKAGE_SANDBOX=1 bash /work/package/verify-package.sh'
```

## Caveats

- Local runs in the pinned image, not CI. Isolated Xvfb and Weston sessions with software
  rendering; no physical display, GPU or compositor.
- Timings come from a shared host and include container start-up.
- The host build left native fixtures out of `ALL` (disk); see [CTest](#ctest).
