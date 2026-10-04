#include "app/viewport.hpp"
#include <QJsonArray>
#include <QLocale>
#include <QPainter>
#include <numbers>
namespace sketchy {
void Viewport::setGuidesVisible(bool visible) {
    guidesVisible_ = visible;
    clearConstraints();
    inference_ = {};
    if (session_.active())
        updateToolPreview(inferencePointer_);
    update();
}
void Viewport::setGuideCreation(bool enabled) {
    createGuides_ = enabled;
    if (guideTool() && session_.active())
        updateToolPreview(inferencePointer_);
    emit guideCreationChanged(enabled);
    emit message(enabled ? "Create guides · Ctrl toggles measurement only"
                         : "Measure only · Ctrl toggles guide creation");
    update();
}
void Viewport::clearGuides() {
    cancel();
    doc_.clearGuides();
    refresh();
    emit changed();
    emit message("Guides cleared · Ctrl+Z restores them");
}
void Viewport::captureTapeReference() {
    tapeReference_.reset();
    if (tool_ != Tool::Tape)
        return;
    const auto source = acquiredInference();
    if (!source || !doc_.bodies().contains(source->body))
        return;
    const auto &body = *doc_.bodies().at(source->body);
    const auto world = doc_.worldTransform(source->body);
    Vec3 direction;
    if (source->entityType == InferenceEntity::Edge &&
        (source->kind == InferenceKind::OnEdge || source->kind == InferenceKind::Midpoint)) {
        const auto &edge = body.topology.edges.at(source->entity);
        direction =
            world.vector(body.surface.vertices.at(edge.b) - body.surface.vertices.at(edge.a));
    } else if (source->entityType == InferenceEntity::Guide &&
               body.guides.at(source->entity).kind == GuideKind::Line)
        direction = world.vector(body.guides.at(source->entity).direction);
    else
        return;
    auto line = guideLine(source->point, direction);
    if (std::abs(dot(line.direction, plane_.normal)) <= 1e-10)
        tapeReference_ = line;
}
double Viewport::guideMeasurement(Vec3 end) const {
    const auto origin = anchor_ ? anchor_ : committedAnchor_;
    if (!origin)
        throw std::runtime_error("Choose the first measurement point");
    if (tool_ == Tool::Protractor) {
        const auto base = baseline_ ? baseline_ : committedBaseline_;
        if (!base)
            throw std::runtime_error("Choose the baseline endpoint");
        return measureAngle(*origin, *base, end, plane_.normal);
    }
    if (tapeReference_)
        return dot(end - tapeReference_->origin, cross(plane_.normal, tapeReference_->direction));
    return measureDistance(*origin, end);
}
QString Viewport::guideMeasurementText(Vec3 end) const {
    const auto value = guideMeasurement(end);
    return QLocale().toString(tool_ == Tool::Protractor ? value * 180 / std::numbers::pi : value,
                              'g', 10) +
           (tool_ == Tool::Protractor ? " deg" : " m");
}
Guide Viewport::prospectiveGuide(Vec3 end) const {
    const auto origin = anchor_ ? anchor_ : committedAnchor_;
    if (!origin)
        throw std::runtime_error("Choose the first measurement point");
    if (tool_ == Tool::Protractor) {
        const auto base = baseline_ ? baseline_ : committedBaseline_;
        if (!base)
            throw std::runtime_error("Choose the baseline endpoint");
        return angledGuide(DrawingPlane::make(*origin, plane_.normal, *base - *origin),
                           guideMeasurement(end));
    }
    if (tapeReference_)
        return offsetGuide(*tapeReference_, plane_.normal, guideMeasurement(end));
    if (measureDistance(*origin, end) <= tolerance)
        throw std::runtime_error("Choose a distinct measurement endpoint");
    return guidePoint(end);
}
QJsonObject Viewport::guideCommand(Vec3 end) const {
    const auto guide = prospectiveGuide(end);
    auto point = [](Vec3 p) { return QJsonArray{p.x, p.y, p.z}; };
    QJsonObject command{{"command", guide.kind == GuideKind::Point ? "guide.point" : "guide.line"},
                        {"body", QString::number(drawingContext_)},
                        {"space", "world"},
                        {"origin", point(guide.origin)}};
    if (guide.kind == GuideKind::Line)
        command["direction"] = point(guide.direction);
    return command;
}
void Viewport::finishGuide(Vec3 end) {
    validateLockedPoint(end);
    const auto label = guideMeasurementText(end);
    if (!createGuides_) {
        if (!session_.active() || !session_.current())
            throw std::runtime_error("Choose a new measurement starting point");
        cancel();
        measurementCompleted_ = true;
        emit measurementPreview(label);
        emit message("Measured " + label + " · No guide created");
        return;
    }
    const auto origin = anchor_ ? anchor_ : committedAnchor_;
    const auto command = guideCommand(end);
    const auto result = session_.commit(command);
    committedShape_ = command;
    committedAnchor_ = origin;
    committedEnd_ = end;
    committedBaseline_ = baseline_ ? baseline_ : committedBaseline_;
    const auto created = result["created"].toArray();
    const auto id = created.empty() ? drawingContext_ : created[0].toString().toULongLong();
    clearConstraints();
    clearPreview();
    if (id)
        setSelection(id);
    emit changed();
    emit message("Guide created · " + label + " · Type a new measurement to revise");
}
void Viewport::paintGuide(QPainter &p, const Guide &guide) const {
    const auto camera = inferenceCamera();
    if (guide.kind == GuideKind::Point) {
        if (const auto screen = camera.project(guide.origin); screen && !clipped(guide.origin)) {
            const QPointF at(screen->x, screen->y);
            p.drawLine(at - QPointF(4, 0), at + QPointF(4, 0));
            p.drawLine(at - QPointF(0, 4), at + QPointF(0, 4));
        }
        return;
    }
    const auto ends = boundedGuideLine(guide);
    const auto &m = camera.clipFromWorld;
    auto clip = [&](Vec3 a) {
        return std::array<double, 4>{m[0] * a.x + m[4] * a.y + m[8] * a.z + m[12],
                                     m[1] * a.x + m[5] * a.y + m[9] * a.z + m[13],
                                     m[2] * a.x + m[6] * a.y + m[10] * a.z + m[14],
                                     m[3] * a.x + m[7] * a.y + m[11] * a.z + m[15]};
    };
    const auto a = clip(ends[0]), b = clip(ends[1]);
    double low = 0, high = 1;
    auto trim = [&](double first, double second) {
        if (first < 0 && second < 0)
            return false;
        if (first < 0)
            low = std::max(low, first / (first - second));
        if (second < 0)
            high = std::min(high, first / (first - second));
        return low <= high;
    };
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : {-1, 1})
            if (!trim(a[3] + sign * a[axis], b[3] + sign * b[axis]))
                return;
    if (clipPlane_) {
        auto distance = [&](Vec3 point) {
            const auto &c = *clipPlane_;
            return c[0] * point.x + c[1] * point.y + c[2] * point.z + c[3];
        };
        if (!trim(distance(ends[0]), distance(ends[1])))
            return;
    }
    auto screen = [&](double t) {
        const auto w = a[3] + (b[3] - a[3]) * t;
        return QPointF((1 + (a[0] + (b[0] - a[0]) * t) / w) * camera.width * .5,
                       (1 - (a[1] + (b[1] - a[1]) * t) / w) * camera.height * .5);
    };
    if (a[3] + (b[3] - a[3]) * low > 0 && a[3] + (b[3] - a[3]) * high > 0)
        p.drawLine(screen(low), screen(high));
}
void Viewport::paintGuides(QPainter &p) const {
    p.save();
    p.setBrush(Qt::NoBrush);
    if (guidesVisible_) {
        p.setPen(QPen(colors_.muted, 1, Qt::DotLine));
        for (const auto &[id, body] : doc_.bodies()) {
            const auto world = doc_.worldTransform(id);
            for (const auto &[gid, guide] : body->guides)
                paintGuide(
                    p, guide.kind == GuideKind::Point
                           ? guidePoint(world.point(guide.origin))
                           : guideLine(world.point(guide.origin), world.vector(guide.direction)));
        }
    }
    if (previewGuide_) {
        p.setPen(QPen(colors_.accent, 2, Qt::DashLine));
        paintGuide(p, *previewGuide_);
    }
    p.restore();
}
} // namespace sketchy
