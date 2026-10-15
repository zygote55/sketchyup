# R091.h — Sheet layout: enable and accept

2026-10-14. Product, suites and documentation on branch `build/r091h-sheets-enable`: the main
matrix at `95258e88`, and round 2 (the focus-ring decisions) at the branch head. `95258e88` which contains R091.g (PR #323 head `a2a60b62`) and `main` after #322 (R091.f).
[Summary](R091h-sheet-layout.json).

R091 adds the Sheets workspace ([ADR 0164](../decisions/0164-sheet-layout-interface.md)):
R091.a page-item records (#316), R091.b persistence (#317), R091.c item evaluation and
commands (#319), R091.d the compositor (#320), R091.e the workspace shell (#321), R091.f model
views (#322), R091.g page items (#323) and this layer. This record checks the roadmap Verify
line: "A small drawing set can be laid out and revised; referenced dimensions update and page
editing never modifies source model geometry".

## Changes in R091.h

- **The flag is gone.** `SKETCHYUP_SHEETS_WORKSPACE` and `Window::sheetsWorkspaceEnabled` are
  removed. The Model | Sheets switch, View › Model/Sheets workspace and `Ctrl+Alt+S` are always
  there. The suites and the AT-SPI bridge test no longer set it; no CI line set it.
  `sheet_workspace_input_tests` now checks that the workspace exists with no flag, and with an
  explicit `SKETCHYUP_SHEETS_WORKSPACE=0`.
- **Tab order.** With the rail stack always built, Tab from command search went to the viewport
  and skipped the tool rail (`keyboard_reachability_tests`: "Tab from command search into the
  rail"). The stack, with both rails, is moved back to right after the search.
- **Focus after prompts (Model workspace).** See [Model workspace focus](#model-workspace-focus).
- **Natural view frame.** The Model view tool measured the extent of only the first vertex:
  `QRectF::united` ignores a zero-size rectangle, so each later point was dropped. Views placed
  with Enter, Place or a typed size were centred on that vertex, not on the drawing. The
  acceptance script found it (a typed-size plan put the chimney outside its frame). It now
  measures every visible vertex.
- **`sheet_acceptance_input_tests`** (new; CI on X11 and Wayland at 1×, 2× and 1.5): the
  [§6 script](#the-adr-0164-6-script).
- **`visible_focus_input_tests --workspace=sheets`** (new CI lines on X11 and Wayland at 1× and
  2×) sweeps the Sheets workspace: the canvas, page tabs, Pages tree, breadcrumb and Entity info
  for the page, a model view and a text; the sheet rail (checked and unchecked buttons); the New
  model view panel; the edit-view banner; the Choose scene dialog; and, every time, the
  accent-filled controls (checked Sheets segment, Place, Done). `sketchy::SheetCanvas` is a
  required class there. The default run sweeps the Model window as before. Splitting keeps each
  run well under the Wayland harness's 60 s (round 2: Model 28.2 s, Sheets 21.7 s at Wayland 2×).
- **Focus rings (owner decisions after round 1).** An accent-filled control shows focus as a
  2 px inset ring in `$surface` of the fill's theme, through the shared stylesheet
  (`accentFill` property, the checked switch segment, and Done in the inverse edit-view banner).
  Those controls carry a 2 px border in the fill colour when unfocused, so focus never shifts the
  layout. The selected page tab's focus is the standard 2 px `$ink` ring; selection keeps its
  accent fill and border. The suite measures an accent-filled control by the ring against the
  fill inside it and the change of its boundary colour, and a tab by its painted box (tab
  margins are spacing). It is strict: no allowance remains.
- **Docs.** [UX_DESIGN](../UX_DESIGN.md) §13 Sheets, the scoped shortcut table in §4.2 and an
  M10 row in §11; [user guide](../USER_GUIDE.md#documentation-sheets) "Documentation sheets";
  ADR 0164 status, implementation list and R091.h conventions; [D-031](R089a-defect-triage.md).

### Existing test expectations that changed

| Suite | Change | Why |
| --- | --- | --- |
| `sheet_workspace_input_tests` | "Without the flag there is no toggle" became "the toggle, canvas and `Ctrl+Alt+S` work with no flag (and with `=0`)" | The flag is gone |
| `sheet_layout_input_tests`, `sheet_annotation_input_tests`, `shortcut_input_tests`, `keyboard_reachability_tests`, `native_accessibility_audit`, `accessibility_bridge_test.py` | No longer set the flag; the audit no longer sets it for the sheets, inventory and all groups | The flag is gone |
| `keyboard_reachability_tests` | New phase: Field of view… (accept, cancel) and a file-operation surface return activation and focus to the viewport | Regression for the Model-workspace focus fix |
| `visible_focus_input_tests` | `--workspace=sheets` sweeps the Sheets workspace with its own required classes (including `sketchy::SheetCanvas`); accent-filled controls and tabs are measured as described above | New Tab stops: the Model \| Sheets buttons in the header (in every Model context), and the Sheets controls |

No Tab-stop count is asserted anywhere: `keyboard_reachability_tests` checks one rail stop, that
each stop is visited once and that the cycle returns to the viewport. The Model Tab cycle gains
the two workspace buttons in the header, each visited once.
No shortcut, menu or audit expectation changed: the R091.g "Reattach" label (no "…") keeps the
dialog inventory complete, and every group passes with Sheets built.

## The ADR 0164 §6 script

`tests/sheet_acceptance_input_tests.cpp`. The model has a 10 × 8 m slab, two walls, a 4 m
chimney and a 3 × 3 m terrace, and four scenes (Ground plan, top; South and North elevation;
Garden view, perspective). It has no sheets. Pointer input is real mouse events on the canvas;
keys go to the focused widget.

1. **Layout.** `Ctrl+Alt+S` opens Sheets. "+ Page" adds an A3 page. Insert › Title block, then
   Enter, typed project and title with Tab, Enter ("Edit title block"); the sheet number A-101 in
   Entity info. `W`, scene, a typed size: plan and south elevation on page 1, `1:100` typed in the
   Scale step; `Ctrl+Shift+N` adds page 2 with a copy of the title block; north elevation and the
   perspective garden view ("Not to scale"); "+ Page" adds page 3 with its title block. `D`: a
   dimension from the slab corner to the chimney corner (vertex anchors, "8732"), and one from a
   point on the terrace's south edge (an edge anchor) to the slab corner (about 2.5 m). `K`: a
   leader label bound to the plan. `X`: text. Accept all. Every view is current.
2. **Revise the model** (model edits): move the chimney 1 m in x; delete the North elevation
   scene; erase the terrace edge the second dimension is anchored to.
3. **States and repairs.** Plan, south and garden are Changed (`stale`); the canvas describes the
   plan as "Changed since accepted". North is Broken (`sceneMissing`). The vertex dimension reads
   "9677" (√(9.2² + 3²) m). The edge dimension is Broken (`referenceMissing`), and the Pages page
   row describes changed and broken entries. Entity info › Reattach proposes a point; Enter
   commits "Reattach dimension" and it is current. Choose scene… on the broken card relinks north
   to South elevation; focus returns to the canvas. Accept all is one entry, "Accept 4 views", and
   every view and item is current.
4. **Model unchanged by page operations.** After every page operation the bodies, scenes,
   definitions, tags, materials and sections maps are pointer-identical, and the encoded document
   minus its drawing tables, drawing allocators, schema version and revision counter is
   byte-identical. Laying out ends with the starting bytes; repairing ends with the revised
   model's bytes.
5. **Round trip.** Save, reopen (pages, views, items and the 9677 value reopen; views reopen
   current), delete the file, save again: the second file is byte-identical to the first, and the
   container re-encodes exactly.

| Platform | Scale | Result | Seconds | Accept all | Dimension after the move | Second save identical |
| --- | --- | --- | ---: | --- | --- | --- |
| X11 | 1× | pass | 14.9 | Accept 4 views | 9677 | yes |
| X11 | 2× | pass | 18.7 | Accept 4 views | 9677 | yes |
| X11 | 1.5 | pass | 17.7 | Accept 4 views | 9677 | yes |
| Wayland | 1× | pass | 14.4 | Accept 4 views | 9677 | yes |
| Wayland | 2× | pass | 13.7 | Accept 4 views | 9677 | yes |
| Wayland | 1.5 | pass | 17.8 | Accept 4 views | 9677 | yes |

Each run prints a JSON summary: 3 pages, 4 views, 7 items, 12 page operations checked. The
model-byte and file digests differ between runs because each run creates a new document
identity; within a run the digests compare equal as stated above.

The ADR's script and the brief differ slightly: the ADR asks for "three pages and four views";
this script has three pages (the third holds only its title block) and four views.

## Native matrix

Final run `r091h-matrix-3`: host `build/dev` at `95258e88`, image
`sketchyup-native-fonts-check:20261008` (`873ba078`), the wide runner copy (6 CPUs, 10 GiB), two
lanes in one container, the host's at-spi2-core 2.60.7 binaries and `libXtst` mounted for the
bridge. It ran every `native.yml` line of "Native interaction and OpenGL tests", "Keyboard
layouts", "Isolated Wayland interaction" and "Fractional client scaling" (346 cells, including
the new acceptance lines), the extra cells below, and the full all-dialog audit. The X11 1×
`render_input_tests` line ran without `SKETCHYUP_BLENDER_TEST` (no Blender in the image), as at
the RC.

**Result: 381 of 383 cells pass.** The two failures are the extra `visible_focus_input_tests`
cells at fractional 1.5 (not CI lines), discussed below.

### Sheet suites and touched suites

Seconds; every cell passed except the two marked.

| Suite | X11 1× | X11 2× | X11 1.5 | Wayland 1× | Wayland 2× | Wayland 1.5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `sheet_acceptance_input_tests` | 14.9 | 18.7 | 17.7 | 14.4 | 13.7 | 17.8 |
| `sheet_workspace_input_tests` | 9.0 | 11.4 | 10.1 | 7.9 | 19.8 | 8.4 |
| `sheet_layout_input_tests` | 20.8 | 26.0 | 25.4 | 21.0 | 29.9 | 22.7 |
| `sheet_annotation_input_tests` | 11.7 | 20.7 | 17.4 | 14.7 | 6.9 | 16.8 |
| `keyboard_reachability_tests` | 8.0 | 15.2 | 10.5 | 11.5 | 17.6 | 13.4 |
| `visible_focus_input_tests` | 14.8 | 33.4 | **fail** 22.4 | 23.9 | 53.2 | **fail** 30.8 |
| `shortcut_input_tests` | 5.6 | 7.6 | 7.2 | 4.7 | 10.1 | 4.2 |
| `interaction_tests` | 4.5 | 4.7 | — | 1.7 | 2.0 | — |
| `accessibility_bridge_test.py` | 7.7 | 9.2 | — | 5.9 | 9.1 | — |

Extra cells beyond `native.yml`: 1.5 for `sheet_workspace_input_tests`,
`keyboard_reachability_tests` and `visible_focus_input_tests`; Wayland 1× and 2× and X11 2× for
`interaction_tests`. `shortcut_input_tests` at 1.5 and 1.75 are CI lines.

- **All-dialog audit:** inventory clean (17 groups, no uncovered "…" action); every group passes
  on X11 2× and Wayland 1×, and the sheets group also on X11 1× and Wayland 2× (37/37).
- **AT-SPI bridge:** all four cells pass; the Sheets phase reads the Pages rows
  ("Title block · House extension", "Text · Notes"), the canvas children and description.
- **Visible focus in this run** (before the round-2 decisions): superseded by
  [Round 2](#round-2-focus-rings), which re-ran every visible-focus cell.
- **Visible focus at 1.5 (extra cells):** 137 and 138 failures. They are not Sheets-specific:
  most are model-window controls (36 tool buttons, 20 tab bars, 15 breadcrumb, the material,
  solar, drawing-plane and assistant-preference dialogs). At a fractional factor the 1 px ring
  is antialiased and its painted colour falls under the threshold. The suite is not a CI line at
  1.5, and R084.q measured 1× and 2× only. Recorded as [D-032](R089a-defect-triage.md).
- **Wayland 2× `visible_focus_input_tests` took 53.2 s** here with both workspaces in one run,
  close to the 60 s harness limit (D-028). Round 2 splits it.

### Round 2: focus rings

After the owner's decisions (accent-fill ring, page-tab ring, strict suite, split run), at the
final head, run `r091h-round2` (single lane; the same image and runner) and `r091h-round2b` (the
Sheets focus cells again after adding the explicit accent-filled probes):

| `visible_focus_input_tests` | X11 1× | X11 2× | X11 1.5 | Wayland 1× | Wayland 2× | Wayland 1.5 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Model (default) | 12.8 | 22.9 | fail 17.3 | 14.4 | 28.2 | fail 17.7 |
| `--workspace=sheets` | 8.7 | 14.8 | fail 11.5 | 10.6 | 21.7 | fail 11.7 |

- **1× and 2×:** 0 failures. Model run 422 controls measured, Sheets run 242; every required
  class covered; minimum ring contrast 4.53:1 outside and 3.93:1 inside.
- **Accent-filled rings** (ring against the fill, and the boundary's change on focus), 1× and 2×,
  X11 and Wayland:

  | Control | Light | Dark |
  | --- | ---: | ---: |
  | Model or Sheets segment, checked | 4.65:1 | 8.05:1 |
  | Place · Enter | 4.65:1 | 8.05:1 |
  | Done · Esc (inverse banner: the other theme's accent and surface) | 8.05:1 | 4.65:1 |

  Computed from the tokens: #f7f7f2 on #547858 is 4.65:1, #202923 on #9ec99d is 8.05:1.
- **Selected page tab:** the `$ink` ring measures 11.27:1 against the bar and 9.52:1 against the
  selected fill (light), 13.18:1 and 6.87:1 (dark).
- **1.5:** 127–128 (Model) and 74 (Sheets) failing measurements, the D-032 class. The 2 px
  accent-fill ring does not fix it: at 1.5 those rings measure 2.36:1 (light) and 2.57:1 (dark).
- **Also re-run, all pass:** `sheet_workspace_input_tests`, `sheet_layout_input_tests`,
  `sheet_annotation_input_tests`, `sheet_acceptance_input_tests` and `keyboard_reachability_tests`
  on X11 and Wayland at 1×, 2× and 1.5 (30 cells); the full audit, inventory clean and 37/37;
  `ctest --preset dev` 198/198 (11 optional skipped).

### Earlier runs

`r091h-matrix-1` (at `f0577e4c`, before the last three commits) found and drove these fixes:

- every audit group failed the inventory ("Reattach…" had no dialog), fixed by R091.g's
  "Reattach" label;
- the bridge needed `libXtst` mounted (environment);
- `extension`, `texture`, `m6_workflow` and `site_recipe` suites read fixtures by the host
  source path, which the container did not mount (environment; passed once mounted);
- `audit-animation` X11 2× failed twice with "Escape closes the parent dialog" after the first
  version of the focus fix: a prompt opened from the Render sheet handed activation to the main
  window. Fixed (`e8fb945e`): activation returns only when no other application window is open;
- Wayland 2× exposure or repaint timeouts in `keyboard_reachability`, `sheet_layout`,
  `hosted_array`, `roof_recipe`, `furniture_recipe`, `site_recipe` and `group` suites under two
  lanes. Each passed on a single-lane rerun (D-021 class).

### Round 3: focus after Choose scene on Wayland

CI on PR #324 (`89706008`) failed `sheet_layout_input_tests` on Wayland: "Focus returns to the
canvas after Choose scene; got none".

- **Reproduced, and not caused by R091.h.** The CI line in the wide runner with four lanes:
  1 of 60 runs at `89706008`; 2 of 60 on the R091.g build (no `ModalReturn`, flag set). With
  `WAYLAND_DEBUG` tracing (slower), 4, 5 and 3 of 60. Single-lane and one-CPU runs (32) did not
  fail. `ModalReturn` cannot be involved on Weston: Weston 15 has no xdg-activation, and Qt's
  `QWaylandWindow::requestActivateWindow` then does nothing.
- **Cause (Qt Wayland client race).** The trace of every failure is the same: Weston focuses
  the Choose scene prompt (`wl_keyboard.enter`) and the client requests a `wl_display.sync`;
  the test accepts the prompt about 60 ms later, before that sync's `done` arrives; the prompt's
  surface is destroyed; `done` then arrives and Qt (`QWaylandDisplay::handleWaylandSync`) queues
  "no focus window", since its active list is empty; Weston sends `leave(nil)` and
  `enter(main window)`, and the next sync compares the main window with
  `QGuiApplication::focusWindow()`, which is still the main window because the queued change
  has not been delivered, so nothing is queued. The queued "no focus window" is then delivered
  and stays: focus window none, active window none, the window's focus child the canvas.
  The compositor did return focus; Qt dropped it. A person cannot close a prompt within one
  round trip of it appearing; automation can.
- **Fix.** `SettledInputDialog` closes only when the prompt is the focus window (at most
  300 ms), so the activation sync is processed first. Choose scene, Save as new scene and Field
  of view use it. On X11 it closes at once.
- **After:** 0 of 180 runs failed the focus check (60 traced and 60 untraced at 1×, 60 more
  traced). The untraced batch had 6 "Window exposed" failures at Wayland 2× under four-lane load
  (D-021; the R091.g build had 10 of 60 in the same batch).
- **Re-run, single lane, at the head after merging R092.a:** `sheet_layout_input_tests`,
  `sheet_acceptance_input_tests`, `keyboard_reachability_tests`, `visible_focus_input_tests` and
  `--workspace=sheets` on X11 and Wayland at 1× and 2×: 20/20 pass. The inventory is clean and
  the sheets audit group passes on X11 1× and 2× and Wayland 1× and 2×. `ctest --preset dev`
  200/200, 12 optional tests skipped.

## CTest and sanitizers

| Check | Where | Result |
| --- | --- | --- |
| `ctest --preset dev` | host, Qt 6.11.2, GCC 16.2.1, `-j8`, at `95258e88` | 198/198 pass; 11 optional consumer tests skipped |
| `ctest --preset headless` | host | 64/64 pass |
| `ctest --preset sanitize` | host | 64/64 pass |
| CI step "Sanitized bounded inspection and CLI" (`-DSKETCHYUP_BUILD_CLI=ON`, the io, command, sheet and item suites) | host, ASan/UBSan | 103/103 pass |

The headless and sanitized builds contain no desktop code, which is all that changed after them.

## Installed smoke

Run `r091h-package`: the wide runner copy (image `sketchyup-build:20261002`, `a4f0465c`), 6 CPUs,
10 GiB. The package directory holds `scripts/package-source.sh` output from the branch at
`e8fb945e` (source archive `da18158a…914c`), `packaging/arch/verify-package.sh`, the 1.0.0-1
release package (pinned SHA-256 checked) and the drawing set saved by the acceptance suite
(`--keep`, X11 1×).

- **Package lifecycle.** `check()`: CTest 198/198 with 11 optional consumers skipped. Install,
  desktop launch, MIME, upgrade to 1.0.0-1, reopen and removal pass. "Prior release reads schemas
  up to 26: refused 1 newer fixtures unchanged and accepted this package's sheet-free save."
  The installed CLI migrates 30 historical fixtures. Package `sketchyup-1.0.0-1-x86_64.pkg.tar.zst`
  is `db200f05…9deb7`.
- **Sheet document in the installed app.** As the unprivileged user, `/usr/bin/sketchyup-cli`
  opens the saved drawing set:
  - `--validate-native`: valid, schema 27;
  - `drawing_sheets.query`: 3 A3 pages with 2, 2 and 0 views;
  - `drawing_views.status`: 4 views, all current, one not to scale (Garden view);
  - `drawing_items.query`: 7 items (2 dimensions, a label, a text, 3 title blocks), all current;
  - `drawing_item.describe` on the first dimension: "9677", 9.6768 m, 96.77 mm on paper;
  - a sheet-only script (rename page 1) saved to a new file: valid, schema 27, renamed;
  - a script deleting every page: valid, schema 26.

## Downgrade window

A document without pages stays schema 26; adding a page writes 27, which 1.0.x refuses.

- **Local**, with the CLI built from tag `v1.0.0` (`.local/wt-v100/build/headless/sketchyup-cli`)
  and the R091.h dev CLI: 1.0.0 reads up to schema 26. It refuses `drawing-sheets-v27.sketchyup`,
  the acceptance drawing set and its renamed copy with
  `{"code":"INVALID_OPERATION","error":"Unsupported container field","status":"failed"}`, each
  byte-identical afterwards. It accepts `examples/saved-scenes.json` saved by the dev CLI and the
  drawing set with every page deleted, both `{"valid":true,"documentVersion":26}`.
- **Installed**, the same steps with the unpacked 1.0.0 CLI and the installed CLI: identical
  results.

## Model workspace focus

R091.f found that under X11 without a window manager (Xvfb, as in CI) the window lost activation
after its `QInputDialog` prompts and after the file-operation surface. A probe (Model workspace;
window active and viewport focused before each step) checked Model flows on X11 1× and Wayland 1×:

| Flow | X11 before | X11 after | Wayland |
| --- | --- | --- | --- |
| View › Field of view… accepted | no active window, no focus widget | active, viewport focused | active, viewport focused |
| View › Field of view… cancelled | no active window, no focus widget | active, viewport focused | active, viewport focused |
| View › Interface text size… | active (it already reactivated) | active | active |
| Open a 48 MiB file (surface shown) | no active window, no focus widget | active, viewport focused | active, viewport focused |
| Save with the surface shown | no active window, no focus widget | active, viewport focused | active, viewport focused |

"After" is the probe with the first version of the fix; the final version (which also yields
to another open application window) is covered by the regression below. The window keeps its
focus child (the viewport); only activation is lost. The fix is general,
not per prompt: `ModalReturn` (`src/app/window.cpp`) watches modal dialogs and the
file-operation surface owned by the window, and when one hides it reactivates the window, unless
another modal dialog, a popup or another application window is still open. The window's focus
child then has focus again. The regression is `keyboard_reachability_tests`' new phase; with
`ModalReturn` disabled it fails on X11 ("Cancelling Field of view… returns activation and focus
to the viewport (focus none)"), and it passes on X11 and Wayland at 1×, 2× and 1.5 with it. A
user with a window manager or a Wayland compositor did not see the loss; the fix matters for
X11 sessions without one and for the native suites.

## Known limitations

- **Edge splits and make component break paper dimensions** that model annotations survive
  (ADR 0164 §3 lineage table). Paper anchors resolve by ID; model edits never write sheet records.
  **Reattach** is the repair: it proposes the nearest vertex or edge point in the view.
- **Raster views use the viewport's visibility, not the scene's.** Rendered and perspective view
  content is captured with the editor's current tag and hidden-object visibility rather than the
  scene's saved visibility.
- **PDF text is outlines** (R092): export and printing of pages are not part of R091, and the
  measured-PDF path writes text as glyph outlines.
- **Focus rings at fractional 1.5×** stay under the threshold for 1 px and 2 px rings alike
  (D-032); 1× and 2× pass.
- The status bar does not name a model edit that Undo steps back into from Sheets (ADR 0164 §5,
  "Undid model edit: …"); the History section shows the entry.

## Defect note (D-031)

On PR #320 (R091.d, `5bf306b8`), run
[38605636463](https://github.com/zygote55/sketchyup/actions/runs/38605636463) attempt 1, job
115995661310: the Wayland 1× line `SKETCHYUP_TEST_SCALE=1 scripts/test-wayland.sh
build/dev/keyboard_reachability_tests` printed "Side tabs: … passed", then Weston's
`BUG: finalizing a layer with views still on it.` (7 s after Weston started), and the step ended
with exit 1 and no check message. Attempt 2 passed. R091.d had no desktop change. No existing
entry covers a silent exit with that Weston line (D-021 is an exposure timeout with a named check;
D-007 is focus lost after exposure), so it is new: [D-031](R089a-defect-triage.md), test
infrastructure, Minor, Unverified. `keyboard_reachability_tests` passed every Wayland cell here.

## Not covered

- No interactive screen-reader session; the bridge test reads the tree and descriptions only.
- The installed smoke inspects the drawing set through the installed CLI; the installed desktop
  was launched by the package lifecycle check, not driven through Sheets.
- The matrix ran in containers with the pinned image, not on a reference machine; CI on the
  branch is the next check.
