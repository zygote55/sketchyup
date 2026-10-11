# R085.q — Numeric keypad decimal key

2026-10-11. Follow-up to [R085.p](R085p-keyboard-layouts.md), which found that the
keypad's decimal key types `,` on a German layout and `.` on a French one,
whatever the application locale. With an English locale on a German keyboard,
keypad `1,5` was two values. With a French locale on a French keyboard, keypad `.`
was rejected.

## Decision

In Measurements and in the numeric entry fields, the keypad decimal key always
inserts the **current locale's decimal separator**, as spreadsheets do. Only the
keypad key changes. The main keyboard's `.` and `,` are unchanged. Qt has no
separate key code for it: it reports `Key_Period` or `Key_Comma` with
`KeypadModifier`, so that is what is matched. (`Qt::Key_Decimal` does not exist.)

## Mechanism

`src/app/keypad_decimal.hpp` is the one shared helper:

- `isKeypadDecimal(QKeyEvent)` and `numericKeyText(QKeyEvent)` give the typed text.
  `Viewport` uses the latter for the shortcut override and the key press that open
  Measurements (`startsMeasurement` in `src/app/viewport.cpp`). The separator is also
  the text sent to Measurements, so a keypad decimal opens it on every layout.
- `KeypadDecimalFilter` is an event filter. It re-delivers a keypad decimal press or
  release to the field as `Key_Comma` (comma locales) or `Key_Period` with the
  separator as its text and without `KeypadModifier`, so it is not seen twice.
- `acceptKeypadDecimal(widget)` installs the filter on a `QLineEdit`, or on the
  embedded line edit of a `QAbstractSpinBox`. Nothing else changes: no styling, no
  validator, no parser.

Fields are listed explicitly, so name, search and path fields still type what the
keyboard produces.

## Fields covered

Measurements; Entity info edit (origin and dimensions); Drawing plane and component
create/axes/origin dialogs; library placement position; hosted glue and placement
options; section normal and distance; solar latitude and longitude; style ground
height, profile width and X-ray opacity; text height, depth, spacing and position;
texture origin; annotation points and text size; reference-image width, height,
opacity, calibration points and known length; material opacity; render environment
strength and rotation, animation transition and hold; STL weld tolerance; and
extension number parameters.

Not covered, because they do not parse a locale decimal: integer spin boxes, the
solar UTC offset, free-text fields. Spin boxes use the locale at the time the keypad
key is pressed, which matches the widget locale unless a widget sets its own.

## Tests

Scripts and exact commands are those of `.github/workflows/native.yml`. Binaries are
the Debug `dev` build. Runs used `scripts/run-container-check.sh` with image
`sketchyup-native-fonts-check:20261008`.

- `numeric_input_tests` (QTest `keyClick` with `KeypadModifier`, for `Key_Period` and
  `Key_Comma`, in de_DE, en_US and fr_FR): from the viewport the keypad key opens
  Measurements with the locale separator; typing `5<sep>1<KP>5` gives `<sep>5<list>1<sep>5`
  and a 0.5 × 1.5 rectangle; `1<KP>5<list>2` is 1.5 × 2; main-keyboard `.` and `,`
  type themselves in the field, `.` still opens Measurements from the viewport, and
  `,` opens it only in a comma locale.
- `entity_info_input_tests`: in de_DE, `2`, keypad `.`, `5m` in a dimension field reads
  `2,5m` and commits 2.5.
- `keyboard_layout_input_tests` (PR #291, physical keys): evdev `KEY_KPDOT` (83) per
  layout. de and fr type `,` for it, us types `.`, but the expected result is the locale
  separator: `,` for de_DE and fr_FR, `.` for C. The fixture opens Measurements with
  it, types `5<list>1<KP>5` and checks a 0.5 × 1.5 rectangle. The fr layout is the
  discriminating case, because its keypad types `.` where the locale needs `,`.
  NumLock is off by default on the nested compositor and the key is then `KP_Delete`;
  the fixture presses NumLock once when the first press types nothing.

| Fixture | X11 1× | X11 2× | Wayland 1× | Wayland 2× |
|---|---|---|---|---|
| `numeric_input_tests` | pass | pass | pass | pass (see note) |
| `units_input_tests` | pass | pass | pass | pass |
| `keyboard_modeling_input_tests` | pass | pass | pass | pass |
| `entity_info_input_tests` | pass | pass | pass | pass |

| `keyboard_layout_input_tests` | us | de | fr |
|---|---|---|---|
| X11 | pass | pass | pass |
| nested Weston | pass | pass | pass |

Note: `numeric_input_tests` Wayland 2× failed once in `qWaitForWindowExposed` ("Numeric
window exposed") before any test step, with other sessions' containers competing for
CPU; the identical rerun passed. R084.r recorded the same Wayland 2× exposure flake.
Layout fixture first runs failed on X11 for all three layouts because a stray
`KP_Delete` (NumLock off) cancelled the pending rectangle; the NumLock probe now runs
in Measurements first. Runs: `r085q-native-1`, `r085q-diag-1`, `r085q-diag2-1`.

Full headless `ctest --preset headless`: 61/61 pass. The whole `dev` preset builds.

## Limits

- Only de and fr were tested with real layouts. Other layouts whose keypad key types a
  different character are covered by construction (the filter ignores the character).
- Input methods and compose do not see the keypad key differently from other keys
  here. They are untested, as in R085.p.
- A physical keyboard on the primary compositor was not tested.
