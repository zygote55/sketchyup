#include "app/line_icons.hpp"
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <tuple>
namespace sketchy {
namespace {
// Drawn in an 18-unit box (plus the margin) in the current ink, so the icons stay sharp at
// fractional scales and follow the theme.
class LineIconEngine final : public QIconEngine {
  public:
    LineIconEngine(QPainterPath path, QColor ink, QColor disabled, QColor checked, qreal margin)
        : path_(std::move(path)), ink_(std::move(ink)), disabled_(std::move(disabled)),
          checked_(std::move(checked)), margin_(margin) {}
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode,
               QIcon::State state) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        const qreal box = 18 + 2 * margin_;
        painter->scale(rect.width() / box, rect.height() / box);
        painter->translate(margin_, margin_);
        const auto &ink = mode == QIcon::Disabled                    ? disabled_
                          : state == QIcon::On && checked_.isValid() ? checked_
                                                                     : ink_;
        painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path_);
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        return scaledPixmap(size, mode, state, 1);
    }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state,
                         qreal scale) override {
        QPixmap result(size * scale);
        result.fill(Qt::transparent);
        {
            QPainter painter(&result);
            paint(&painter, QRect(QPoint(), size * scale), mode, state);
        }
        result.setDevicePixelRatio(scale);
        return result;
    }
    QIconEngine *clone() const override { return new LineIconEngine(*this); }

  private:
    QPainterPath path_;
    QColor ink_, disabled_, checked_;
    qreal margin_;
};
} // namespace
QIcon strokeIcon(QPainterPath path, QColor ink, QColor disabled, QColor checked, qreal margin) {
    return QIcon(new LineIconEngine(std::move(path), std::move(ink), std::move(disabled),
                                    std::move(checked), margin));
}
namespace icons {
QPainterPath selectIcon() {
    QPainterPath path;
    path.moveTo(4, 2);
    for (const auto &point : {QPointF(13, 8), QPointF(9, 9), QPointF(11, 14), QPointF(9, 15),
                              QPointF(7, 10), QPointF(4, 13)})
        path.lineTo(point);
    path.closeSubpath();
    return path;
}
QPainterPath panIcon() {
    QPainterPath path;
    path.moveTo(6, 9);
    path.lineTo(6, 4);
    path.arcTo(QRectF(6, 3, 2, 2), 180, -180);
    path.lineTo(8, 8);
    path.moveTo(8, 8);
    path.lineTo(8, 3);
    path.arcTo(QRectF(8, 2, 2, 2), 180, -180);
    path.lineTo(10, 8);
    path.moveTo(10, 8);
    path.lineTo(10, 4);
    path.arcTo(QRectF(10, 3, 2, 2), 180, -180);
    path.lineTo(12, 10);
    path.cubicTo(12, 13, 10, 15, 7, 15);
    path.cubicTo(4, 15, 3, 13, 2, 11);
    path.lineTo(1, 9);
    path.cubicTo(.7, 8, 2.2, 7, 3, 8);
    path.lineTo(4, 10);
    return path;
}
QPainterPath zoomIcon() {
    QPainterPath path;
    path.addEllipse(QPointF(8, 8), 5, 5);
    path.moveTo(12, 12);
    path.lineTo(16, 16);
    return path;
}
QPainterPath modelViewIcon() {
    QPainterPath path;
    path.addRoundedRect(QRectF(2, 3, 14, 12), 1, 1);
    path.moveTo(5, 12);
    path.lineTo(8, 8);
    path.lineTo(10, 10);
    path.lineTo(13, 6);
    return path;
}
QPainterPath moveIcon() {
    QPainterPath path;
    path.moveTo(9, 2);
    path.lineTo(9, 16);
    path.moveTo(2, 9);
    path.lineTo(16, 9);
    for (const auto &[tip, a, b] : {std::tuple{QPointF(9, 2), QPointF(7, 4), QPointF(11, 4)},
                                    std::tuple{QPointF(9, 16), QPointF(7, 14), QPointF(11, 14)},
                                    std::tuple{QPointF(2, 9), QPointF(4, 7), QPointF(4, 11)},
                                    std::tuple{QPointF(16, 9), QPointF(14, 7), QPointF(14, 11)}}) {
        path.moveTo(a);
        path.lineTo(tip);
        path.lineTo(b);
    }
    return path;
}
// The page-item tools (R091.g), from the R091 design's rail.
QPainterPath lineIcon() {
    QPainterPath path;
    path.moveTo(3, 15);
    path.lineTo(15, 3);
    return path;
}
QPainterPath rectangleIcon() {
    QPainterPath path;
    path.addRect(QRectF(3, 5, 12, 8));
    return path;
}
QPainterPath textIcon() {
    QPainterPath path;
    path.moveTo(4, 4);
    path.lineTo(14, 4);
    path.moveTo(9, 4);
    path.lineTo(9, 15);
    return path;
}
QPainterPath labelIcon() {
    QPainterPath path;
    path.moveTo(3, 15);
    path.lineTo(9, 9);
    path.lineTo(15, 9);
    path.addEllipse(QPointF(3, 15), 1.2, 1.2);
    return path;
}
QPainterPath dimensionIcon() {
    QPainterPath path;
    path.moveTo(2, 9);
    path.lineTo(16, 9);
    path.moveTo(2, 6);
    path.lineTo(2, 12);
    path.moveTo(16, 6);
    path.lineTo(16, 12);
    path.moveTo(5, 7);
    path.lineTo(2, 9);
    path.lineTo(5, 11);
    path.moveTo(13, 7);
    path.lineTo(16, 9);
    path.lineTo(13, 11);
    return path;
}
// The narrow layout's panel strip (UX_DESIGN §3.3), after the narrow-tile mockup: one glyph per
// Model panel tab, the Assistant and More panels. They sit half a unit off the grid, so at 1x
// and 2x their straight 1.5-unit strokes cover whole pixels and keep their full colour.
QPainterPath infoIcon() {
    QPainterPath path;
    path.addEllipse(QPointF(9, 9), 8, 8);
    path.addEllipse(QPointF(9, 5.5), .4, .4);
    path.moveTo(9, 8.5);
    path.lineTo(9, 13);
    return path.translated(.5, .5);
}
QPainterPath outlinerIcon() {
    QPainterPath path;
    // Three rows, each a 2-unit square bullet and a line; rows two and three are indented.
    for (const auto &[x, y] : {std::pair{2., 4.}, std::pair{6., 9.}, std::pair{6., 14.}}) {
        path.addRect(QRectF(x, y - 1, 2, 2));
        path.moveTo(x + 5, y);
        path.lineTo(16, y);
    }
    return path.translated(.5, .5);
}
QPainterPath materialsIcon() {
    QPainterPath path;
    for (const auto &[x, y] :
         {std::pair{2., 2.}, std::pair{10., 2.}, std::pair{2., 10.}, std::pair{10., 10.}})
        path.addRoundedRect(QRectF(x, y, 6, 6), 1.5, 1.5);
    return path.translated(.5, .5);
}
QPainterPath tagsIcon() {
    QPainterPath path;
    // A tag whose square corner points up and left, with its string hole.
    path.moveTo(2, 2);
    path.lineTo(9.5, 2);
    path.lineTo(16.5, 9);
    path.lineTo(9, 16.5);
    path.lineTo(2, 9.5);
    path.closeSubpath();
    path.addEllipse(QPointF(5.5, 5.5), 1, 1);
    return path.translated(.5, .5);
}
QPainterPath historyIcon() {
    QPainterPath path;
    path.addEllipse(QPointF(9, 9), 8, 8);
    // Hands at twelve and three.
    path.moveTo(9, 4);
    path.lineTo(9, 9);
    path.lineTo(13, 9);
    return path.translated(.5, .5);
}
QPainterPath assistantIcon() {
    QPainterPath path;
    // A four-point sparkle.
    path.moveTo(9, 1.5);
    path.quadTo(10, 8, 16.5, 9);
    path.quadTo(10, 10, 9, 16.5);
    path.quadTo(8, 10, 1.5, 9);
    path.quadTo(8, 8, 9, 1.5);
    path.closeSubpath();
    return path.translated(.5, .5);
}
QPainterPath morePanelsIcon() {
    QPainterPath path;
    for (const qreal x : {3., 9., 15.})
        path.addEllipse(QPointF(x, 9), .75, .75);
    return path.translated(.5, .5);
}
QPainterPath menuIcon() {
    QPainterPath path;
    for (const qreal y : {4., 9., 14.}) {
        path.moveTo(2, y);
        path.lineTo(16, y);
    }
    return path.translated(.5, .5);
}
} // namespace icons
} // namespace sketchy
