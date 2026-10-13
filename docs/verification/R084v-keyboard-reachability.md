# R084.v — Keyboard reachability of the main window

2026-10-13. Product and fixture on branch `build/r084v-keyboard-reachability` (based on
`main` plus R084.u, `build/r084u-all-dialog-keyboard` at `619e04f2`).

R084.u covered every dialog. The N05 row still had three open clauses in the main window:
the tool-rail buttons were not Tab stops, Tab passed tab-bar scroll buttons, and the
Model/Render and Assistant side tabs were not exercised by keyboard. This record closes
them and fixes [D-026](R089a-defect-triage.md).

## What did not work before the change

Found by code reading and by the new fixture while it was developed (Qt 6.11.2, real key
events only):

- **Tool rail.** The `QToolBar` itself was one `StrongFocus` stop; its buttons were
  `NoFocus` (Qt's default for toolbar buttons), so no single tool could be reached or
  chosen from the rail by keyboard. The buttons were created before the window's contents
  were assembled, so they sat about 250 positions away from the rail in the focus chain.
  The **Solid tools** button exposed its icon text "Solids" as its accessible name.
- **Auto-hidden tab bars.** `modelTabs` (Model/Render) and the side tabs hide their bar
  while they have one page. The tab widget stayed a Tab stop whose focus proxy (the
  hidden bar) could not take focus, so Tab stopped there and focus did not move. A
  hidden bar with Tab focus behaved the same.
- **Pages added later.** The Assistant page and render result pages join the focus chain
  far from their tab bar, so Tab from the bar skipped them and Shift+Tab from them did
  not return to the bar.
- **Context breadcrumb.** Shift+Tab onto the breadcrumb (a label with keyboard-accessible
  links) never left it: every further Shift+Tab stayed on the label. The reverse Tab
  cycle of the main window was therefore trapped.
- **Tab-bar scroll buttons** (`ScrollLeftButton`/`ScrollRightButton` in every `QTabBar`)
  were Tab stops, as R084.s recorded.
- **D-026.** With Render disabled while it had focus (what a job start does), Qt moved
  focus to **Jobs…**; closing and reopening the sheet kept it there, so Enter opened Jobs.
  The new fixture reproduced this before the fix (run `r084v-dev30`: "D-026: reopened
  Render with Blender focuses Render, not QPushButton/showRenderJobs").

## Change

- `src/app/keyboard_navigation.{hpp,cpp}` (new) and `src/app/window.cpp`:
  - **Tool rail, toolbar pattern.** The rail is `NoFocus`; exactly one button has Tab
    focus: the active tool's, or the first usable button when the active tool is not in
    the rail or is hidden. Focus requests on the rail itself (F6/Shift+F6) are forwarded
    to that button. Up/Left and Down/Right move between enabled, visible buttons and wrap;
    Home and End go to the ends; Enter clicks the focused button and Space is the button's
    own activation, so both run the tool exactly as a click does (focus moves to the
    canvas). Leaving the rail returns the stop to the active tool. Buttons are kept
    together just before the rail in the focus chain. Each button's accessible name is its
    action's full text (so the Solid tools button is "Solid tools"). Mouse clicks still do
    not take focus; layout, sizes, text and the tool lifecycle are unchanged, and the
    R084.q `QToolButton:focus` ring marks the focused tool.
  - **Overflow.** When the tools do not fit (at 900 px height the last tools, including
    Orbit and Pan, do not), Qt's overflow button is the rail's last stop, named
    **More tools**; Enter opens Qt's menu of the remaining tools.
  - **Tab bars.** Every `QTabBar` scroll button in the main window is `NoFocus`. Tab
    widgets whose bar hides itself are not stops of their own, and their bar is a stop
    only while shown.
  - **Late pages.** When a page added after construction becomes current in `modelTabs`
    or the side tabs (render results, the Assistant), its Tab stops are moved, in their
    own order, directly after the bar. Pages present at construction are left alone, as
    reordering the Model page breaks the Model panel's focus-proxy entry.
  - **Breadcrumb and component banner.** Shift+Tab leaves the label for the previous
    stop; Tab still walks its links one by one.
- `src/app/render_panel.cpp` (D-026): reopening "Render with Blender" focuses Render when
  it is enabled, which makes it the default button again; while a job runs (Render
  disabled) focus is left alone.
- Tests whose expectations encoded the old rail: `interaction_tests` (F6 now lands on a
  button inside the rail, not on the rail) and `visible_focus_input_tests` (`QToolBar` is
  no longer a required focus class, since the rail never holds focus; its buttons are
  `QToolButton`s). `keyboard_panel_tabs_tests` already allowed, but did not require, the
  scroll buttons, so it is unchanged.

## Fixture

`keyboard_reachability_tests` loads the demo model, points the saved Blender path at an
absent file so a system Blender is never probed, and uses `QTest::keyClick` on the focused
widget:

1. **Tool rail.** One Tab stop, on the active tool; Tab from Commands enters on Select.
   Down/Up/Right/Left with wrapping at both ends, Home and End, the active tool unchanged.
   A disabled and a hidden tool are skipped. Shift+Tab and Tab leave the rail from any
   button, and re-entry lands on the active tool. Enter chooses Rectangle and Space
   chooses Paint (the active tool changes and focus moves to the canvas, as a click does);
   Shift+F6 then lands on the new active tool. Every button's accessible name is its tool
   name. End reaches **More tools**; on X11, Enter opens its menu and arrows and Enter
   choose Pan, after which the rail's stop is its first tool (the active one is in the
   overflow). Qt's Wayland popups need a compositor input serial that synthetic keys
   cannot supply, so the menu is not opened on Wayland.
2. **Tab cycle.** From the canvas (Paint active; with Select the canvas keeps Tab for
   entity cycling), the full Tab and Shift+Tab cycles return to the canvas, contain no
   tab-bar scroll button, visit each widget once (a label's links count as one) and have
   exactly one tool-rail stop. Together they reach the Model-panel tab bar.
3. **Side tabs.** Ctrl+J shows the Assistant side tab with focus inside it; Shift+Tab
   reaches the side tab bar; Left/Right switch between Model and Assistant, and from the
   bar Tab moves into each page and Shift+Tab walks back to the bar. Tab from the canvas
   also reaches the bar, both cycles are checked again, and Ctrl+J hides the Assistant
   and returns to the canvas.
4. **D-026 and Model/Render.** Opens Render with Blender from the palette, points it at
   the project's fake Blender worker (`blender_job_tests --fake success`) and checks it.
   With Render focused it disables Render, as a job start does, and asserts that Qt moved
   focus to Jobs…; Escape closes the sheet; Render is re-enabled; the palette reopens it.
   Render must have focus, Enter must not open Jobs and must render (a Render result tab
   appears). On the Model/Render bar, Left/Right switch tabs and Tab/Shift+Tab move into
   each page and back; both cycles are checked once more with the Model/Render bar shown.

## Results

Local runner `scripts/run-container-check.sh` (image
`sketchyup-native-fonts-check:20261008`, one CPU, 4 GiB, Qt 6.11.2), `build/dev` built on
the host. Commands are the `native.yml` lines (`timeout 90s` on X11; `scripts/test-wayland.sh`
caps each Wayland run at 60 s). Seconds include starting Xvfb or Weston. Runs
`r084v-matrix1` (all suites), `r084v-matrix2` (the final fixture and
`visible_focus_input_tests`, with a fake `blender` that sleeps 45 s first on `PATH`) and
`r084v-audit3` (the audit groups on R084.u `619e04f2`, same slow `blender` on `PATH`).

| Case | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
|---|---|---|---|---|
| `keyboard_reachability_tests` (final, matrix2) | pass, 13.8 s | pass, 27.6 s | pass, 21.6 s | pass, 34.4 s |
| `keyboard_panel_tabs_tests` | pass, 9.1 s | pass, 20.5 s | pass, 15.8 s | pass, 25.9 s |
| `visible_focus_input_tests` (final, matrix2) | pass, 38.1 s | pass, 92.7 s | **timeout**, 60.1 s | **timeout**, 60.6 s |
| `tool_lifecycle_tests` | pass, 5.0 s | pass, 5.2 s | pass, 2.5 s | pass, 7.3 s |
| `assistant_panel_tests` | pass, 10.7 s | pass, 17.7 s | pass, 13.1 s | pass, 30.4 s |
| `render_input_tests` | pass, 13.2 s | pass, 20.1 s | pass, 14.8 s | pass, 24.3 s |
| `interaction_tests` | pass, 5.6 s | pass, 6.6 s | pass, 3.8 s | pass, 8.9 s |
| `native_accessibility_audit` (F6 inventory) | pass, 6.6 s | pass, 9.8 s | pass, 6.0 s | pass, 6.3 s |
| `native_accessibility_audit --dialogs-keyboard=render` | pass, 7.8 s | pass, 7.8 s | pass, 6.2 s | pass, 14.7 s |
| `native_accessibility_audit --dialogs-keyboard=assistant` | pass, 7.4 s | pass, 11.5 s | pass, 9.6 s | pass, 27.6 s |

`ctest --preset headless`: 62/62 passed.

Notes on the runs:

- `visible_focus_input_tests` on Wayland hit the 60 s cap of `scripts/test-wayland.sh` in
  both matrices before printing a result; the same suite needs 92.7 s on X11 2×. The cap was
  not raised. This suite was already slow on `main` under local load; no `main` run was
  repeated here for comparison, so whether these Wayland runs would pass within the cap on
  an unloaded host or in CI is not shown locally. In `r084v-matrix1` its X11 runs failed
  only because `QToolBar` was still a required focus class; the change above fixed that and
  both X11 runs then passed with no uncovered class and no failure (421 measured controls).
- `keyboard_reachability_tests` Wayland 2× failed in `r084v-matrix1` because an earlier
  fixture revision opened the overflow menu on Wayland ("Enter on More tools opens a menu
  that lists Pan"); the menu step is now X11-only, as R084.s does for the View menu.
- The `r084v-matrix1` audit groups (R084.u `02886d42`) also passed 8/8; the table shows
  the rerun on `619e04f2`.

## Not covered

- No run on the `main` parent was kept for the whole fixture. Only the D-026 step was
  shown to fail without its fix (`r084v-dev30`, above); the rail and Tab-bar findings come
  from code reading and fixture development runs.
- The overflow menu is driven by keys on X11 only. The Model-panel tab bar is reached by
  Shift+Tab from its page; Tab from the canvas enters the panel at its focus proxy (the
  current page), as R084.s records. Shift+Tab no longer walks a breadcrumb's links
  backwards; Tab still does.
- `tests/accessibility_bridge_test.py` was not run: `libatspi.so.0` is absent from every
  local image. Screen-reader announcements, real window-manager focus policies and the
  sanitized builds were not run. The interactive Orca session stays waived
  ([ADR 0161](../decisions/0161-m9-hardware-evidence-waiver.md)). R084/M9 acceptance remains
  open.
