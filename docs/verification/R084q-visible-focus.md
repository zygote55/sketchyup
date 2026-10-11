# R084.q — Visible keyboard focus

2026-10-10. Closes the "visible focus" part of Core scope N05 for the Qt widget
interface. Complete accessibility acceptance remains open (R084, M9).

Before this layer only `QLineEdit`, `QTreeWidget`, `QListWidget`, `QToolBar` and
`QPushButton` had a focus rule in the application style sheet
(`src/app/window.cpp`, `Window::applyTheme`). Every other focusable control drew
whatever the platform style gives it, which under the themed palette is often
nothing. The style sheet is the only `setStyleSheet` call in `src`, so dialogs and
panels parented to the main window inherit it; there is no competing sheet.

## Inventory

Classes instantiated in `src/app` (grep of `new Q…`), and what they now do on focus.
"Ring" is the 1 px `$accent` border used by the existing rules.

| Class | Used for | Before | Now |
| --- | --- | --- | --- |
| `QLineEdit` (50) | measurements, search, panel and dialog fields | ring | ring (unchanged) |
| `QPushButton` (47, plus 26 button boxes) | panels, dialogs | ring | ring (unchanged) |
| `QListWidget` (11), `QTreeWidget` (4) | Outliner, results, history | ring, but border 0 → 1 px on focus | ring; transparent 1 px base border, no shift |
| `QToolBar` (1) | modeling tool rail | ring, border 0 → 1 px | ring; transparent base border, padding compensated |
| `QToolButton` | rail tools (Qt gives them `NoFocus`), tab scrollers | none | ring; checked and hover backgrounds kept |
| `QComboBox` (40) | selectors in panels and dialogs | none | boxed like `QLineEdit`, ring |
| `QSpinBox` (10), `QDoubleSpinBox` (8), `QDateEdit`, `QTimeEdit` | numbers, sun date and time | none | boxed like `QLineEdit`, ring |
| `QCheckBox` (23) | options | none | transparent base border on the widget, ring around box and label |
| `QPlainTextEdit` (20) | composer, logs, details | none | boxed like `QLineEdit`, ring |
| `QTabBar` (tab widgets 3, scene tabs 1) | model panels, scenes | none | ring on the current tab |
| `QScrollArea` (6) | panel and dialog scrolling | none | ring |
| `QKeySequenceEdit` (1) | shortcut editor | inner edit only | inner `QLineEdit` ring (unchanged rule) |
| `QLabel` with `StrongFocus` (2) | editing-context breadcrumb, component scope banner | none | ring, 2 px × 4 px padding so the link never touches it |
| `AssistantPanel` container | F6 region stop | none | ring (`WA_StyledBackground` so Qt draws the border) |
| `Viewport` (`QOpenGLWidget`) | 3D canvas | own painted 2 px accent ring (`viewport.cpp`) | unchanged; measured |
| `QRadioButton`, `QSlider`, `QTextEdit` | not instantiated anywhere | – | rule added defensively; not exercised |

`QProgressBar`, plain `QLabel` and `QDialogButtonBox` itself are not focus stops.
The tool rail's individual buttons are `NoFocus` (Qt's `QToolBar` default); the rail
itself is the Tab stop and already had a ring. Making individual tools keyboard
stops would be a separate behaviour change and is not part of this layer. The
`QToolButton` rule is proven on a rail button temporarily given Tab focus by the
test, for both a checked and an unchecked tool.

Classes that only appear in dialogs the test cannot open without a file chooser
(DXF/STL/OBJ/units/library/render dialogs) are the same classes covered in the
dialogs the test does open, under the same single style sheet.

## Style changes

* `QToolBar`, `QToolButton`, `QCheckBox`/`QRadioButton`, `QListWidget`,
  `QTreeWidget`, `QScrollArea`, `QSlider`, the assistant container and the two
  context labels get `border:1px solid transparent`; focus only changes colour.
  The tool rail's padding is reduced by exactly the new border width
  (toolbar `10px 5px` → border + `9px 5px 9px 4px`, buttons `12px 5px` → border +
  `11px 4px`) because the rail has a fixed width; without this, "Follow Me" and
  "Tape Measure" elide. Outer geometry is unchanged.
* `QComboBox`, `QAbstractSpinBox`, `QPlainTextEdit` and `QTextEdit` use the
  `QLineEdit` look (input background, `$border` 1 px, 4 px radius, 3 px × 5 px
  padding) so the focus border is the same element everywhere.
* Tabs: `QTabBar::tab` is styled (`$selected` for the current tab, `$hover` on
  hover) and `QTabBar::tab:selected:focus` carries the ring. A box on `QTabBar`
  itself is not drawn by Qt, and `QTabBar::tab:focus` never matches.
  `QTabWidget::pane` uses `$border`; its native frame was a fixed grey that sat
  against the ring.
* **`resources/controls/`** holds eight tiny PNG arrows (up/down × light/dark ×
  1×/2×), compiled into `sketchyup_desktop` with `qt_add_resources`, and used by
  `QAbstractSpinBox::up-arrow`, `::down-arrow` and `QComboBox::down-arrow`.
  Qt drops the native up/down glyphs of a spin box as soon as it has a styled
  border and no image for them (this was observed in the first rendering), so a
  boxed spin box would otherwise show empty buttons. The images use the theme
  ink colour and the `@2x` variants keep them crisp at 2×. They are not used for
  anything else.
* No `$focus` token was needed (below).

## Contrast

Computed from `themeColors()` (`src/app/theme.hpp`), WCAG relative luminance.

