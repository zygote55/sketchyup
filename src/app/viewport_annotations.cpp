#include "app/annotation_display.hpp"
#include "app/viewport.hpp"
#include "automation/annotation_commands.hpp"
#include <QPainter>
namespace sketchy {
QJsonObject Viewport::editAnnotations(const QJsonArray &commands) {
    for (const auto &command : commands)
        if (!isAnnotationCommand(command.toObject()["command"].toString()))
            throw std::runtime_error("Expected dimension or label edit");
    cancel();
    auto result = commitCommands(commands, false);
    refresh();
    emit changed();
    return result;
}
void Viewport::paintAnnotations(QPainter &painter) {
    if (!doc_.isCurrentSnapshot(annotationStamp_)) {
        annotationMeasurements_.clear();
        for (const auto &[id, record] : doc_.annotations())
            annotationMeasurements_.emplace(id, measureAnnotation(doc_, *record));
        annotationStamp_ = doc_.saveStamp();
    }
    const auto transform = matrix();
    auto inFront = [&](Vec3 p) {
        const auto relative = p - renderOrigin_;
        const auto v =
            transform * QVector4D(float(relative.x), float(relative.y), float(relative.z), 1);
        return v.w() > 1e-9 && v.z() >= -v.w() && v.z() <= v.w();
    };
    painter.save();
    painter.setClipRect(QRect(0, 0, renderWidth(), renderHeight()));
    for (const auto &[id, record] : doc_.annotations()) {
        const auto &m = annotationMeasurements_.at(id);
        if (!inFront(m.textPoint))
            continue;
        bool suppressed{};
        for (size_t i = 0; i < record->anchors.size(); ++i) {
            const auto &a = record->anchors[i];
            const auto &resolved = m.anchors[i];
            if (!inFront(resolved.point)) {
                suppressed = true;
                break;
            }
            if (resolved.state != AnchorState::Resolved || !a.body)
                continue;
            const auto kind = a.kind == AnchorKind::Face   ? SelectionKind::Face
                              : a.kind == AnchorKind::Edge ? SelectionKind::Edge
                                                           : SelectionKind::Body;
            if (!visible({a.body, kind, kind == SelectionKind::Body ? 0 : a.entity}) ||
                clipped(resolved.point, a.body)) {
                suppressed = true;
                break;
            }
        }
        if (suppressed)
            continue;
        const bool broken = m.state != AnchorState::Resolved;
        const auto color =
            broken ? QColor(180, 45, 30)
                   : QColor::fromRgbF(record->color[0], record->color[1], record->color[2]);
        painter.setPen(QPen(color, 1.5, broken ? Qt::DashLine : Qt::SolidLine));
        painter.setBrush(Qt::NoBrush);
        const auto textPoint = project(m.textPoint);
        auto tick = [&](QPointF p, QPointF direction) {
            const auto length = std::hypot(direction.x(), direction.y());
            if (length < .01)
                return;
            direction /= length;
            const QPointF perpendicular{-direction.y(), direction.x()};
            painter.drawLine(p, p + direction * 7 + perpendicular * 3);
            painter.drawLine(p, p + direction * 7 - perpendicular * 3);
        };
        if (record->kind == AnnotationKind::Distance) {
            if (!inFront(m.anchors[0].point + record->offset) ||
                !inFront(m.anchors[1].point + record->offset))
                continue;
            const auto a = project(m.anchors[0].point), b = project(m.anchors[1].point);
            const auto da = project(m.anchors[0].point + record->offset);
            const auto db = project(m.anchors[1].point + record->offset);
            painter.drawLine(da, db);
            if (record->leader) {
                painter.drawLine(a, da);
                painter.drawLine(b, db);
            }
            tick(da, db - da);
            tick(db, da - db);
        } else if (record->leader) {
            const auto a = project(m.anchors[0].point);
            painter.drawLine(a, textPoint);
            tick(a, textPoint - a);
        }
        if (broken)
            for (const auto &anchor : m.anchors) {
                if (anchor.state == AnchorState::Resolved)
                    continue;
                const auto p = project(anchor.point);
                painter.drawLine(p + QPointF(-4, -4), p + QPointF(4, 4));
                painter.drawLine(p + QPointF(-4, 4), p + QPointF(4, -4));
            }
        auto font = painter.font();
        font.setPixelSize(qRound(record->textSize));
        painter.setFont(font);
        const auto text = annotationText(*record, m, doc_.displayUnits());
        const auto bounds =
            painter.fontMetrics().boundingRect(QRect(0, 0, 360, 1000), Qt::TextWordWrap, text);
        QRectF box(textPoint.x() - bounds.width() / 2. - 5,
                   textPoint.y() - bounds.height() / 2. - 3, bounds.width() + 10,
                   bounds.height() + 6);
        painter.setPen(Qt::NoPen);
        auto background = colors_.canvas;
        background.setAlpha(235);
        painter.setBrush(background);
        painter.drawRoundedRect(box, 3, 3);
        painter.setPen(color);
        painter.drawText(box.adjusted(5, 3, -5, -3), Qt::TextWordWrap | Qt::AlignCenter, text);
    }
    painter.restore();
}
} // namespace sketchy
