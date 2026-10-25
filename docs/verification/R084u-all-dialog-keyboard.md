# R084.u — All-dialog keyboard audit

2026-10-13. Branch `build/r084u-all-dialog-keyboard`; audited source `308cc5fe`, the code
commit on top of `main` `0874ab4f`.

R084.p checked Tab traversal for four dialogs (Drawing plane, Keyboard shortcuts,
Commands, Assistant preferences). N03 and N05 stayed Open because nothing covered the
other dialogs. This layer inventories every dialog the desktop app can show, audits each
one with key events only, and fixes what the audit found.

## Inventory

Sources: every `QDialog`, `QMessageBox`, `QFileDialog`, `QInputDialog`, `QColorDialog`,
`QProgressDialog` and `ReportSheet` construction in `src/app` (nothing outside `src/app`
creates a window), the actions that open them, and the panel buttons. The app has no
Help menu and no About or Help dialog.

**102 dialogs are audited**, each as one case of
`native_accessibility_audit --dialogs-keyboard=GROUP` in `tests/native_dialog_cases.hpp`.
A dialog whose controls change with its mode is audited once per mode (four Assistant
preference modes, Extensions with and without an installed extension, the 3D text and
annotation create/edit forms). The table at the end lists them with their keyboard path
and the result per platform.

The same widgets also appear on paths that the audit reaches but does not traverse again:

| Also shown | Reached by keyboard in | Same widget as |
|---|---|---|
| Save PNG, New measured drawing, Save new library bundle (file choosers) | Enter on `raster-export`, `measured-export`, `save-template`, `save-component` (opened, then Escape) | the audited Qt file dialog |
| New STL file, New OBJ package folder (file choosers) | Enter on `stl-export-options`, `obj-export-options` | the audited Qt file dialog (`dxf-export-file` is traversed) |
| DXF, STL and OBJ import reports | Enter on the three import-option cases (report closed with Escape) | the audited export reports (`ReportSheet` with a named details pane) |
| Recover work at start-up (`showRecovery(true)`) | — (opens itself only when a recovery copy exists) | `recover-work` |
| Unsaved changes from Open, Quit, templates and imports | — | `unsaved-changes` (one `canReplace` box) |
| Assign tag from the Tags page | — | `outliner-tag` |

**Not exercised: one.** The file-operation progress popup (`fileOperationDialog`,
"Saving model…"/"Opening model…") is a non-activating tool-tip surface with no controls
(R082.jj); it never takes keyboard focus by design, so the keyboard criteria do not apply.

The inventory mode (`--dialogs-keyboard=inventory`) also fails if a public command whose
label ends in "…" is not the keyboard path of some case, so a new dialog command cannot
be added without a case.

## What each case checks

Each case starts with focus in the viewport, then, with key events only:

1. **Opens by keyboard.** The Ctrl+K palette (typing the command and Enter), a shortcut
   (Ctrl+O, Ctrl+N, Ctrl+J, Ctrl+K), or Tab and Space to a Model-panel or parent-dialog
   button after a palette command. Every public command is in the palette. Menu
   mnemonics are not driven: R084.s showed Alt+letter menus work on X11, and Qt's
   Wayland popups need a compositor input serial that synthetic keys lack.
2. **Names.** Every visible, enabled, Tab-focusable control, including Qt's internal
   spin-box and combo-box editors, has an accessible name. A name that is only a value
   (`#f0f1ec`, `100%`) fails. Closed combo boxes report their value as the name on
   Linux; for them the form label (the Label relation) or an explicit name counts.
3. **Tab and Shift+Tab.** Each reaches every such control without leaving the dialog,
   Shift+Tab visits the Tab cycle in reverse, and the cycle follows reading order: it
   may move up and to the left only once (the wrap). Positions are taken when each
   control has focus, and compared within a scroll area's content where both share one.
