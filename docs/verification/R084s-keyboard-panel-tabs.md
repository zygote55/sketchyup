# R084.s — Keyboard-only Model panel tabs

2026-10-10. Product and fixture on branch `build/r084s-keyboard-panel-tabs`.

The earlier audit, AT-SPI bridge and organization fixtures switch Model-panel tabs
programmatically or with an AT-SPI Press. Nothing established that a keyboard user
can reach each of the twelve tabs (Outliner, Tags, Info, Materials, History, Styles,
Scenes, Sections, Annotations, 3D Text, Sun, Images), land in the page content and
get back to the viewport. This record closes that gap for N05.

## What worked and what did not before the change

Observed with a throwaway probe on the parent (Qt 6.11.2, real key events only):

- The tab bar is focusable (Qt's default `Tab` focus). It is **not** an F6 region:
  F6 lands on the Outliner tree, or on the current page's content, because the panel's
  focus proxy follows the tab. The bar is reached only by `Shift+Tab` from the page (three
  presses from the Outliner: search field, a tab scroll button, the bar).
- `Left`/`Right` on the bar switch tabs; `Home`/`End` do nothing. `Ctrl+Tab` and
  `Ctrl+Shift+Tab` from page content switch tabs and move focus into the new page,
  wrapping at both ends. `Ctrl+PageUp/PageDown` are swallowed by tree and list views
  and so are not a reliable path.
- `Tab` from the bar passes through one or two of the bar's scroll buttons (the panel
  is 248 px wide, so they are visible) before the page content.
- No focus was lost or trapped on any of those paths, and F6 always returned to the
  viewport.
- The View menu and command palette had actions for History, Saved scenes, Section
  planes, Dimensions and labels, 3D text, Sun and shadows, Reference images and Model
  styles, but **none for Outliner, Tags, Info or Materials** (Materials only opened as
  a side effect of the Paint tool). Entity info therefore had no menu or palette
  command at all. Of the eight existing actions only History and Reference images moved
  focus; the other six left it in the viewport, so a palette command changed the visible
  tab but not the keyboard target. View > History also did not reveal the Model panel
  when the Assistant tab was in front (by code inspection; not run before the change).

## Change

- Four public View actions, unbound by default and therefore listed in the command
  palette and the shortcut editor: `view.outliner` "Outliner", `view.tags` "Tags and
  folders", `view.info` "Entity info" and `view.materials` "Materials", implemented by
  new `OrganizationPanel::showOutliner/showTags/showInfo` (`showMaterials` existed).
  The shortcut policy (R084.e/j) documents no default-binding pattern for panels, and
  the eight existing panel actions are unbound, so none is added.
- All twelve panel actions now share one helper that reveals the Model panel (including
  from the Assistant tab), selects the tab and moves focus into the page via the panel's
  focus proxy. Menu and palette commands therefore end with focus in the content, as
  History and Reference images already did. `showMaterials()` itself still does not
  take focus, so choosing the Paint tool from the keyboard does not leave the viewport.
- No style-sheet change; PR #286's focus rules apply unchanged.

## Fixture

`keyboard_panel_tabs_tests` starts with viewport focus, then uses `QTest::keyClick` on
the focused widget only:

1. F6 to the panel, `Shift+Tab` to the bar, then for each tab `Right`, `Tab` (only the
   bar's own scroll buttons may intervene) into the page, `Shift+Tab` back. `Right` at
   the end stays put; `Left` walks back to the first tab. F6 returns to the viewport.
2. `Ctrl+Tab` through all twelve tabs with wrap-around, then `Ctrl+Shift+Tab` back.
3. Ctrl+K, typed label, arrows and Enter for Outliner, Tags, Info, Materials (new) and
   Scenes, Images (changed).
4. View menu: all twelve actions are asserted to be listed. On X11 Alt+V, arrows and
   Enter also choose Entity info and History. Qt's Wayland popups need a compositor
   input serial that synthetic keys cannot supply, so that sub-step is X11-only.

Each step checks the visible tab title and page, that focus is inside that page (not
on the bar), and that every key leaves some widget focused. Test order and action
labels are asserted so a new tab cannot be added without coverage.

## Results

Parent (`build/parent`, no product change): fails immediately on X11 1× and 2× with
[`Public unbound View action exists for Outliner`](R084s-parent-failure.txt).

With the change, all **68/68** cases in the [matrix](R084s-platform-matrix.json) pass
across X11 and Wayland at 1× and 2×: the new test, `keyboard_outliner_input_tests`,
`keyboard_modeling_input_tests`, `shortcut_input_tests`, `entity_info_input_tests`,
`organization_input_tests`, `history_input_tests`, `scene_input_tests`,
`section_input_tests`, `style_input_tests`, `text_input_tests`, `solar_input_tests`,
`reference_image_input_tests`, `annotation_input_tests`, `units_input_tests`, and
`native_accessibility_audit` in its default F6 keyboard mode and `--dialogs-keyboard`
(158 public actions listed). The panel tests cover the changed focus behaviour of the
older actions. Invocations are the non-sanitized `native.yml` lines. Under heavy
host load five Wayland 2× cases first timed out at window exposure or dialog focus
(`style`, `solar`, `reference_image`, `annotation`, `audit`) and passed on rerun without
changing waits; one earlier fixture revision also exposed a Wayland popup limit (above)
and a too-long run (`timeout 60s` in `scripts/test-wayland.sh`), after which the palette
sample was reduced from twelve to six commands.

## Not covered

`tests/accessibility_bridge_test.py` was not run: `libatspi.so.0` is absent from every
local image; it only presses the Info tab and the change does not touch that path. The
Model/Render tab bar and the Assistant side tab are separate widgets and are not
exercised. Tab scroll buttons remain in the Tab order between the bar and the page (one
or two extra presses); this is left unchanged because removing them is a UI decision.
`Home`/`End` on the tab bar are not supported by Qt. Screen-reader announcements, real
window-manager focus policies and the sanitized builds were not run. R084/M9 acceptance
remains open.
