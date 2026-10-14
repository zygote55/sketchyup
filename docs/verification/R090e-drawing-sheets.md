# R090.e — Drawing sheets and linked views

2026-10-14. Product and fixtures on branch `build/r090e-sheets-verification` at
`ac61ccee` (R090.d, PR #313 head `ababdaf2`, plus the two R090.e commits below).
Later commits on the branch change documentation only.
[Summary](R090e-drawing-sheets.json).

R090 adds drawing-sheet and linked-view records ([ADR 0163](../decisions/0163-documentation-sheets.md)):
R090.a records (#310), R090.b schema 27 persistence (#311), R090.c linked-view
evaluation (#312) and R090.d commands and queries (#313). This record checks the
roadmap Verify line: "Page and view records round trip; changing model geometry
updates or marks linked views stale without changing declared scale". R090 has no
desktop UI; that is R091.

## Changes in R090.e

- **Generated API schemas.** `scripts/generate-api-schemas.py` regenerates the ten
  `docs/api` files that `scripts/generate-tool-reference.py` does not cover. It runs
  the built CLI (`--capabilities`, `--session-capabilities`, `--mcp-capabilities`,
  `--recipe-capabilities`, `--extension-capabilities`, `--format-capabilities`) and
  desktop binary (`--inspection-capabilities`, `--mcp-inspection-capabilities`). Each
  file is written byte for byte in its existing layout. `--check` fails on drift and
  names the files. CI runs it after the tool-reference check in `native.yml`. No
  `docs/api` file is maintained by hand; [ADR 0138](../decisions/0138-generated-tool-reference.md)
  now says so.
- **D-030 fix.** The shared schema validator ignored `properties`, `required` and
  bounds in a schema without `type`. Both `oneOf` choices of `annotation.create`
  (distance, label) have no `type`, so both matched every request and the
  "exactly one" count failed. A schema without `type` now applies its keywords to
  the kind of value it meets. [D-030](R089a-defect-triage.md) records the class
  and impact.

## Results

| Check | Where | Result |
| --- | --- | --- |
| `ctest --preset dev` | host, Qt 6.11.2, GCC 16.2.1, `-j6` | 194/194 pass; 11 optional consumer tests skipped |
| `ctest --preset headless` | host | 63/63 pass |
| `ctest --preset sanitize` (core targets, as CI's first sanitized step) | host | 63/63 pass |
| Sanitized R090 suites and `transaction_dispatch` (`-DSKETCHYUP_BUILD_CLI=ON`, the CI step's configuration) | host | 5/5 pass |
| `generate-api-schemas.py --check` | dev binaries, with and without a display | 10 files match |
| `generate-tool-reference.py --check` | dev CLI | 2 files match |
| Golden drawing fingerprints | dev and sanitize `drawing_view_tests` | identical, see below |
| D-030 before the fix | 1.0.0 CLI and R090.d dev CLI, `--session` | `annotation.create` rejected (distance and label) |
| D-030 after the fix | dev CLI, `--session`; `transaction_dispatch_tests` | distance accepted through `--session`; distance and label accepted and three invalid requests rejected in the test |
| Downgrade window | tag-built 1.0.0 CLI, local | refuses three schema-27 files unchanged; accepts two sheet-free saves |
| Downgrade window | CI, PR #311 package job | refused 1 newer fixture unchanged; accepted the sheet-free save |
| Native save and recovery smoke | container, X11 and Wayland at 1× | 4/4 pass |
| Arch package build, check and lifecycle | container, pinned image | pass; check phase 194/194 with 11 optional consumers skipped |
| Installed sheet smoke and downgrade window | same container, installed CLI | pass |

## R090 suites against the Verify line

- `drawing_sheet_records` (`tests/drawing_sheet_record_tests.cpp`, R090.a). Sheet and
  view records validate bounds, names and frames inside the paper. View IDs are
  global, and a view moves between sheets. Undo never rewinds an allocator. Deleting
  a scene keeps the view record. Every sheet edit leaves model records
  pointer-identical, and the reverse. This is the in-memory half of "records round
  trip".
- `drawing_sheet_io` (`tests/drawing_sheet_io_tests.cpp`, R090.b). Every field round
  trips through the container, the raw document, save, `.bak` and recovery
  (`sameSheets`), and the container re-encodes byte-identically. A broken scene
  link survives the round trip. 29 historical fixtures load with empty sheet
  tables and migrate to schema 26 with the sources unchanged. Unknown,
  missing or noncanonical fields are rejected. A sheet-free document saves as
  schema 26, adding a sheet writes 27, deleting every sheet writes 26 again, and
  undo writes 27. With `SKETCHYUP_PRIOR_CLI` set to the 1.0.0 CLI, it also checks
  that 1.0.0 accepts the sheet-free file and refuses the schema-27 file unchanged.
  This is the persisted half of "records round trip".
- `drawing_views` (`tests/drawing_view_tests.cpp`, R090.c). Moving geometry inside
  the crop makes an accepted view `stale` while the declared scale stays 1:50 and
  the view record is unchanged. A
  revision-only change, an undo to equal content and an edit outside the crop stay
  `current`. A scene update gives `stale`; deleting the scene gives `broken`
  (`sceneMissing`), and undo returns it to `current`. Camera zoom never changes the
  declared scale. 2 m at 1:50 measures 40 mm at a frame offset. A perspective scene
  is `notToScale`, not `broken`. This is "marks linked views stale without changing
  declared scale".
- `drawing_sheet_commands` (`tests/drawing_sheet_command_tests.cpp`, R090.d). Every
  operation is one sheet-only history entry with a pointer-identical model. Batches
  are atomic; no-ops and stale revisions are rejected. Accept stamps the evaluated
  revision and fingerprint, and undo and redo restore it. Broken views are refused.
  Status is paged. Component scope is rejected. A CLI script round trips to schema
  27. This is the same Verify line through the shared command and query surface.

R090 does not update a view's content automatically; it marks the view stale until
it is accepted again (ADR 0163). The Verify line allows either.

## Golden fingerprints

`drawing_view_tests` prints and asserts two recorded digests. The dev build and
the sanitized build print the same values:

- orthographic plan: `2979d0391bf8426b5ee74adbd67c053ac4a06fc22ef6e8384dda2b3dc4b5e487`
- perspective: `01ccf7a9736d87ba2e16b8319f07b10a400f053e256b76a2641a33a1a99b6978`

## Downgrade window

ADR 0163 decision 2 keeps sheet-free documents at schema 26 so SketchyUp 1.0.0
still opens them. A document with a sheet is schema 27, which 1.0.0 refuses.

- **CI.** The PR #311 run's Arch package acceptance job
  ([run 38517673211](https://github.com/zygote55/sketchyup/actions/runs/38517673211/job/115700028489))
  downloaded the 1.0.0-1 release package, checked its pinned SHA-256 and unpacked
  its CLI (`sketchyup-cli 1.0.0`). It printed: "Prior release reads schemas up to
  26: refused 1 newer fixtures unchanged and accepted this package's sheet-free
  save."
- **Local re-run.** The same steps with the CLI built from tag `v1.0.0`
  (`.local/wt-v100/build/headless/sketchyup-cli`) and the R090.e dev CLI. 1.0.0
  reports `readDocumentVersions` up to 26. It refuses `drawing-sheets-v27.sketchyup`
  and the two schema-27 files from the sheet smoke below with
  `{"status":"failed","code":"INVALID_OPERATION","error":"Unsupported container field"}`.
  All three files are byte-identical afterwards. It accepts `examples/saved-scenes.json`
  saved by the dev CLI and the smoke's model before its sheet, both
  `{"valid":true,"documentVersion":26}`. `drawing_sheet_io_tests` with
  `SKETCHYUP_PRIOR_CLI` set to the same 1.0.0 CLI also passes.

## D-030 evidence

`tests/transaction_dispatch_tests.cpp` gains a block that first failed with
"Parameter must match exactly one supported schema" and passes after the fix:

- A synthetic schema whose two `oneOf` choices have no `type` and differ only in a
  property `const`. `{"kind":"a"}` and `{"kind":"b"}` pass; `{"kind":"c"}` is
  rejected. Choices that differ only in `required` reject both-present and
  neither-present objects.
- The shipped `annotation.create` schema through `transaction.apply`. A distance
  with two point anchors and a label with one anchor and text are accepted in one
  apply. A distance with one anchor, a label with empty text and a label without
  text are rejected with `INVALID_REQUEST`.

A real `--session` (`--new --output … --outcomes …`) applying `annotation.create`
in a transaction returned `INVALID_REQUEST` "Parameter must match exactly one
supported schema" for both a distance and a label on the 1.0.0 CLI and on the
R090.d dev CLI. After the fix the distance is accepted (`stageVersion` 1).

A scan of every `docs/api` schema finds no other `oneOf` choice without `type` that
constrains anything. The `{"const": 0}` choice of `text.create` depth was already
checked. `anyOf` (reference image and text updates) is not evaluated by this
validator before or after the fix. The fix therefore changes only
`annotation.create`. The full dev CTest run passes with it.

## Installed smoke

Run `r090e-package`: a copy of the wide local runner pointed at this worktree
(image `sketchyup-build:20261002`, `a4f0465c`; 6 CPUs, 10 GiB, `-j4`), 16 minutes.
The source archive from `scripts/package-source.sh` is `25a97e98…0d40`. The script
runs `packaging/arch/verify-package.sh` with `SKETCHYUP_PRIOR_RELEASE_PACKAGE` set
to the 1.0.0-1 release package (pinned SHA-256 checked), then the R090.e steps.

- **Package lifecycle.** The 0.1.0-3 build runs `check()`: CTest 194/194, with the
  11 optional consumer tests skipped, and `SKETCHYUP_PRIOR_CLI` set to the released
  1.0.0 CLI for `drawing_sheet_io`. Install, desktop launch, MIME, upgrade to
  1.0.0-1, reopen and removal pass. The prior-release block prints "Prior release
  reads schemas up to 26: refused 1 newer fixtures unchanged and accepted this
  package's sheet-free save." The installed CLI migrates 30 historical fixtures.
  Package `sketchyup-1.0.0-1-x86_64.pkg.tar.zst` is `82558a04…1c18`.
- **Sheet smoke.** The package is installed again. As the unprivileged user,
  `/usr/bin/sketchyup-cli` runs five scripts, each opening the previous file and
  saving a new one: a box and an orthographic "Plan" scene; an A3 sheet
  (420 × 297 mm); a view "Plan 1:50" of that scene at 1:50 in a 200 × 150 mm frame;
  `drawing_view.accept`; and a 0.5 m `geometry.translate` of the box. Sheet
  commands and model commands go in separate scripts, since they cannot share a
  batch. Every file passes `--validate-native`. The model file is schema 26 and
  the four files with a sheet are schema 27. Each inspection reopens a saved file:

  | File | `drawing_views.status` | Reasons | Declared scale (`drawing_view.describe`) |
  | --- | --- | --- | --- |
  | after create | `stale` | `notAccepted` | — |
  | after accept | `current` | — | 1:50, 20 paper mm per metre |
  | after the move | `stale` | `changedSinceAccepted` | 1:50, 20 paper mm per metre; accepted stamp unchanged |

  `drawing_sheets.query` lists one sheet, "A3 plan", 420 × 297 mm, with one view.
  The view's fingerprints (`3342858e…`, then `b114cba3…` after the move) are the
  same in the installed release build and the host debug build.
- **Downgrade window, installed.** The unpacked 1.0.0 CLI refuses
  `drawing-sheets-v27.sketchyup` and the smoke's `view` and `moved` files with
  "Unsupported container field", each unchanged afterwards. It accepts
  `examples/saved-scenes.json` saved by the installed CLI and the smoke's model
  file, both schema 26.
- The package is removed at the end.

## Native matrix

R090 adds no desktop UI, and no native GUI suite changed. As a smoke check that save
and recovery still work in a build with schema 27, `file_operation_input_tests` and
`recovery_input_tests` ran on X11 and Wayland at 1× with the `native.yml` commands
(`timeout 90s`/`50s` under `xvfb-run`; `scripts/test-wayland.sh`). They used the
host `build/dev` in image `sketchyup-native-fonts-check:20261008` (`873ba078`), 6
CPUs, 10 GiB, run `r090e-native`.

| Suite | X11 1× | Wayland 1× |
| --- | --- | --- |
| `file_operation_input_tests` | pass, 4.9 s | pass, 1.7 s |
| `recovery_input_tests` | pass, 4.3 s | pass, 1.9 s |

These fixtures save sheet-free documents, which are written as schema 26. Saving
and recovering documents with sheets is covered by `drawing_sheet_io` (save, `.bak`
and recovery) and the installed smoke. The 2× and sanitized native runs were not
repeated.

## Not covered

- No desktop UI exists for sheets yet (R091). No PDF or print output exists (R092).
- The installed smoke runs in a container with the pinned image only, not on a
  clean machine or a reference snapshot.
- The 2× and sanitized native matrices were not run for R090, since no native suite
  changed.