4. **Escape.** Escape (pressed from the dialog's first control) closes it; the document
   bytes and the preference file are unchanged; and the owning window's keyboard focus
   is the opener, its Model-panel page, or the viewport.
5. **Default button and Enter.** The default button is the expected one. In a second
   opening, Enter in the first single-line field (or on the default button) applies and
   closes the dialog with Accepted; opens the expected nested chooser or confirmation
   (cancelled again with Escape); or continues to the next dialog of the flow. Where the
   apply button is disabled until a choice exists (empty library), Enter must not close
   the dialog. For file choosers, the Blender/OpenAI permission and manager dialogs with
   no apply action, only the default is asserted: pressing it would act on external files,
   a provider or an installer.

Preparation uses public APIs and is not part of the keyboard claim: selections, fixture
models, the files that import cases open, an installed example extension, two camera
scenes, the project's fake Blender worker (`blender_job_tests --fake`, with a launcher that
holds a scene handoff open), and one simulated ChatGPT sign-in completion
(`ChatGptAuth::connected`; nothing is sent). Inside a flow, file names in Qt's file
dialog are set directly before Enter; the file dialogs themselves are audited by key.

**Native file dialogs.** No platform theme is loaded in the fixtures, so Qt uses its
own widget file dialog, and the audit traverses it (eighteen instances, from Open model to
the Blender handoff chooser). On a desktop where Qt uses the XDG portal or a GTK
dialog, that dialog is outside the app's control; the app's path to it is the same
keyboard path audited here.

**Focus after closing.** Xvfb runs no window manager, and the headless Weston session
does not reactivate the main window after a dialog closes, so "focus returns" is measured
as the owning window's focus child (`windowReactivated` is recorded and false). After a
non-modal window hides itself (the Render sheet after Render, the progress windows), Weston
can leave no window focused, and synthetic keys give Qt no Wayland input serial to request
activation with. Between cases the audit then maps the model window again, which the
compositor focuses, as a user re-selecting it would. With a physical keyboard the serial
exists; whether the compositor refocuses the parent is compositor policy and is not tested.

## Findings and fixes

On `main` (`0874ab4f` with only the audit added), **40 of the 102 dialogs failed** on X11 1×
([parent record](R084u-all-dialog-keyboard.json), `parentOnMain`). All are fixed in the code
commit; the same audit now passes every dialog on X11 and Wayland at 1× and 2×.

