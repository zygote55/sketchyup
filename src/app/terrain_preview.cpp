#include "app/terrain_preview.hpp"
#include <QAccessible>
#include <QKeyEvent>
#include <QPolygonF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
namespace sketchy {
void finishTerrainPreview(TerrainPreviewData &data) {
    data.edges.clear();
    data.edges.reserve(data.triangles.size() * 3 / 2 + 3);
    for (const auto &triangle : data.triangles)
        for (int k = 0; k < 3; ++k) {
            auto a = triangle[size_t(k)], b = triangle[size_t((k + 1) % 3)];
            if (a > b)
                std::swap(a, b);
            data.edges.push_back({a, b});
        }
    std::sort(data.edges.begin(), data.edges.end());
    data.edges.erase(std::unique(data.edges.begin(), data.edges.end()), data.edges.end());
    data.edges.shrink_to_fit();
    double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    float zMin = INFINITY, zMax = -INFINITY;
    auto take = [&](QPointF p, float z) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
        zMin = std::min(zMin, z);
        zMax = std::max(zMax, z);
    };
    for (const auto &line : data.lines)
        for (const auto &p : line.points)
            take(p, line.z);
    for (size_t i = 0; i < data.points.size(); ++i)
        take(data.points[i], data.pointZ[i]);
    for (size_t i = 0; i < data.vertices.size(); ++i)
        take(data.vertices[i], data.vertexZ[i]);
    data.contours = data.breaklines = data.boundaries = data.holes = 0;
    for (const auto &line : data.lines)
        switch (line.role) {
        case TerrainSourceRole::Contour:
            ++data.contours;
            break;
        case TerrainSourceRole::Breakline:
            ++data.breaklines;
            break;
        case TerrainSourceRole::Boundary:
            ++data.boundaries;
            break;
        case TerrainSourceRole::Hole:
            ++data.holes;
            break;
        case TerrainSourceRole::Points:
            break;
        }
    if (minX > maxX) {
        data.bounds = {};
        data.zMin = data.zMax = 0;
        return;
    }
    data.bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
    data.zMin = zMin;
    data.zMax = zMax;
}

