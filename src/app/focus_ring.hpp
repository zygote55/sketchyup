#pragma once
namespace sketchy {
// D-032: focus rings at a fractional device pixel ratio (QT_SCALE_FACTOR=1.5).
//
// Focus rings are style-sheet borders (Window::applyTheme and the dialogs' own sheets).
// Qt's style sheet style always paints borders antialiased, so at a fractional ratio a 1 px
// ring whose edge falls in the middle of a device pixel leaves that pixel only part-covered:
// its painted colour is a blend of the ring and the chrome beside it, under 3:1 contrast.
//
// Installed once on the application, this snaps the border of every focusable control to
// whole device pixels when the control paints at a fractional ratio. The border's colour,
// width and position are read from the control painted at 1x (the style sheet's own result,
// so the colour token and placement stay the style sheet's), once per control state (focused
// or not, checked or not, size) until its style, palette, properties or enabled state change.
// After the control's normal paint, each straight edge of the border is filled, without
// antialiasing, over ceil(width x ratio) device pixels from the pixel holding its outer edge;
// a pixel its inner edge leaves part-covered takes the fill just inside. Unfocused borders are
// snapped too, so the change between the two states is measured between whole pixels.
// At integer ratios (1x, 2x) nothing is done: borders are already pixel-aligned there.
void installFractionalFocusRings();
} // namespace sketchy