| Dialogs that failed on `main` | Finding | Fix |
|---|---|---|
| Keyboard shortcuts | Shift+Tab in "New key combination" recorded `Shift+Backtab` and kept focus; Tab moved on | Shift+Tab moves focus back (`window_shortcuts.cpp`); the shortcut policy reserves Tab keys, so nothing assignable is lost |
| Recover work | List and details unnamed; Qt made **Discard recovery data** the default button, so Enter in the list would discard a recovery copy | Names; Discard is no longer auto-default (Qt's default becomes Open recovered version) |
| Field of view | The input dialog's number editor unnamed | Explicit `QInputDialog` with the editor named after its label |
| Extensions (empty, and with an installed extension) | Parameter scroll area unnamed; parameter editors were added after the dialog buttons in the Tab order; numeric editors unnamed | Name; `setTabOrder` after the scroll area; editor names |
| Set original face color, Material color, Section and Model style colour choosers | Qt colour dialog's hue/saturation field, value strip and six number editors unnamed | `nameColorDialog` (new `src/app/accessible_names.hpp`); the two static `getColor` calls become dialogs |
| Model style, New/Edit section plane | Colour buttons named by their value (`#f0f1ec`) | "Background color", "Fill color"…; the value becomes the description |
| New/Edit material | Colour field and opacity editor unnamed; OK without a change reported "Batch has no committed changes" and stayed open | Names; OK on an unchanged material closes |
| Create/Edit 3D text | The Text editor trapped Tab; font family/style editors and Position X/Y/Z unnamed | `setTabChangesFocus(true)`; names |
| New dimension, New label, Edit annotation, Annotation color | The text editor trapped Tab (so the colour button was unreachable); world-point and offset X/Y/Z fields, size editor and the value-named colour button unnamed | `setTabChangesFocus(true)`; names ("World point 1 Y", "Text offset Z", "Color") |
| Save template / component to library | The Description editor trapped Tab | `setTabChangesFocus(true)` |
| Sun and shadows, Export measured PDF/SVG, Export view as PNG, DXF import options, Render with Blender, Export saved scenes as frames | Date, time and number editors unnamed (Render: also its scroll area and worker log) | `nameInternalEditors` gives each editor its control's label; names |
| DXF, STL and OBJ export reports, glTF and Formline import reports, Measured drawing report | Details pane unnamed | Names |
| Geometry diagnostics | Findings list and details unnamed | Names (also the orientation reference choice) |
| Rename, Move to parent, Assign tag (Outliner) | **Space on any focused Outliner button ran Hide/show** (a page-wide Space shortcut), so the buttons could not be pressed and visibility toggled instead | Space toggles visibility from the tree only (`organization_panel.cpp`) |
| Assistant sheet (window narrower than 1100 px) | Panel and task area unnamed; Qt made **Preferences…** the sheet's default, so Enter in a field opened Preferences | Names; the panel's buttons are not auto-default in the sheet, as when docked |
| What is sent (permission before OpenAI requests) | The request editor trapped Tab, so Send was unreachable by Tab | `setTabChangesFocus(true)` on the request; after Cancel, focus now returns to Send (it went to the header Commands button once Send was reachable) |

Fixed at the same time, in states the audit does not reach on `main`: the recovery interval
(disabled while recovery is off), the STL weld-tolerance editor (disabled until welding is on),
and the diagnostics orientation reference (shown only for orientation findings).

## Open questions (not changed; UI decisions)

- **Render with Blender, reopened after a render.** Render is disabled while the job
  starts, Qt moves focus to **Jobs…**, and the sheet keeps that focus when reopened, so
  Enter then opens Jobs instead of Render. The first opening (audited) defaults to Render.
  Recorded as defect [D-026](R089a-defect-triage.md) (Minor).
- **Default buttons chosen by Qt in dialogs without an apply action.** Extensions
  (**Install…**), Render jobs (**Open image**), Geometry diagnostics (**Refresh**) and
  Recover work (now **Open recovered version**) take Qt's first auto-default button. None
  edits the model, but whether Enter should run an action there is a design choice.

## Matrix

Local runner `scripts/run-container-check.sh` (image
`sketchyup-native-fonts-check:20261008`, one CPU, 4 GiB, Qt 6.11.2), `build/dev` built
on the host. Commands are the `native.yml` lines; outputs went to the run's capture
directory because the source mount is read-only.

### All-dialog audit

Every group passes in every configuration: **64/64 group runs, 102 dialogs × 4 = 408 dialog
results, no finding**, plus the inventory (exit 0: every "…" command is covered). Runs
`r084u-x11-audit4` and `r084u-wayland-audit4`, source `308cc5fe`; seconds include starting
Xvfb or Weston.

| Group | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
|---|---|---|---|---|
| `settings` | pass, 8.9 s | pass, 12.9 s | pass, 14.5 s | pass, 40.6 s |
| `app` | pass, 8.7 s | pass, 12.7 s | pass, 12.2 s | pass, 34.5 s |
| `files` | pass, 8.4 s | pass, 12.5 s | pass, 11.0 s | pass, 35.5 s |
| `dxf` | pass, 8.2 s | pass, 13.0 s | pass, 12.8 s | pass, 39.8 s |
| `stl-obj` | pass, 8.0 s | pass, 12.9 s | pass, 12.0 s | pass, 39.1 s |
| `import` | pass, 9.5 s | pass, 14.9 s | pass, 13.9 s | pass, 43.1 s |
| `components` | pass, 9.3 s | pass, 13.9 s | pass, 13.4 s | pass, 47.2 s |
| `materials` | pass, 10.3 s | pass, 16.6 s | pass, 17.5 s | pass, 50.2 s |
| `styles` | pass, 8.8 s | pass, 14.0 s | pass, 12.3 s | pass, 37.4 s |
| `sections` | pass, 12.8 s | pass, 17.7 s | pass, 16.5 s | pass, 48.5 s |
| `annotations` | pass, 9.6 s | pass, 15.6 s | pass, 14.7 s | pass, 47.8 s |
| `outliner` | pass, 7.9 s | pass, 14.8 s | pass, 13.6 s | pass, 46.7 s |
| `animation` | pass, 17.0 s | pass, 21.1 s | pass, 27.1 s | pass, 47.7 s |
| `blender` | pass, 10.3 s | pass, 14.6 s | pass, 17.2 s | pass, 30.8 s |
| `assistant` | pass, 7.4 s | pass, 11.5 s | pass, 10.0 s | pass, 28.3 s |
| `render` | pass, 6.1 s | pass, 7.8 s | pass, 5.4 s | pass, 16.4 s |

Earlier official audit runs are retained. In `r084u-*-audit` and `r084u-*-audit2`,
`blender-handoff-progress` failed its preparation on Wayland because, after the Render sheet
hid itself, Weston left no window focused and the model window could not be reactivated with
synthetic input (see above); the harness now maps the model window again and waits for
activation to settle. `r084u-*-audit3` passed everything with the source before the
keyboard-path labels in the inventory were corrected. No product code changed between them.

### Affected native suites

The suites that exercise the changed dialogs and panels, with their `native.yml` lines
(runs `r084u-x11` and `r084u-wayland`, product source identical to `308cc5fe`):

| Suite | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
|---|---|---|---|---|
| `annotation_input_tests` | pass, 5.5 s | pass, 7.7 s | pass, 5.8 s | pass, 25.3 s |
| `annotation_viewport_tests` | pass, 4.5 s | pass, 5.4 s | pass, 2.5 s | pass, 4.1 s |
| `assistant_panel_tests` | pass, 11.5 s | pass, 18.5 s | pass, 13.2 s | pass, 33.3 s |
| `diagnostics_input_tests` | pass, 10.9 s | pass, 19.9 s | pass, 17.3 s | pass, 53.0 s |
| `dxf_input_tests` | — | pass, 7.7 s | — | pass, 15.4 s |
| `extension_input_tests` | exit 1, 3.3 s | exit 1, 3.2 s | exit 1, 0.5 s | exit 1, 1.3 s |
| `formline_input_tests` | pass, 5.7 s | pass, 6.5 s | pass, 5.2 s | pass, 13.6 s |
| `gltf_input_tests` | — | pass, 6.1 s | — | pass, 10.4 s |
| `keyboard_outliner_input_tests` | pass, 6.1 s | pass, 8.7 s | pass, 5.6 s | pass, 18.3 s |
| `keyboard_panel_tabs_tests` | pass, 10.1 s | pass, 23.7 s | pass, 18.2 s | pass, 37.3 s |
| `library_input_tests` | pass, 5.9 s | pass, 7.2 s | pass, 4.7 s | pass, 12.0 s |
| `m4_workflow_tests` | pass, 6.9 s | pass, 8.9 s | pass, 6.0 s | pass, 9.4 s |
| `material_input_tests` | pass, 6.2 s | pass, 9.0 s | pass, 5.9 s | pass, 19.6 s |
| `measured_input_tests` | pass, 6.4 s | pass, 7.0 s | pass, 4.8 s | pass, 13.3 s |
| `obj_input_tests` | — | pass, 7.8 s | — | pass, 13.0 s |
| `organization_input_tests` | pass, 6.1 s | pass, 9.0 s | pass, 7.4 s | pass, 18.0 s |
| `preference_input_tests` | pass, 5.9 s | pass, 7.1 s | pass, 4.7 s | pass, 10.1 s |
| `raster_export_input_tests` | pass, 7.4 s | pass, 9.9 s | pass, 5.7 s | pass, 14.7 s |
| `recovery_input_tests` | pass, 6.7 s | pass, 9.8 s | pass, 7.4 s | pass, 18.2 s |
| `render_input_tests` | — | pass, 20.5 s | pass, 14.6 s | pass, 24.8 s |
| `render_input_tests-blender` | exit 1, 6.3 s | — | — | — |
| `render_input_tests-noblender-substitute` | pass, 12.5 s | — | — | — |
| `render_input_tests-section` | — | — | — | pass, 34.6 s |
| `section_input_tests` | pass, 5.0 s | pass, 6.7 s | pass, 5.0 s | pass, 17.2 s |
| `shortcut_input_tests` | pass, 8.0 s | pass, 13.4 s | pass, 12.5 s | pass, 37.6 s |
| `solar_input_tests` | pass, 16.7 s | pass, 26.1 s | pass, 17.3 s | pass, 26.3 s |
| `stl_input_tests` | — | pass, 7.7 s | — | pass, 14.1 s |
| `style_input_tests` | pass, 5.6 s | pass, 7.9 s | pass, 6.4 s | pass, 20.8 s |
| `text_input_tests` | pass, 13.4 s | pass, 17.5 s | pass, 15.0 s | pass, 37.3 s |
| `text_size_input_tests` | pass, 7.6 s | pass, 12.7 s | pass, 8.3 s | pass, 22.2 s |
| `units_input_tests` | pass, 5.9 s | pass, 8.7 s | pass, 5.7 s | pass, 14.3 s |
| `visible_focus_input_tests` | pass, 40.2 s | exit 124, 93.2 s | exit 1, 60.1 s | exit 1, 60.7 s |

The three non-passing entries are environmental:

- `extension_input_tests` compiles its fixture path in (`SOURCE_DIR`, the host checkout),
  and the runner mounts the checkout at `/source`. With the checkout mounted at its build
  path (same image, same invocations, a development container) it passes on all four.
- `render_input_tests` with `SKETCHYUP_BLENDER_TEST=/usr/bin/blender` needs Blender, which the
  image lacks. The same invocation without that variable passes.
- `visible_focus_input_tests` exceeded the 90 s (X11) and 60 s (Wayland) caps on this
  one-CPU runner. `main` without this change takes as long in the same environment, and
  both pass with longer caps: X11 2× 113 s and 111 s, Wayland 1× 76 s and 75 s, Wayland 2×
  167 s and 170 s (parent and branch, development container). R084.q recorded 14–32 s on
  2026-10-10, so the slowdown predates this layer; it is a CI timing risk for that suite.

The headless suite (`ctest --preset headless`) passed 62/62.

## CI

`native.yml` gains the step "All-dialog keyboard audit": the inventory, then every group
on X11 at 2× and on Wayland at 1× (32 audit invocations, about 26 s per group locally).
Together these cover both platforms and both scales in about 8 minutes. The full four-way
matrix above is the local evidence and is not repeated in CI. Wayland 2× is the slowest
configuration: up to 50.2 s per group against `test-wayland.sh`'s 60 s cap, comparable to
`diagnostics_input_tests` at 53.0 s in the same run. That margin is too thin for a loaded
one-CPU CI host; adding Wayland 2× to CI would first need smaller groups. The main window is 1100 × 560 during the audit: wide enough for
the docked Assistant, and small enough that software rendering of the 2× window does not
dominate. The JSON reports land in `build/dialog-keyboard/`.

## Not covered

Screen readers (AT-SPI announcements, Orca), physical keyboards and input methods,
portal/GTK file dialogs, menu-mnemonic navigation on Wayland, visible focus (R084.q) and
every intermediate state of each dialog (for example Recover work with a real recovery
copy, a running render, or a failed provider sign-in). The sanitized builds were not run.
[The machine-readable record](R084u-all-dialog-keyboard.json) has every dialog's
controls, Tab and Shift+Tab orders, default button and focus result per platform.

## Exchange commands (2026-10-25)

The IFC, STEP and SketchUp exchange commands (R097.e1, R098.a3, R098.c2, R102.c) came after
the audit above. Their eight "…" commands had no case, so the inventory failed on `main`.
Five cases in the `exchange` group cover the commands that open a "not installed"
availability sheet directly, with no file chooser first. The offline fixture has no helper,
and the SketchUp helper is built under `src/skp`, not beside the audit binary, so it is not
found either. Each sheet's OK is the default. Enter and Escape both close it, with no document
or preference change.

Three commands, Show IFC, STEP and SketchUp import report…, are disabled until an import
completes. An import needs a helper, so they are exempt by action objectName in
`needsPriorImport()`, and the inventory lists the exemption. The suites
`ifc_import_dialog_tests`, `step_import_dialog_tests` and `skp_import_dialog_tests` re-show
each report from its command and check its contents. They close it by clicking OK, though,
so no suite audits the report's own Enter or Escape. That gap is open.

Run on 2026-10-25, on top of `f47cbdeb`, with Qt 6.12.0 under `QT_QPA_PLATFORM=offscreen`.
`xvfb-run` and Wayland are not available on this machine. The inventory exits 0 with no
uncovered commands and 126 dialogs; the table below predates the five new cases.
`--dialogs-keyboard=exchange` passes 5/5. The X11 2× and Wayland 1× runs of the new group are
not measured here; CI runs them.

## Inventory and results

| # | Dialog | Keyboard path | Group | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
|---|---|---|---|---|---|---|---|
| 1 | Drawing plane | Draw > Drawing plane > Custom plane… (palette) | `settings` | pass | pass | pass | pass |
| 2 | Keyboard shortcuts | Edit > Keyboard shortcuts… (palette) | `settings` | pass | pass | pass | pass |
| 3 | Commands and model search | Ctrl+K (View > Commands…) | `settings` | pass | pass | pass | pass |
| 4 | Document units | File > Document units… (palette) | `settings` | pass | pass | pass | pass |
| 5 | Welcome to SketchyUp (first-run units) | Opens itself at first start with keyboard focus | `settings` | pass | pass | pass | pass |
| 6 | Recovery settings | File > Recovery settings… (palette) | `settings` | pass | pass | pass | pass |
| 7 | Recover work | File > Recover work… (palette) | `settings` | pass | pass | pass | pass |
| 8 | Field of view | View > Field of view… (palette) | `settings` | pass | pass | pass | pass |
| 9 | Interface text size | View > Interface text size… (palette) | `settings` | pass | pass | pass | pass |
| 10 | Extensions | Extensions > Manage extensions… (palette) | `app` | pass | pass | pass | pass |
| 11 | Extensions (with an enabled extension) | Extensions > Manage extensions… (palette) | `app` | pass | pass | pass | pass |
| 12 | Install extension (file chooser) | Extensions dialog > Install… (Tab, Space) | `app` | pass | pass | pass | pass |
| 13 | Set original face color (color chooser) | Edit > Set original face color… (palette) | `app` | pass | pass | pass | pass |
| 14 | Could not complete edit (error message) | Any command that fails, e.g. Edit > Save component to library… with no component | `app` | pass | pass | pass | pass |
| 15 | Open model (file chooser) | File > Open… (Ctrl+O) | `app` | pass | pass | pass | pass |
| 16 | Save model (file chooser) | File > Save as… (palette; also Ctrl+Shift+S) | `app` | pass | pass | pass | pass |
| 17 | Replace model? (confirmation) | Save as… to an existing file name | `app` | pass | pass | pass | pass |
| 18 | Unsaved changes (confirmation) | File > New (Ctrl+N) with unsaved edits | `app` | pass | pass | pass | pass |
| 19 | Save failed (error message) | Save as… to a location that cannot be written | `files` | pass | pass | pass | pass |
| 20 | New from template | File > New from template… (palette) | `files` | pass | pass | pass | pass |
| 21 | Choose library folder (folder chooser) | New from template > Choose folder… (Tab, Space) | `files` | pass | pass | pass | pass |
| 22 | Save template to library | File > Save template to library… (palette) | `files` | pass | pass | pass | pass |
| 23 | DXF export options | File > Export 2D DXF… (palette) | `dxf` | pass | pass | pass | pass |
| 24 | New 2D DXF drawing (file chooser) | Export 2D DXF… > OK | `dxf` | pass | pass | pass | pass |
| 25 | DXF export report | Export 2D DXF… > OK > file name | `dxf` | pass | pass | pass | pass |
| 26 | Import 2D DXF (file chooser) | File > Import 2D DXF… (palette) | `dxf` | pass | pass | pass | pass |
| 27 | DXF import options | Import 2D DXF… > file name | `dxf` | pass | pass | pass | pass |
| 28 | STL export options | File > Export STL… (palette) | `dxf` | pass | pass | pass | pass |
| 29 | STL export report | Export STL… > OK > file name | `stl-obj` | pass | pass | pass | pass |
| 30 | Import STL (file chooser) | File > Import STL… (palette) | `stl-obj` | pass | pass | pass | pass |
| 31 | STL import options | Import STL… > file name | `stl-obj` | pass | pass | pass | pass |
| 32 | OBJ export options | File > Export OBJ package… (palette) | `stl-obj` | pass | pass | pass | pass |
| 33 | OBJ export report | Export OBJ package… > OK > name | `stl-obj` | pass | pass | pass | pass |
| 34 | Import OBJ/MTL (file chooser) | File > Import OBJ/MTL… (palette) | `import` | pass | pass | pass | pass |
| 35 | OBJ import options | Import OBJ/MTL… > file name | `import` | pass | pass | pass | pass |
| 36 | Import GLB/glTF (file chooser) | File > Import GLB/glTF… (palette) | `import` | pass | pass | pass | pass |
| 37 | GLB/glTF import report | Import GLB/glTF… > file name | `import` | pass | pass | pass | pass |
| 38 | Import Formline (file chooser) | File > Import Formline… (palette) | `import` | pass | pass | pass | pass |
| 39 | Formline import report | Import Formline… > file name | `import` | pass | pass | pass | pass |
| 40 | Export measured PDF / SVG | File > Export measured PDF/SVG… (palette) | `components` | pass | pass | pass | pass |
| 41 | Measured drawing exported (report) | Export measured PDF/SVG… > Save > file name | `import` | pass | pass | pass | pass |
| 42 | Export view as PNG | File > Export view as PNG… (palette) | `components` | pass | pass | pass | pass |
| 43 | Make component | Edit > Make component… (G, palette) | `components` | pass | pass | pass | pass |
| 44 | Place component | Edit > Place component… (palette) | `components` | pass | pass | pass | pass |
| 45 | Replace component | Edit > Replace component… (palette) | `components` | pass | pass | pass | pass |
| 46 | Change component axes | Edit > Change component axes… (palette) | `components` | pass | pass | pass | pass |
| 47 | Insert library component | Edit > Browse component library… (palette) | `files` | pass | pass | pass | pass |
| 48 | Save component to library | Edit > Save component to library… (palette) | `files` | pass | pass | pass | pass |
| 49 | Set component glue face | Edit > Set glue face… (palette) | `components` | pass | pass | pass | pass |
| 50 | Attachment placement options | Edit > Attachment placement options… (palette) | `components` | pass | pass | pass | pass |
| 51 | Geometry diagnostics | Edit > Geometry diagnostics… (palette) | `components` | pass | pass | pass | pass |
| 52 | New material | View > Materials (palette), then New… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 53 | Material color (color chooser) | New material > Choose… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 54 | Edit entity | View > Entity info (palette), then Edit entity… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 55 | Model style | View > Model styles (palette), then Colors and details… (Tab, Space) | `styles` | pass | pass | pass | pass |
| 56 | Sun and shadows | View > Sun and shadows (palette), then Edit sun study… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 57 | New saved scene | View > Saved scenes (palette), then New… (Tab, Space; or Ctrl+Shift+N) | `styles` | pass | pass | pass | pass |
| 58 | New section plane | View > Section planes (palette), then New… (Tab, Space) | `sections` | pass | pass | pass | pass |
| 59 | Create 3D text | View > 3D text (palette), then Create 3D text… (Tab, Space) | `sections` | pass | pass | pass | pass |
| 60 | Import reference image (file chooser) | View > Reference images (palette), then Import reference image… (Tab, Space) | `styles` | pass | pass | pass | pass |
| 61 | Place reference image | Import reference image… > file | `annotations` | pass | pass | pass | pass |
| 62 | Edit reference image | View > Reference images (palette), then Edit selected image… (Tab, Space) | `annotations` | pass | pass | pass | pass |
| 63 | Calibrate reference image | View > Reference images (palette), then Calibrate selected image… (Tab, Space) | `sections` | pass | pass | pass | pass |
| 64 | Rename (outliner) | View > Outliner (palette), then Rename (Tab, Space; or F2) | `outliner` | pass | pass | pass | pass |
| 65 | Move to parent (outliner) | View > Outliner (palette), then Move to (Tab, Space; or Ctrl+Shift+M) | `outliner` | pass | pass | pass | pass |
| 66 | Assign tag | View > Outliner (palette), then Tag (Tab, Space; or Ctrl+Alt+T) | `outliner` | pass | pass | pass | pass |
| 67 | New tag | View > Tags and folders (palette), then New tag (Tab, Space; or Ctrl+Alt+N) | `outliner` | pass | pass | pass | pass |
| 68 | New tag folder | View > Tags and folders (palette), then Folder (Tab, Space; or Ctrl+Alt+F) | `outliner` | pass | pass | pass | pass |
| 69 | Edit material | View > Materials (palette), then Edit… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 70 | Attach file (file chooser) | View > Materials (palette), then Attach file… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 71 | Replace stored file (file chooser) | View > Materials (palette), then Replace file… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 72 | Texture mapping | View > Materials (palette), then Texture mapping… (Tab, Space) | `materials` | pass | pass | pass | pass |
| 73 | Model style color (color chooser) | Model style > Background (Tab, Space) | `styles` | pass | pass | pass | pass |
| 74 | Update saved scene | View > Saved scenes (palette), then Update… (Tab, Space) | `styles` | pass | pass | pass | pass |
| 75 | Rename saved scene | View > Saved scenes (palette), then Rename… (Tab, Space; or F2) | `styles` | pass | pass | pass | pass |
| 76 | Edit section plane | View > Section planes (palette), then Edit… (Tab, Space) | `sections` | pass | pass | pass | pass |
| 77 | Section fill color (color chooser) | Section plane dialog > fill color (Tab, Space) | `sections` | pass | pass | pass | pass |
| 78 | New dimension | View > Dimensions and labels (palette), then Dimension… (Tab, Space; or Ctrl+Shift+D) | `annotations` | pass | pass | pass | pass |
| 79 | New label | View > Dimensions and labels (palette), then Label… (Tab, Space; or Ctrl+Shift+L) | `annotations` | pass | pass | pass | pass |
| 80 | Edit annotation | View > Dimensions and labels (palette), then Edit / rebind… (Tab, Space) | `annotations` | pass | pass | pass | pass |
| 81 | Annotation color (color chooser) | Annotation dialog > color (Tab, Space) | `annotations` | pass | pass | pass | pass |
| 82 | Edit 3D text | View > 3D text (palette), then Edit text… (Tab, Space; or F2) | `sections` | pass | pass | pass | pass |
| 83 | Rename tag | View > Tags and folders (palette), then Rename (Tab, Space; or F2) | `outliner` | pass | pass | pass | pass |
| 84 | Move tag to folder | View > Tags and folders (palette), then Move to (Tab, Space; or Ctrl+Shift+M) | `outliner` | pass | pass | pass | pass |
| 85 | Export saved scenes as PNG frames | Render with Blender > Export scene animation… (Tab, Space) | `animation` | pass | pass | pass | pass |
| 86 | Choose animation parent folder (folder chooser) | Export saved scenes > Choose parent folder… (Tab, Space) | `animation` | pass | pass | pass | pass |
| 87 | Export scene animation (progress) | Export saved scenes > Export frames (Enter) | `blender` | pass | pass | pass | pass |
| 88 | Save render image (file chooser) | Render (Tab, Space), then Save image as… on the result page (Tab, Space) | `blender` | pass | pass | pass | pass |
| 89 | Save and open Blender scene (file chooser) | Render (Tab, Space), then Open scene in Blender… on the result page (Tab, Space) | `blender` | pass | pass | pass | pass |
| 90 | Assistant (sheet in a narrow window) | View > Assistant (Ctrl+J) when the window is narrower than 1100 px | `assistant` | pass | pass | pass | pass |
| 91 | What is sent (permission before OpenAI requests) | Assistant composer: type a request, then Send (Tab, Space) | `assistant` | pass | pass | pass | pass |
| 92 | One-way Blender handoff (progress) | Open scene in Blender… > file name | `animation` | pass | pass | pass | pass |
| 93 | You're using your ChatGPT plan (message) | Opens itself in Assistant preferences when a ChatGPT sign-in completes | `assistant` | pass | pass | pass | pass |
| 94 | Render with Blender | Camera > Render… (palette) | `render` | pass | pass | pass | pass |
| 95 | Render jobs | Render with Blender > Jobs… (Tab, Space) | `render` | pass | pass | pass | pass |
| 96 | Choose Blender (file chooser) | Render with Blender > Choose executable… (Tab, Space) | `render` | pass | pass | pass | pass |
| 97 | Choose HDR environment (file chooser) | Render with Blender > Choose HDR panorama… (Tab, Space) | `render` | pass | pass | pass | pass |
| 98 | Assistant preferences | View > Assistant (Ctrl+J), then Preferences… (Tab, Space) | `assistant` | pass | pass | pass | pass |
| 99 | Assistant preferences (Ollama) | View > Assistant (Ctrl+J), then Preferences… (Tab, Space) | `assistant` | pass | pass | pass | pass |
| 100 | Assistant preferences (OpenAI, ChatGPT plan) | View > Assistant (Ctrl+J), then Preferences… (Tab, Space) | `assistant` | pass | pass | pass | pass |
| 101 | Assistant preferences (OpenAI, API key) | View > Assistant (Ctrl+J), then Preferences… (Tab, Space) | `assistant` | pass | pass | pass | pass |
| 102 | What is sent (assistant disclosure) | View > Assistant (Ctrl+J), then What is sent (Shift+Tab, Space) | `assistant` | pass | pass | pass | pass |