| Accent vs | Light (`#547858`) | Dark (`#9ec99d`) |
| --- | --- | --- |
| `surface` | 4.648 | 8.052 |
| `input` | 4.995 | 8.985 |
| `selected` (checked tool, selected tab) | 3.926 | 4.195 |
| `hover` | 4.118 | 5.636 |
| `border` (unfocused outline) | 3.260 | 4.073 |

All pairs are at least 3:1 in both themes, so the existing accent is kept and no
`$focus` token was added.

## Test

`tests/visible_focus_input_tests.cpp` (`visible_focus_input_tests`, registered
beside `theme_input_tests` in CMake and every native, X11 and sanitized Wayland
list in `.github/workflows/native.yml`). In the real main window (every model-panel
tab, tool rail) and the real Drawing plane, Keyboard shortcuts, Commands, Sun
and shadows, New material and Assistant preferences dialogs, in light and dark at
100% and 200% interface text, for the first two Tab-focusable controls of each
class per context at 100% text and the first at 200% (every class in
every context):

1. focus another control, grab the control and a 4 px margin;
2. `setFocus(Qt::TabFocusReason)`, process events, grab again;
3. assert the ring pixels (middle half of each edge) differ on all four edges;
4. assert the control's size and size hint are unchanged (no layout shift);
5. assert the ring colour has at least 3:1 against the outside background (the
   dominant colour 2–4 px beyond the boundary, ignoring pixels beyond the window
   and the 3D canvas) and against the inside background (1 px past the border, top
   and left edges, where Qt does not place arrows or scroll bars);
6. assert every required class was exercised.

The 3D canvas is measured from its own framebuffer; its outside neighbour is the
window chrome and its ratio is reported but not asserted (see findings). Tab bars
are measured around the current tab. Controls clipped by a scroll area are
skipped and counted.

## Results

[Platform matrix](R084q-platform-matrix.json), merged with main at `c04aea04`
(#284), container `sketchyup-native-fonts-check:20261008` (Qt 6.11.2, Xvfb,
headless Weston, software GL), host-built binaries:

* `visible_focus_input_tests`: **4/4**, X11 and Wayland, 1× and 2×
  (14/15/25/32 s, inside the 60 s Wayland wrapper limit). 421 controls measured,
  0 failures, 14 clipped skips, the same
  numbers in all four cases. Minimum ring contrast **4.648:1** outside and
  **3.926:1** inside (the checked tool button and selected tab, whose inside is
  `$selected`); [per class, theme and size](R084q-focus-contrast.json).
* Existing suites, the same four cases each, **68/68**: `theme_input_tests`
  (contrast), `text_size_input_tests`, `interaction_tests`, `style_input_tests`,
  `keyboard_outliner_input_tests`, `keyboard_modeling_input_tests`,
  `shortcut_input_tests`, `preference_input_tests`, `solar_input_tests`,
  `scene_input_tests`, `organization_input_tests`, `entity_info_input_tests`,
  `material_input_tests`, `annotation_input_tests`, `assistant_panel_tests`,
  `section_input_tests` and `native_accessibility_audit --dialogs-keyboard`.

The full headless `ctest --preset headless` passes **59/59**; it does not compile the
desktop sources this layer changes.

Per class, minimum ring contrast against outside / inside, all themes and sizes:

| Class | Controls | Outside | Inside |
| --- | --- | --- | --- |
| `QLineEdit` | 75 | 4.65 | 4.99 |
| `QPushButton` | 89 | 4.65 | 4.65 |
| `QComboBox` | 26 | 4.65 | 4.99 |
| `QSpinBox`, `QDoubleSpinBox`, `QDateEdit`, `QTimeEdit` | 25 | 4.65 | 4.99 |
| `QCheckBox` | 8 | 4.65 | 4.65 |
| `QPlainTextEdit`, `QKeySequenceEdit` | 5 | 4.65 | 4.99 |
| `QListWidget`, `QTreeWidget` | 15 | 4.65 | 3.93 |
| `QTabBar` (current tab) | 30 | 4.65 | 3.93 |
| `QToolButton` (rail, checked and unchecked) | 8 | 4.65 | 3.93 |
| `QToolBar` rail | 30 | 4.65 | 4.65 |
| `QScrollArea`, context `QLabel`, `AssistantPanel` | 80 | 4.65 | 4.65 |
| `Viewport` (own ring) | 30 | 2.997 (not asserted) | 4.40 |

Screenshots of one dialog, combo box focused (left) and a checked tool button
focused (right): [light](R084q-focus-light.png), [dark](R084q-focus-dark.png).

## Findings that remain

* **Canvas adjacency in the dark theme.** The tool rail, the breadcrumb and the
  assistant container touch the 3D canvas, whose default document colour is light
  (`#f0f1ec`). The dark accent against it is 1.64:1, so on that side the ring only
  contrasts with the dark interface inside it (8.05:1). The canvas is document
  content with a user-chosen colour, so the test excludes it from the outside
  background. A mid-tone `$focus` could not satisfy this and the checked-tool
  case (3.93:1 against `$selected`) together.
* The 3D canvas ring is painted from document-derived colours; against the dark
  window chrome it is 2.997:1, just under 3:1.
* Native scroll bars are not themed: in the dark theme they are light grey and
  sit next to scroll-area rings. Tab-bar scroll buttons are still styled by the
  generic `QToolButton` rule (padding), which predates this layer.
* 14 controls per run are wider than their scroll area at 200% text and are
  skipped as clipped; this is a layout limitation, not a focus result.

This verifies painted focus indicators in the Qt widget interface at two themes,
two interface text sizes, two display backends and two scales. It does not claim
all dialogs, high-contrast modes, other Qt styles or assistive-technology
operation. R084 and M9 remain open. All tests use private settings and a failing
synthetic credential helper.
