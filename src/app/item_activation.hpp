#pragma once
class QAbstractItemView;
namespace sketchy {
// Qt activates the current row of an item view on Return and Enter on Linux, but not on macOS,
// where a view takes Command+O instead. SketchyUp's lists act on Enter (UX_DESIGN's keyboard
// paths), so on macOS the view also activates its current row on Return and Enter as Qt does on
// Linux: the view emits activated(), and the key goes on to the view and its parents. Elsewhere
// this does nothing.
void activateOnReturn(QAbstractItemView *view);
} // namespace sketchy
