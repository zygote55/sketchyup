#include "app/focus_ring.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QHash>
#include <QImage>
#include <QList>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QTabBar>
#include <QTransform>
#include <array>
#include <cmath>
#include <utility>
namespace sketchy {
namespace {
bool fractional(qreal ratio) { return std::abs(ratio - std::round(ratio)) > 1e-6; }
// The rectangle the ring surrounds: a tab bar's ring belongs to its current tab.
QRect ringArea(const QWidget *widget) {
    if (auto *tabs = qobject_cast<const QTabBar *>(widget))
        return tabs->currentIndex() < 0 ? QRect() : tabs->tabRect(tabs->currentIndex());
    return widget->rect();
}
// The control state the style sheet's ring depends on besides focus.
struct Key {
    QRect area;
    bool focused{}, checked{}, down{};
    bool operator==(const Key &) const = default;
};
Key keyOf(const QWidget *widget) {
    Key key{ringArea(widget), widget->hasFocus()};
    if (auto *button = qobject_cast<const QAbstractButton *>(widget))
        key.checked = button->isChecked(), key.down = button->isDown();
    return key;
}
// One straight edge of the ring, in logical pixels of the 1x paint: `depth` from the area's
// boundary (a style-sheet margin), `width` thick, from `from` to `to` (exclusive) along it;
// the rounded corners lie outside that span and are left as the style sheet painted them.
struct Edge {
    int depth{}, width{}, from{}, to{};
    QColor inside; // The fill just inside the ring along its span, when uniform and opaque.
    int haloWidth{}; // D-023: a halo of this width lies directly outside the ring (at depth).
};
struct Ring {
    Key key;
    QColor color, halo;          // The halo's colour is only meaningful when the edges have a halo.
    std::array<Edge, 4> edges{}; // Top, right, bottom, left.
    bool valid{};
};
// Reads the ring from the control painted at 1x into a transparent image: on each edge, at
// its middle, the first opaque run of at most 3 px within 8 px of the boundary (or 1 px of a
// fill that reaches the boundary), in one colour along a straight span through the middle.
// All four edges must agree on colour and width; anything else (a control without a border,
// content) is not a ring and is left alone.
// D-023: a control that borders the 3D canvas (`haloed`) has a two-tone ring, a ring inside a
// 1 px halo of the control's own background; each is a straight run of at most 3 px in one
// colour. When both are found, `halo` and each edge's `haloWidth` describe the halo and `depth`
// is where the ring itself starts; the unfocused state has neither and is read as any other.
Ring readRing(const QImage &image, const Key &key, bool haloed) {
    Ring ring;
    ring.key = key;
    const auto area = key.area;
    if (area.width() < 8 || area.height() < 8)
        return ring;
    for (int edge = 0; edge < 4; ++edge) {
        const bool horizontal = edge == 0 || edge == 2;
        const int span = horizontal ? area.width() : area.height();
        const int across = horizontal ? area.height() : area.width();
        auto pixel = [&](int depth, int along) {
            QPoint at;
            if (edge == 0)
                at = {area.left() + along, area.top() + depth};
            else if (edge == 1)
                at = {area.right() - depth, area.top() + along};
            else if (edge == 2)
                at = {area.left() + along, area.bottom() - depth};
            else
                at = {area.left() + depth, area.top() + along};
            return image.rect().contains(at) ? image.pixel(at) : QRgb(0);
        };
        const int mid = span / 2, deepest = std::min(8, across / 2 - 1);
        // The runs of one colour from the boundary inwards, from the first opaque pixel.
        int depth = 0;
        while (depth <= deepest && qAlpha(pixel(depth, mid)) != 255)
            ++depth;
        if (depth > deepest)
            return ring;
        // Its straight span at `at`: through the middle, in one colour, and at least a third of
        // the edge.
        int from{}, to{};
        const auto straight = [&](int at) {
            const auto color = pixel(at, mid);
            from = mid, to = mid + 1;
            while (from > 0 && pixel(at, from - 1) == color)
                --from;
            while (to < span && pixel(at, to) == color)
                ++to;
            return 3 * (to - from) >= span;
        };
        // The run of one colour at `at`, along the middle, in px.
        const auto runAt = [&](int at) {
            int run = 1;
            while (at + run < across && pixel(at + run, mid) == pixel(at, mid))
                ++run;
            return run;
        };
        int haloWidth = 0;
        QColor halo;
        if (haloed) {
            const int run = runAt(depth), next = depth + run;
            if (run <= 3 && straight(depth) && next <= deepest && qAlpha(pixel(next, mid)) == 255 &&
                pixel(next, mid) != pixel(depth, mid) && runAt(next) <= 3 && straight(next)) {
                haloWidth = run;
                halo = QColor::fromRgba(pixel(depth, mid));
                depth = next;
            }
        }
        // The ring is the first straight run of at most 3 px within reach (past a margin the
        // control paints in its own background, such as a page tab's); failing that, a border
        // in the fill's own colour (an accent-filled control) is its first run's boundary pixel.
        int width = 0;
        bool filled = true;
        for (int at = depth; at <= deepest;) {
            const int run = runAt(at);
            if (run <= 3 && qAlpha(pixel(at, mid)) == 255 && straight(at)) {
                depth = at, width = run, filled = false;
                break;
            }
            at += run;
        }
        if (filled) {
            width = 1;
            if (!straight(depth))
                return ring;
        }
        const auto color = pixel(depth, mid);
        if (edge == 0)
            ring.color = QColor::fromRgba(color), ring.halo = halo;
        else if (QColor::fromRgba(color) != ring.color || width != ring.edges[0].width ||
                 haloWidth != ring.edges[0].haloWidth || halo != ring.halo)
            return ring;
        QColor inside;
        if (!filled) {
            const auto fill = pixel(depth + width, mid);
            bool uniform = qAlpha(fill) == 255;
            for (int along = from; uniform && along < to; ++along)
                uniform = pixel(depth + width, along) == fill;
            if (uniform)
                inside = QColor::fromRgba(fill);
        }
        ring.edges[edge] = {depth, width, from, to, inside, haloWidth};
    }
    ring.valid = true;
    return ring;
}
class FractionalFocusRings final : public QObject {
  public:
    explicit FractionalFocusRings(QObject *parent) : QObject(parent) {
        qApp->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (!object->isWidgetType())
            return false;
        auto *widget = static_cast<QWidget *>(object);
        switch (event->type()) {
        case QEvent::Paint:
            return paint(widget, event);
        // Focus changes the ring: read it before the control's next paint, unless known.
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            if (candidate(widget) && !find(widget))
                schedule(widget);
            return false;
        // These may change any of its states: each is read again when next painted.
        case QEvent::StyleChange:
        case QEvent::PaletteChange:
        case QEvent::DynamicPropertyChange:
        case QEvent::EnabledChange:
            if (auto found = rings_.find(widget); found != rings_.end())
                found->clear();
            return false;
        default:
            return false;
        }
    }

