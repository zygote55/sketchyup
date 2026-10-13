# R084.t — Projection label no longer overlaps the context breadcrumb

2026-10-12. Branch `fix/viewport-label-overlap`, based on `origin/main` at `db68cd14`.

The M9 hardware session (Wayland, scale 2, 701×418 logical px) showed the breadcrumb chip
"Model" drawn over the viewport's "PERSPECTIVE / METERS" text, which read "Model ECTIVE /
METERS". [Before](R084t-before-701x418.png) reproduces it in the native fixture;
[after](R084t-after-701x418.png) is the fixed placement.

## Root cause

`Viewport::paintGL` drew the projection/units label at a fixed `(20, 28)`. The breadcrumb
is a separate `QLabel` child of the viewport at `(16, 12)` (R084.i), so it always covers
the first characters of that text; the chip's opaque background hides them. The two were
never laid out against each other, and a longer nested breadcrumb or larger interface text
made it worse.

## Fix

The viewport now places the label from the breadcrumb's live geometry
(`Viewport::setContextOverlay`, registered by `Window`):

- Breadcrumb visible: the label starts at the breadcrumb's right edge plus 8 px on the same
  28 px baseline.
- If it does not fit before the right 20 px margin, it moves below the context chips,
  left-aligned with the breadcrumb, one font line spacing under their bottom edge. The
  texture summary, which sat 10 px under the label baseline, keeps that offset.
- Breadcrumb hidden: unchanged `(20, 28)`.

The viewport repaints when the chips move, resize, show or hide (an event filter), and on
font, style and palette changes, so resize, entering or leaving nested contexts, text-size
preference and theme changes all re-place the label. Fonts, colors and contrast are
unchanged. The component scope banner (shown under the breadcrumb inside a component) also
counts as a context chip when the label wraps, so the label cannot land on it.

## Test

`viewport_label_overlap_tests` (new; registered in CMake and the four non-sanitized
`native.yml` lists) builds a component containing three nested groups, then for text
100%/200%/100% (changed through the real dialog), light and dark themes, model and
nested-group contexts, and window widths 640, 701 and 1200 asserts that the painted label
rect and the breadcrumb (and banner, when shown) do not intersect and that both stay inside
the viewport; it also asserts the 701×418 case, the exact beside/below geometry, that
both placements occur, and that a hidden breadcrumb keeps the old position.

Mutation check: with the old placement restored the fixture fails immediately
(`100% text, theme 1, model, width 640: label overlaps breadcrumb`).

## Results

Run in `sketchyup-native-fonts-check:20261008` through `scripts/run-container-check.sh`
(1 CPU, 4 GiB; Debug build) with the `native.yml` invocations:

- `viewport_label_overlap_tests`: X11 1×, X11 2×, Wayland 1×, Wayland 2× all pass
  (38 states each: 34 beside, 4 below).
- `theme_input_tests`, `text_size_input_tests`, `group_input_tests`,
  `component_input_tests`, `viewport_tests`: pass on X11 and Wayland at 1× and 2×.
- `visible_focus_input_tests`: passes on X11 1× (42 s) and X11 2× (99 s; the workflow's
  `timeout 90s` is exceeded on this one-CPU host, identically **98 s on the unchanged
  parent**); the two Wayland cases hit the 60 s cap in `scripts/test-wayland.sh` both with
  and without this change (parent: same exits and elapsed times). No pass/fail difference.

Captures at 701×418, Wayland 2×, light theme: [before](R084t-before-701x418.png),
[after](R084t-after-701x418.png), [after, dark](R084t-after-701x418-dark.png), and with a
nested breadcrumb and the component banner [before](R084t-before-701x418-nested.png),
[after](R084t-after-701x418-nested.png). The captures composite the framebuffer and chips
over `QWidget::grab()`, which omits the viewport overlay on its own.

## Not covered

Sanitizer runs were not repeated. The first-line label can still run past the right edge
only if the window is narrower than the label itself, below any tested width. R084/M9
remain open.
