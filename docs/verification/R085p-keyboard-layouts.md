# R085.p — Keyboard layouts through physical key input

2026-10-10. Closes the N03 keyboard-layout gap from the
[Core parity audit](R088d-core-parity-gaps.md). R085.j–m and the local run names
r085n/o were already used as attempt labels, so this record takes R085.p.

## How shortcuts and numeric entry depend on the layout

- **Shortcuts.** Every public command is a `QAction`/`QShortcut` with a
  `QKeySequence`. Qt matches these by the key symbol that the active XKB layout
  produces, not by the physical position. No application code reads
  `nativeScanCode` or `nativeVirtualKey`. "L" fires on the `l` character wherever
  the layout puts it. `Shift+Z` and `Ctrl+Z` follow `z`, which is the physical Y key on
  QWERTZ and the W key on AZERTY. On AZERTY the `1`/`2`/`3` view shortcuts need
  Shift, because Shift is what types a digit there. The unshifted key types `é` and does not
  trigger them.
- **Viewport keys.** `Viewport` uses `key()` only for keys that do not depend on
  the layout: Tab, Return/Enter, Escape, Control, Shift and the arrow locks.
  `Ctrl+A` uses the key symbol, as shortcuts do. Measurements opens on the typed
  `text()`, so it opens on whatever character the layout produces. The AltGr
  brackets on QWERTZ and AZERTY are not reported as Alt or Ctrl, so `[` still
  opens it.
- **Measurements parsing.** The parser reads text only. The decimal and list
  separators come from the locale, not from the layout. `'` and `"` can be typed on
  all three tested layouts: Shift+# and Shift+2 on QWERTZ, and the unshifted 4 and
  3 keys on AZERTY.
- **Shortcut editor.** `QKeySequenceEdit` records the event's key symbol and
  modifiers. Bindings are stored as `PortableText`, so a saved binding follows its
  character across layouts. Recording real non-US keys in the editor is not
  exercised here.

## Mechanism

`QTest::keyClick` builds Qt key events directly, so it never passes through a
keymap. The new `keyboard_layout_input_tests` instead sends **physical keycodes**
(evdev + 8) through XTest (`libxcb-xtest`, part of libxcb). The fixture's own
tables give the key for each character on us, de and fr, taken from the printed
keycaps. They are independent of the keymap that the application receives. The
fixture first checks that the X server's `_XKB_RULES_NAMES` layout is the expected one.
[`scripts/test-keyboard-layout.sh`](../../scripts/test-keyboard-layout.sh) runs it
on two paths:

- **X11 (`xcb`).** `xvfb-run -s '… -noreset'`, then
  `setxkbmap -model pc105 -layout L`, then a Qt xcb client.
- **Wayland.** The same Xvfb runs Weston 15's x11 backend. Weston builds its
  keymap from the X server's XKB rule names and forwards the injected keys to a
  Qt Wayland client as `wl_keyboard` events. Headless Weston (`test-wayland.sh`) has
  no input source, so it cannot carry real key input. The image has no `wtype`,
  and Weston does not offer the `zwp_virtual_keyboard` protocol that `wtype` needs.

`-noreset` is required. Without it, Xvfb resets to its default US keymap as soon
as `setxkbmap`, its only client, disconnects. Run r085p-layout-1 shows this:
`setxkbmap` reports success, then de and fr fail the server-layout check. Any
`setxkbmap` inside a plain `xvfb-run` session is silently undone in the same way.

Each layout run does the following:

1. Types `l r c m q a z` and requires Line, Rectangle, Circle, Move, Rotate,
   Center arc and Zoom. On AZERTY, `a` is the physical Q key and must select Center
   arc, not Rotate.
2. Types the unbound `y` and `w`, which sit on the US "Z" key on QWERTZ and
   AZERTY. Select must stay active.
3. Requires `Shift+Z` to fit the view and a typed `2` to select Top.
4. Types `[0,0,0]` and then `2'6",1.5` from the viewport. This needs AltGr
   brackets, Shift digits and both foot/inch marks. The result must be a 0.762 m ×
   1.5 m rectangle.