TerrainPreview::TerrainPreview(QWidget *parent) : QWidget(parent) {
    setObjectName("terrainImportPreview");
    setAccessibleName("Terrain preview");
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(240, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    describe();
}
void TerrainPreview::setData(std::shared_ptr<const TerrainPreviewData> data) {
    const auto before = data_ ? data_->bounds : QRectF();
    data_ = std::move(data);
    // Keep a view the user chose while settings change; otherwise fit the new extent.
    if (autoFit_ || !data_ || (data_->bounds != before && !marker_))
        fit();
    changed();
}
void TerrainPreview::setPlaceholder(const QString &text) {
    placeholder_ = text;
    describe();
    update();
}
void TerrainPreview::setMode(Mode mode) {
    if (mode_ == mode)
        return;
    mode_ = mode;
    changed();
}
void TerrainPreview::setShading(bool shading) {
    if (shading_ == shading)
        return;
    shading_ = shading;
    changed();
}
void TerrainPreview::setColors(const ThemeColors &colors) {
    colors_ = colors;
    changed();
}
void TerrainPreview::setOverlays(TerrainPreviewOverlays overlays) {
    overlays_ = std::move(overlays);
    // The view follows the overlays too, until the person moves it.
    if (autoFit_)
        fit();
    changed();
}
void TerrainPreview::setSummary(const QString &summary) {
    summary_ = summary;
    describe();
}
// The data's extent and the overlays' together: what a fit must show.
QRectF TerrainPreview::contentBounds() const {
    double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    auto take = [&](QPointF p) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    };
    if (data_ && !data_->bounds.isNull()) {
        take(data_->bounds.topLeft());
        take(data_->bounds.bottomRight());
    }
    for (const auto *outline : {&overlays_.file, &overlays_.window, &overlays_.grid})
        for (const auto &p : *outline)
            take(p);
    if (overlays_.origin)
        take(*overlays_.origin);
    for (const auto &p : overlays_.voids)
        take(p);
    if (minX > maxX)
        return {};
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}
void TerrainPreview::fit() {
    const auto bounds = contentBounds();
    if (bounds.isNull()) {
        scale_ = 1;
        centre_ = {};
        autoFit_ = true;
        changed();
        return;
    }
    const double w = std::max(bounds.width(), 1e-3), h = std::max(bounds.height(), 1e-3);
    // 24 px around, plus room for the legend band at the bottom.
    const double margin = 24, legend = 28;
    scale_ = std::max(1e-6,
                      std::min((width() - 2 * margin) / w, (height() - 2 * margin - legend) / h));
    centre_ = bounds.center() - QPointF(0, legend / 2 / scale_);
    autoFit_ = true;
    changed();
}
void TerrainPreview::focusLocation(QPointF local, double span) {
    marker_ = local;
    autoFit_ = false;
    centre_ = local;
    scale_ = std::max(1e-6, std::min(width(), height()) / std::max(span, 1e-3));
    changed();
}
void TerrainPreview::clearMarker() {
    if (!marker_)
        return;
    marker_.reset();
    update();
}
QPointF TerrainPreview::toScreen(QPointF local) const {
    return {width() / 2.0 + (local.x() - centre_.x()) * scale_,
            height() / 2.0 - (local.y() - centre_.y()) * scale_};
}
void TerrainPreview::changed() {
    cacheValid_ = false;
    describe();
    update();
}
void TerrainPreview::describe() {
    QString text;
    if (!data_ || !placeholder_.isEmpty())
        text = placeholder_;
    else if (mode_ == Mode::Surface)
        text = QString("Surface: %1 triangles").arg(QLocale().toString(qulonglong(data_->triangles.size())));
    else
        text = QString("Sources: %1 contours, %2 breaklines, %3 boundary, %4 holes, %5 points")
                   .arg(data_->contours)
                   .arg(data_->breaklines)
                   .arg(data_->boundaries)
                   .arg(data_->holes)
                   .arg(QLocale().toString(qulonglong(data_->points.size())));
    if (marker_)
        text += QString(". Marked location at %1, %2 m")
                    .arg(marker_->x(), 0, 'f', 2)
                    .arg(marker_->y(), 0, 'f', 2);
    text += ". Arrow keys pan, plus and minus zoom, Shift+Z fits.";
    if (!summary_.isEmpty())
        text = summary_ + ". " + text;
    if (accessibleDescription() != text) {
        setAccessibleDescription(text);
        QAccessibleEvent event(this, QAccessible::DescriptionChanged);
        QAccessible::updateAccessibility(&event);
    }
}
QColor TerrainPreview::elevationColour(float z) const {
    // Low green, middle sand, high brown: hypsometric bands that read in both themes.
    static const std::array<QColor, 3> ramp{QColor("#4f8a5b"), QColor("#c9b56e"),
                                            QColor("#8c5a3c")};
    const float range = data_ ? data_->zMax - data_->zMin : 0;
    float t = range > 0 ? (z - data_->zMin) / range : 0.5f;
    t = std::clamp(t, 0.f, 1.f);
    // Ten bands, so the shading reads as elevation steps.
    t = std::min(9.f, std::floor(t * 10)) / 9.f;
    const auto &a = t < .5f ? ramp[0] : ramp[1];
    const auto &b = t < .5f ? ramp[1] : ramp[2];
    const float u = t < .5f ? t * 2 : (t - .5f) * 2;
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * u,
                            a.greenF() + (b.greenF() - a.greenF()) * u,
                            a.blueF() + (b.blueF() - a.blueF()) * u);
}
void TerrainPreview::render(QImage &image) const {
    image.fill(colors_.canvas);
    if (!data_)
        return;
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, data_->triangles.size() < 20000);
    const auto &d = *data_;
    const QRectF view(-4, -4, width() + 8, height() + 8);
    auto screen = [&](QPointF local) { return toScreen(local); };
    if (mode_ == Mode::Surface) {
        std::vector<QPointF> v(d.vertices.size());
        for (size_t i = 0; i < v.size(); ++i)
            v[i] = screen(d.vertices[i]);
        if (shading_) {
            p.setPen(Qt::NoPen);
            for (const auto &t : d.triangles) {
                const QPointF corners[3]{v[t[0]], v[t[1]], v[t[2]]};
                const auto [minX, maxX] = std::minmax({corners[0].x(), corners[1].x(), corners[2].x()});
                const auto [minY, maxY] = std::minmax({corners[0].y(), corners[1].y(), corners[2].y()});
                if (maxX < view.left() || minX > view.right() || maxY < view.top() ||
                    minY > view.bottom())
                    continue;
                p.setBrush(elevationColour(
                    (d.vertexZ[t[0]] + d.vertexZ[t[1]] + d.vertexZ[t[2]]) / 3.f));
                p.drawPolygon(corners, 3);
            }
        }
        // Edges: thin, and fainter when the mesh is dense on screen.
        auto edgeColour = colors_.ink;
        const double pixels = d.bounds.width() * scale_ * d.bounds.height() * scale_;
        edgeColour.setAlphaF(shading_ ? .35 : (pixels / std::max<size_t>(d.edges.size(), 1) < 30 ? .35 : .7));
        p.setPen(QPen(edgeColour, 0));
        std::vector<QLineF> lines;
        lines.reserve(std::min<size_t>(d.edges.size(), 400000));
        for (const auto &e : d.edges) {
            const QLineF line(v[e[0]], v[e[1]]);
            if (!view.intersects(QRectF(line.p1(), line.p2()).normalized().adjusted(-1, -1, 1, 1)))
                continue;
            lines.push_back(line);
        }
        p.drawLines(lines.data(), int(lines.size()));
        return;
    }
    // Sources, by role (or by elevation with shading).
    auto roleColour = [&](TerrainSourceRole role) {
        switch (role) {
        case TerrainSourceRole::Contour:
            return colors_.ink;
        case TerrainSourceRole::Breakline:
            return colors_.information;
        case TerrainSourceRole::Boundary:
            return colors_.accent;
        case TerrainSourceRole::Hole:
            return colors_.warning;
        case TerrainSourceRole::Points:
            break;
        }
        return colors_.muted;
    };
    for (const auto &line : d.lines) {
        const bool polygon =
            line.role == TerrainSourceRole::Boundary || line.role == TerrainSourceRole::Hole;
        QPen pen(shading_ ? elevationColour(line.z) : roleColour(line.role), polygon ? 2 : 1.2);
        if (line.role == TerrainSourceRole::Breakline || line.role == TerrainSourceRole::Hole)
            pen.setStyle(Qt::DashLine);
        p.setPen(pen);
        QPolygonF poly;
        poly.reserve(qsizetype(line.points.size()));
        for (const auto &point : line.points)
            poly << screen(point);
        if (!view.intersects(poly.boundingRect().adjusted(-1, -1, 1, 1)))
            continue;
        p.drawPolyline(poly);
    }
    // Points as square dots, batched by colour (one batch, or one per elevation band), so a
    // million points draw in one pass.
    std::array<std::vector<QPointF>, 10> bands;
    const float range = d.zMax - d.zMin;
    for (size_t i = 0; i < d.points.size(); ++i) {
        const auto s = screen(d.points[i]);
        if (!view.contains(s))
            continue;
        const int band =
            shading_ && range > 0
                ? std::clamp(int(std::floor((d.pointZ[i] - d.zMin) / range * 10)), 0, 9)
                : 0;
        bands[size_t(band)].push_back(s);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
    for (size_t band = 0; band < bands.size(); ++band) {
        if (bands[band].empty())
            continue;
        const auto colour =
            shading_ ? elevationColour(d.zMin + range * (float(band) + .5f) / 10)
                     : roleColour(TerrainSourceRole::Points);
        p.setPen(QPen(colour, d.points.size() > 20000 ? 2 : 4, Qt::SolidLine, Qt::SquareCap));
        p.drawPoints(bands[band].data(), int(bands[band].size()));
    }
}
// R107.b4: the overlays, over the cached drawing. Every outline and mark has its label, drawn on a
// band of the canvas colour so it reads in both themes.
void TerrainPreview::paintOverlays(QPainter &p) const {
    const auto &o = overlays_;
    auto polygon = [&](const std::vector<QPointF> &points) {
        QPolygonF poly;
        poly.reserve(qsizetype(points.size()));
        for (const auto &point : points)
            poly << toScreen(point);
        return poly;
    };
    auto label = [&](QPointF at, const QString &text, QColor colour) {
        if (text.isEmpty())
            return;
        const auto metrics = p.fontMetrics();
        auto band = colors_.canvas;
        band.setAlphaF(.85);
        const QRectF box(at.x() - 3, at.y() - metrics.ascent() - 2,
                         metrics.horizontalAdvance(text) + 6, metrics.height() + 2);
        p.setPen(Qt::NoPen);
        p.setBrush(band);
        p.drawRect(box);
        p.setBrush(Qt::NoBrush);
        p.setPen(colour);
        p.drawText(at, text);
    };
    // The top-left corner of an outline on screen, where its label sits.
    auto corner = [&](const std::vector<QPointF> &points) {
        QRectF box;
        for (const auto &point : points)
            box = box.isNull() ? QRectF(toScreen(point), QSizeF(0, 0))
                               : box.united(QRectF(toScreen(point), QSizeF(0, 0)));
        return box.topLeft() + QPointF(6, 14);
    };
    p.setRenderHint(QPainter::Antialiasing);
    if (o.grid.size() >= 3) {
        auto fill = colors_.accent;
        fill.setAlphaF(.16);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawPolygon(polygon(o.grid));
        label(corner(o.grid), o.gridLabel, colors_.ink);
    }
    if (o.file.size() >= 3) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(colors_.information, 1.5, Qt::DashLine));
        p.drawPolygon(polygon(o.file));
        label(corner(o.file), o.fileLabel, colors_.ink);
    }
    if (o.window.size() >= 3) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(o.windowOutside ? colors_.failure : colors_.accent, 2));
        p.drawPolygon(polygon(o.window));
        label(corner(o.window), o.windowLabel, o.windowOutside ? colors_.failure : colors_.ink);
    }
    if (o.origin) {
        const auto s = toScreen(*o.origin);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(colors_.ink, 1.5));
        p.drawLine(s + QPointF(-7, 0), s + QPointF(7, 0));
        p.drawLine(s + QPointF(0, -7), s + QPointF(0, 7));
        label(s + QPointF(8, -6), QStringLiteral("Origin"), colors_.ink);
    }
    // A void post is a cross in the failure colour, so the mark carries the meaning, not colour alone.
    p.setPen(QPen(colors_.failure, 2));
    for (const auto &point : o.voids) {
        const auto s = toScreen(point);
        p.drawLine(s + QPointF(-4, -4), s + QPointF(4, 4));
        p.drawLine(s + QPointF(-4, 4), s + QPointF(4, -4));
    }
    if (!o.voids.empty())
        label(toScreen(o.voids.front()) + QPointF(6, -6), o.voidLabel, colors_.failure);
}
void TerrainPreview::paintEvent(QPaintEvent *) {
    QPainter p(this);
    const qreal ratio = devicePixelRatioF();
    if (!cacheValid_ || cache_.size() != size() * ratio) {
        cache_ = QImage(size() * ratio, QImage::Format_ARGB32_Premultiplied);
        cache_.setDevicePixelRatio(ratio);
        render(cache_);
        cacheValid_ = true;
    }
    p.drawImage(0, 0, cache_);
    p.setRenderHint(QPainter::Antialiasing);
    const bool overlaid = !overlays_.empty();
    if (!overlaid && (!placeholder_.isEmpty() || !data_)) {
        p.setPen(colors_.muted);
        p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                   placeholder_);
    } else if (!placeholder_.isEmpty()) {
        // With overlays, the placeholder (the dry run's stage) sits at the foot, not over them.
        p.setPen(colors_.muted);
        p.drawText(rect().adjusted(16, 0, -16, -8), Qt::AlignBottom | Qt::AlignLeft, placeholder_);
    } else if (data_ && mode_ == Mode::Sources && !shading_) {
        // The legend, bottom left on a band of the canvas colour, wrapping when narrow.
        const auto metrics = p.fontMetrics();
        struct Entry {
            QColor colour;
            Qt::PenStyle style;
            QString text;
            bool dot;
        };
        const std::vector<Entry> entries{{colors_.ink, Qt::SolidLine, "Contour", false},
                                         {colors_.information, Qt::DashLine, "Breakline", false},
                                         {colors_.accent, Qt::SolidLine, "Boundary", false},
                                         {colors_.warning, Qt::DashLine, "Hole", false},
                                         {colors_.muted, Qt::SolidLine, "Point", true}};
        std::vector<std::pair<QPoint, const Entry *>> placed;
        int x = 10, row = 0;
        const int line = metrics.height() + 4;
        for (const auto &entry : entries) {
            const int advance = 28 + metrics.horizontalAdvance(entry.text);
            if (x > 10 && x + advance > width() - 6) {
                x = 10;
                ++row;
            }
            placed.push_back({QPoint(x, row), &entry});
            x += advance;
        }
        const int rows = row + 1;
        const int top = height() - 8 - rows * line;
        auto band = colors_.canvas;
        band.setAlphaF(.85);
        p.fillRect(QRect(0, top - 2, width(), height() - top + 2), band);
        for (const auto &[at, entry] : placed) {
            const int y = top + at.y() * line + metrics.ascent() + 2;
            const double mid = y - metrics.ascent() / 2.0 + 1;
            if (entry->dot) {
                p.setPen(Qt::NoPen);
                p.setBrush(entry->colour);
                p.drawRect(QRectF(at.x() + 6, mid - 2, 4, 4));
            } else {
                p.setPen(QPen(entry->colour, 2, entry->style));
                p.drawLine(QPointF(at.x(), mid), QPointF(at.x() + 16, mid));
            }
            p.setPen(colors_.muted);
            p.drawText(QPointF(at.x() + 20, y), entry->text);
        }
    }
    if (overlaid)
        paintOverlays(p);
    if (marker_) {
        const auto s = toScreen(*marker_);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(colors_.failure, 3));
        p.drawEllipse(s, 11, 11);
        p.setPen(QPen(colors_.surface, 1));
        p.drawEllipse(s, 13, 13);
    }
    if (hasFocus()) {
        // The canvas focus ring, as the 3D viewport draws it.
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(colors_.canvasRing, 2));
        p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
    }
}
void TerrainPreview::resizeEvent(QResizeEvent *) {
    if (autoFit_ && data_)
        fit();
    cacheValid_ = false;
}
void TerrainPreview::zoomBy(double factor, QPointF anchor) {
    const QPointF before((anchor.x() - width() / 2.0) / scale_ + centre_.x(),
                         -(anchor.y() - height() / 2.0) / scale_ + centre_.y());
    autoFit_ = false;
    scale_ = std::clamp(scale_ * factor, 1e-6, 1e6);
    centre_ = QPointF(before.x() - (anchor.x() - width() / 2.0) / scale_,
                      before.y() + (anchor.y() - height() / 2.0) / scale_);
    changed();
}
void TerrainPreview::keyPressEvent(QKeyEvent *event) {
    const auto key = event->key();
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
        const double step = (shift ? 160 : 40) / scale_;
        autoFit_ = false;
        centre_ += QPointF(key == Qt::Key_Left ? -step : key == Qt::Key_Right ? step : 0,
                           key == Qt::Key_Down ? -step : key == Qt::Key_Up ? step : 0);
        changed();
        event->accept();
        return;
    }
    if (key == Qt::Key_Plus || key == Qt::Key_Equal || key == Qt::Key_Minus) {
        zoomBy(key == Qt::Key_Minus ? 1 / 1.25 : 1.25, QPointF(width() / 2.0, height() / 2.0));
        event->accept();
        return;
    }
    if (key == Qt::Key_Home || (key == Qt::Key_Z && shift)) {
        fit();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void TerrainPreview::wheelEvent(QWheelEvent *event) {
    const auto steps = event->angleDelta().y() / 120.0;
    if (steps != 0)
        zoomBy(std::pow(1.25, steps), event->position());
    event->accept();
}
void TerrainPreview::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
        drag_ = event->position();
}
void TerrainPreview::mouseMoveEvent(QMouseEvent *event) {
    if (!drag_)
        return;
    const auto delta = event->position() - *drag_;
    drag_ = event->position();
    autoFit_ = false;
    centre_ -= QPointF(delta.x() / scale_, -delta.y() / scale_);
    changed();
}
void TerrainPreview::mouseReleaseEvent(QMouseEvent *) { drag_.reset(); }
void TerrainPreview::focusInEvent(QFocusEvent *event) {
    QWidget::focusInEvent(event);
    update();
}
void TerrainPreview::focusOutEvent(QFocusEvent *event) {
    QWidget::focusOutEvent(event);
    update();
}
} // namespace sketchy