  private:
    // A focusable control painting at a fractional ratio. Its unfocused border is snapped like
    // its focused one, so the change between the two states is measured between whole pixels
    // (an accent-filled control's indicator is that change).
    static bool candidate(const QWidget *widget) {
        // Only a shown control: rendering a hidden one would lay out its hidden ancestors.
        // A control painting all its own pixels (a canvas) draws its own ring, not the style
        // sheet's; GL surfaces paint theirs in their own framebuffer.
        return widget->isVisible() && !widget->testAttribute(Qt::WA_OpaquePaintEvent) &&
               (widget->focusPolicy() & Qt::TabFocus || widget == QApplication::focusWidget()) &&
               !qobject_cast<const QOpenGLWidget *>(widget) &&
               fractional(widget->devicePixelRatioF());
    }
    // The ring read for the control's current state, if any.
    const Ring *find(const QWidget *widget) const {
        const auto found = rings_.constFind(widget);
        if (found == rings_.cend())
            return nullptr;
        const auto key = keyOf(widget);
        for (const auto &ring : *found)
            if (ring.key == key)
                return &ring;
        return nullptr;
    }
    // Reads the ring after the current event (the style sheet is repolished by then).
    void schedule(QWidget *widget) {
        if (!rings_.contains(widget)) {
            rings_.insert(widget, {});
            connect(widget, &QObject::destroyed, this, [this, widget] {
                rings_.remove(widget);
                pending_.remove(widget);
            });
        }
        if (pending_.isEmpty())
            QMetaObject::invokeMethod(this, &FractionalFocusRings::flush, Qt::QueuedConnection);
        pending_.insert(widget);
    }
    void flush() {
        const auto widgets = std::exchange(pending_, {});
        for (auto *widget : widgets)
            if (candidate(widget) && !find(widget))
                probe(widget);
    }
    // Paints the control at 1x, as the style sheet draws it, and reads its ring.
    void probe(QWidget *widget) {
        const auto key = keyOf(widget);
        Ring ring;
        ring.key = key;
        if (key.area.isValid()) {
            QImage image(widget->size(), QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(1);
            image.fill(Qt::transparent);
            probing_ = widget;
            widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags());
            probing_ = nullptr;
            ring = readRing(image, key, widget->property("focusHalo").toBool());
        }
        // A few states per control (focused and not, checked and not) are kept.
        auto &rings = rings_[widget];
        if (rings.size() >= 4)
            rings.removeFirst();
        rings.append(ring);
        if (ring.valid)
            widget->update(key.area);
    }
    bool paint(QWidget *widget, QEvent *event) {
        if (widget == probing_ || widget == forwarding_ || !candidate(widget))
            return false;
        const auto *found = find(widget);
        if (!found) {
            schedule(widget);
            return false;
        }
        if (!found->valid)
            return false;
        const auto ring = *found;
        // The control's own paint (its style sheet, other filters), then the ring on top.
        forwarding_ = widget;
        QCoreApplication::sendEvent(widget, event);
        forwarding_ = nullptr;
        QPainter painter(widget);
        const auto device = painter.deviceTransform();
        if (device.type() > QTransform::TxScale)
            return true;
        // Paint in device pixels.
        painter.setWorldTransform(device.inverted());
        painter.setRenderHint(QPainter::Antialiasing, false);
        const auto x = [&](qreal logical) { return device.map(QPointF(logical, 0)).x(); };
        const auto y = [&](qreal logical) { return device.map(QPointF(0, logical)).y(); };
        const auto down = [](qreal v) { return int(std::floor(v + 1e-6)); };
        const auto up = [](qreal v) { return int(std::ceil(v - 1e-6)); };
        const auto area = QRectF(ring.key.area);
        for (int edge = 0; edge < 4; ++edge) {
            const auto &e = ring.edges[edge];
            const bool horizontal = edge == 0 || edge == 2, near = edge == 0 || edge == 3;
            const auto across = [&](qreal logical) { return horizontal ? y(logical) : x(logical); };
            const qreal origin = horizontal ? area.top() : area.left();
            const qreal extent = horizontal ? area.height() : area.width();
            // Along the edge: the straight span, inside the device pixels it covers.
            const int first = up(horizontal ? x(area.left() + e.from) : y(area.top() + e.from));
            const int last = down(horizontal ? x(area.left() + e.to) : y(area.top() + e.to));
            // Across it: the ring from the device pixel holding its outer edge, ceil(width x
            // ratio) pixels deep; the pixel its inner edge part-covers takes the fill inside.
            const int thickness = up(e.width * (horizontal ? device.m22() : device.m11()));
            const qreal outer = near ? origin + e.depth : origin + extent - e.depth;
            const qreal inner = near ? outer + e.width : outer - e.width;
            int ringFrom, ringTo, insideFrom, insideTo;
            if (near) {
                ringFrom = down(across(outer));
                ringTo = ringFrom + thickness;
                insideFrom = ringTo;
                insideTo = up(across(inner));
            } else {
                ringTo = up(across(outer));
                ringFrom = ringTo - thickness;
                insideFrom = down(across(inner));
                insideTo = ringFrom;
            }
            const auto band = [&](int from, int to) {
                return horizontal ? QRect(first, from, last - first, to - from)
                                  : QRect(from, first, to - from, last - first);
            };
            // The halo, from the device pixel holding its outer edge to the ring.
            if (e.haloWidth) {
                const qreal edgeOuter = near ? outer - e.haloWidth : outer + e.haloWidth;
                const int haloFrom = near ? down(across(edgeOuter)) : ringTo;
                const int haloTo = near ? ringFrom : up(across(edgeOuter));
                if (haloTo > haloFrom)
                    painter.fillRect(band(haloFrom, haloTo), ring.halo);
            }
            painter.fillRect(band(ringFrom, ringTo), ring.color);
            if (e.inside.isValid() && insideTo > insideFrom)
                painter.fillRect(band(insideFrom, insideTo), e.inside);
        }
        return true;
    }
    QHash<const QWidget *, QList<Ring>> rings_;
    QSet<QWidget *> pending_;
    QObject *probing_{}, *forwarding_{};
};
} // namespace
void installFractionalFocusRings() {
    static QPointer<FractionalFocusRings> installed;
    if (!installed)
        installed = new FractionalFocusRings(qApp);
}
} // namespace sketchy