5. Draws a line from `[10,0,0]` with the Up-arrow blue lock and length 2. Draws
   another line to the relative point `<0,1,0>`.
6. Undoes with `Ctrl+Z` and redoes with `Ctrl+Shift+Z`.
7. Switches to the locale that usually goes with the layout (de_DE, fr_FR or C)
   and types `[30;0;0]` and `,5;2` (C locale: `.5,2`). The result must be a 0.5 × 2
   rectangle.

Every typed prefix must appear exactly in Measurements before the next key is sent.

## Matrix

[Results, versions and binary hashes](R085p-layout-matrix.json). All runs used
`scripts/run-container-check.sh` with image `sketchyup-native-fonts-check:20261008`:
Qt 6.11.2, Weston 15.0.1, Xvfb 21.1.24, xkeyboard-config 2.48 and libxkbcommon
1.13.2. Final run r085p-matrix-1: **54/54 pass**.

| Fixture | us X11 | us Wayland | de X11 | de Wayland | fr X11 | fr Wayland |
|---|---|---|---|---|---|---|
| `keyboard_layout_input_tests` (physical keys) | pass | pass | pass | pass | pass | pass |
| `shortcut_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `keyboard_modeling_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `keyboard_outliner_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `units_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `numeric_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `tool_lifecycle_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `constraint_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |
| `inference_input_tests` (QTest control) | pass | pass | pass | pass | pass | pass |

The QTest rows ran X11 controls after `setxkbmap` under `-noreset`, and Wayland
controls through `test-wayland.sh` with `XKB_DEFAULT_LAYOUT` and
`XKB_DEFAULT_MODEL` inherited by Weston. The script needed no change. These rows
show that the layout environment does not disturb the existing fixtures. They are
**not** layout evidence, because their events bypass the keymap. The headless
compositor's keymap was not independently inspected.

## Defect found and fixed

**A leading locale decimal separator was dropped.** Measurements opened only on
`0123456789.+-[<xX/`. With a comma-decimal locale, typing `,5;2` from the
viewport discarded the comma. The `5` then opened Measurements, so the result was a
5 × 2 rectangle instead of 0.5 × 2. Run r085p-layout-3-prefix reproduced this on de
and fr on both backends; us passed with `.5,2`. The viewport now also opens
Measurements on `QLocale().decimalPoint()`, for both the key press and the
shortcut override (`startsMeasurement` in `src/app/viewport.cpp`). Regression
assertions are in the physical-key fixture and in `numeric_input_tests`
(QTest, German locale).

No shortcut or numeric key was matched by keycode. `'` and `"` can be typed on all
three layouts.

## CI

`.github/workflows/native.yml` gains one step, "Keyboard layouts through physical
key input". It runs the fixture for us/de/fr on X11 and on nested Weston. It needs
no new packages: `xorg-setxkbmap` is a dependency of `xorg-server-common`,
`xcb-xtest` comes with libxcb, and Weston ships the x11 backend.
`CMakeLists.txt` links the fixture with `pkg_check_modules(XcbTest xcb xcb-xtest)`.

## Remaining limits

- **Numeric keypad decimal.** The German layout's keypad separator types `,`
  (`kpdl(comma)`); French types `.`. With an English/C locale on QWERTZ, keypad
  `1,5` is read as two values. With fr_FR on AZERTY, keypad `.` is rejected.
  Whether the keypad decimal should map to the locale separator is an open product
  decision, so nothing was changed.
- Input methods/IME, compose, dead keys and layout variants (nodeadkeys, Dvorak,
  Neo) are not tested. Non-Latin layouts (Qt's Latin fallback for Ctrl shortcuts)
  and runtime group switching are not tested either.
- Recording real non-US keys in the shortcut editor is not tested.
- Weston nested in Xvfb is a real compositor key path, but it is not the primary
  Hyprland compositor or a physical keyboard. Physical-keyboard and Hyprland layout
  checks remain open with the other R085 physical gates.
- Only de and fr were tested besides the US control. Other layouts are not
  certified.
