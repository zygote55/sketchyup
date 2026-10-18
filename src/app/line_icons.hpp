#pragma once
#include <QColor>
#include <QIcon>
#include <QPainterPath>
namespace sketchy {
// Original 1.5 px line icons (UX_DESIGN §7): a path in an 18-unit glyph box, stroked in the
// current theme's colours so the icons stay sharp at fractional scales. `checked` (when valid)
// draws the icon's On state, for a checked button; `margin` pads the glyph box on every side,
// so the glyph fills 18 of (18 + 2 × margin) units of the icon.
QIcon strokeIcon(QPainterPath path, QColor ink, QColor disabled, QColor checked = {},
                 qreal margin = 0);
namespace icons {
// The sheet rail (R091.e, R091.g).
QPainterPath selectIcon();
QPainterPath panIcon();
QPainterPath zoomIcon();
QPainterPath modelViewIcon();
QPainterPath moveIcon();
QPainterPath lineIcon();
QPainterPath rectangleIcon();
QPainterPath textIcon();
QPainterPath labelIcon();
QPainterPath dimensionIcon();
// The narrow layout's panel strip and menu button (UX_DESIGN §3.3).
QPainterPath infoIcon();
QPainterPath outlinerIcon();
QPainterPath materialsIcon();
QPainterPath tagsIcon();
QPainterPath historyIcon();
QPainterPath assistantIcon();
QPainterPath morePanelsIcon();
QPainterPath menuIcon();
} // namespace icons
} // namespace sketchy
