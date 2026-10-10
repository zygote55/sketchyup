# R084.r — Per-document display precision

2026-10-10. This closes two D01 clauses. Precision formatting now survives save and
load, and decimal imperial entry has explicit tests. Each document stores a
display precision: **Full**, or a fixed number of decimal places (`m` 0–6, `mm` 0–3,
`ft-in` 0–3 for the inches part). Full is stored as `-1` and is the default. It
reproduces the earlier output exactly: trimmed 8/5/6-decimal lengths and
8-significant-digit areas/volumes. Existing documents therefore display unchanged.
Fixed precision keeps trailing zeros and carries rounding (`0.9999 m` at 2 shows
`1.00 m`; `0.30479 m` at 3 shows `1' 0.000"`). The design is recorded in
[ADR 0018](../decisions/0018-document-units.md#display-precision-r084r).

## Behaviour covered

- **Model/edit.** One labeled, undoable Edit (`Change display precision`). It uses
  the ordinary revision, dirty, save-stamp, amendment and stale-edit checks. The
  after-state precision must be valid for the after-state unit. A unit change
  resets to Full unless it names a precision. Shared-definition drafts and
  geometry amendments cannot change it.
- **API.** `document.units` takes an optional `precision` (`"full"` or an integer
  in range). Out-of-range values reject before publication. `document.describe`
  and the inspection document query report `displayPrecision` in the same form.
  Previews and staging see it as an ordinary revision change. Staging reports a
  `displayPrecision` change.
- **Persistence.** Native schema 25 adds a required integer `displayPrecision`.
  The container requires `display-precision-v1` and uses `json-v25`. Schemas 1–24
  and every historical fixture migrate to Full. Save snapshots and recovery
  checkpoints carry it. The new golden fixture is
  `tests/fixtures/display-precision-v25.sketchyup`.
- **Desktop.** File → Document units has a **Precision** combo (accessible name
  "Display precision") directly after Units in tab order. Its entries are samples
  from the real formatter, for example "Full (4' 0.605031")", then "4' 1"", "4' 0.6"", and so on. Changing
  the unit repopulates the list and selects Full. "Use for new documents" also
  stores the precision. First-run setup still asks only for units.
- **Readouts that follow precision.** Measurements live previews, tape, Entity
  info position/dimensions/length/area/volume, selection face area, viewport grid
  hint, hosted/offset messages, section and text summaries, reference-image
  summary, dimension annotations (viewport, panel, measured drawings), and
  assistant measured previews. Editable fields keep Full formatting, so editing
  never loses precision.
- **Measurements preview consistency fix.** Live drawing/arc/push-pull previews
  previously showed raw metres with `'g', 8` even in `mm` or `ft-in` documents.
  For example, a `mm` model showed `1.5` for 1.5 m, which reads back as 1.5 mm
  when re-entered. They now use the shared formatter with an explicit suffix, so
  in metres at Full they read `1.5 m`, which parses to the same value. No existing
  native test asserted the old strings.
- **Decimal imperial entry.** `2.5in`, `1.25ft`, `1.5'`, `3' 4.5"` and a bare `2.5`
  in feet-and-inches units convert to 0.0635, 0.381, 0.4572, 1.0287 and 0.762 m
  within 1e-12, including German-locale `2,5in`/`3' 4,5"`.

## Commands and results

Host: shared Arch Linux workstation under concurrent load. Builds used `--parallel 3`.

| Step | Command | Result |
| --- | --- | --- |
| Dev build | `cmake --preset dev && cmake --build --preset dev --parallel 3` | pass, no errors |
| New suites | `build/dev/display_precision_tests`, `build/dev/display_precision_io_tests` | pass |
| Contracts | CLI `--session-capabilities`/`--mcp-capabilities`, `transaction_dispatch_tests --print-capabilities`, `scripts/generate-tool-reference.py --cli build/dev/sketchyup-cli --root .` then `--check` | regenerated; only the `precision` property differs |
| Headless | `ctest --preset dev -j3` | 184/184 pass (includes `automation_session`, `mcp`, `transaction_dispatch`, `tool_reference_drift`, `native_format`) |
| Sanitizer core | `cmake --preset sanitize && cmake --build --preset sanitize --parallel 3 && ctest --preset sanitize -j3` | 59/59 pass |
| Sanitizer IO/API | `cmake --preset sanitize -DSKETCHYUP_BUILD_CLI=ON`, build the listed targets, `ctest --test-dir build/sanitize -R …` | 24/24 pass (`display_precision`, `display_precision_io`, `document_units(_io)`, `native_format`, `persistence`, every schema-touched `*_io` suite, `staging`, `inspection`, `automation_session`, `mcp`, `transaction_dispatch`, `tool_reference_drift`) |

The frozen provider corpus (`tests/provider-release-corpus.json`) pins the bytes of
its generated input models. Schema 25 changes those bytes. Only the eight
`inputSha256` values were updated; every prompt and its hash still matches.

## Native matrix

Host X11/Weston are not installed, so the native cases ran in the bounded local
runner (`scripts/run-container-check.sh`, image `sketchyup-native-fonts-check:20261008`:
one CPU, 4 GiB, Qt 6.11.2 matching the host build). They used the CI invocations from
`.github/workflows/native.yml` against `build/dev` binaries:

```
xvfb-run -a env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 timeout 60s build/dev/units_input_tests
xvfb-run -a env QT_QPA_PLATFORM=xcb QT_SCALE_FACTOR=2 LIBGL_ALWAYS_SOFTWARE=1 timeout 60s build/dev/units_input_tests
scripts/test-wayland.sh build/dev/units_input_tests
SKETCHYUP_TEST_SCALE=2 scripts/test-wayland.sh build/dev/units_input_tests
```

`units_input_tests` opens File → Document units and checks the samples and
repopulation. Using only the keyboard, it Tabs from Units to Precision, presses
Down twice and then Return. It then checks that Entity info dimensions and area
change to one decimal, and that Undo restores the Full readouts.

| Case | Result |
| --- | --- |
| X11 1× | pass (two runs) |
| X11 2× | pass (two runs) |
| Wayland 1× | pass (two runs) |
| Wayland 2× | pass in 2 of 4 runs; the 2 failures were `Units window exposed` |

The two Wayland 2× failures happened before any test step, in
`qWaitForWindowExposed`, with host load averaging about 28. The program never
reached the units or precision steps. This is recorded as a startup flake under
load, not a pass.

These further native tests use code paths whose formatting changed. They passed on
X11 1× in the same runner: `annotation_viewport`, `annotation_input`, `offset_input`,
`numeric_input`, `interaction`, `text_input`, `section_input`, `style_input`,
`hosted_input`, `reference_image_input`, `texture_input`, `assistant_panel`,
`assistant_preview`, `measured_input` and `keyboard_modeling_input`.
`texture_input` needs its compiled `SOURCE_DIR` path, so its passing run
symlinked that path to the read-only checkout.

## Coordination and limits

The schema change uses the next version after `main`, 25. Branch R082.cc is also
bumping to 25. The bump is kept in separate hunks so the second branch to land
can renumber it: the `nativeDocumentVersion` constant, the container feature
chain, the document field/migration line, `native-format-v1.json`, and the
schema literals in tests. Component-library extraction documents still start at
Full. This record does not claim D01 or R084 acceptance.
