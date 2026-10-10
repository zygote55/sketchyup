#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include <algorithm>
#include <limits>
namespace sketchy {
void Viewport::beginOffset(Id body, Id face, Vec3 anchor) {
    if (!selectable({body, SelectionKind::Face, face}))
        throw std::runtime_error("Choose an editable face to offset");
    const auto &surface = doc_.bodies().at(body)->surface;
    const auto world = doc_.worldTransform(body);
    const auto &loops = surface.faces.at(face).loops;
    std::vector<Vec3> outer;
    for (auto id : loops[0])
        outer.push_back(world.point(surface.vertices.at(id)));
    Vec3 normal{};
    for (size_t i = 0; i < outer.size(); ++i)
        normal = normal + cross(outer[i] - outer[0], outer[(i + 1) % outer.size()] - outer[0]);
    // The normal has area units; normalize explicitly for micrometer faces.
    const auto magnitude = length(normal);
    if (!std::isfinite(magnitude) || magnitude < 2 * tolerance * tolerance)
        throw std::runtime_error("Face is too small to offset in world space");
    normal = normal * (1 / magnitude);
    double nearest = std::numeric_limits<double>::infinity();
    Vec3 axis{};
    for (size_t h = 0; h < loops.size(); ++h) {
        std::vector<Vec3> points;
        for (auto id : loops[h])
            points.push_back(world.point(surface.vertices.at(id)));
        double orientation{};
        for (size_t i = 0; i < points.size(); ++i)
            orientation += dot(
                cross(points[i] - points[0], points[(i + 1) % points.size()] - points[0]), normal);
        const double sign = (orientation > 0 ? 1.0 : -1.0) * (h == 0 ? 1.0 : -1.0);
        for (size_t i = 0; i < points.size(); ++i) {
            const auto a = points[i], delta = points[(i + 1) % points.size()] - a;
            const auto closest =
                a + delta * std::clamp(dot(anchor - a, delta) / dot(delta, delta), 0.0, 1.0);
            const auto distance = length(anchor - closest);
            if (distance < nearest) {
                nearest = distance;
                axis = cross(normalized(delta), normal) * sign;
            }
        }
    }
    session_.begin();
    offsetBody_ = body;
    offsetFace_ = face;
    offsetNormal_ = normal;
    offsetAxis_ = axis;
    anchor_ = anchor;
    previewDistance_ = 0;
}
QJsonObject Viewport::offsetCommand(double distance) const {
    return {{"command", "geometry.offset"},
            {"body", QString::number(offsetBody_)},
            {"face", QString::number(offsetFace_)},
            {"distance", distance},
            {"space", "world"}};
}
void Viewport::updateOffsetPreview(QPointF point) {
    const auto [origin, direction] = ray(point);
    const auto denominator = dot(direction, offsetNormal_);
    if (std::abs(denominator) < 1e-8) {
        previewValid_ = false;
        previewEdges_.clear();
        previewError_ = "Orbit to view the face plane or enter an offset distance";
        emit message(previewError_);
        update();
        return;
    }
    const auto intersection =
        origin + direction * (dot(*anchor_ - origin, offsetNormal_) / denominator);
    previewDistance_ = dot(intersection - *anchor_, offsetAxis_);
    if (std::abs(previewDistance_) < tolerance) {
        previewValid_ = false;
        previewEdges_.clear();
        previewError_.clear();
        emit measurementPreview(displayLength(0, doc_.displayUnits(), doc_.displayPrecision()));
        update();
        return;
    }
    previewCommand(offsetCommand(previewDistance_));
    emit measurementPreview(displayLength(previewDistance_, doc_.displayUnits(), doc_.displayPrecision()));
}
void Viewport::finishOffset(double distance) {
    if (!std::isfinite(distance) || std::abs(distance) < tolerance)
        throw std::runtime_error("Enter a nonzero offset of at least 0.0000001 m");
    const auto result = session_.commit(offsetCommand(distance));
    const auto faceChanges =
        result["changes"].toObject()[QString::number(offsetBody_)].toObject()["faces"].toObject();
    auto candidates = faceChanges["descendants"].toObject()[QString::number(offsetFace_)].toArray();
    // Amendment reports map the previous result to its replacement, so the
    // original profile ID may no longer be in that report. Include generated
    // faces rather than losing selection after numeric re-entry.
    for (auto value : faceChanges["created"].toArray())
        candidates.append(value);
    candidates.append(QString::number(offsetFace_));
    clearPreview();
    refresh();
    SelectionSet selected;
    for (auto value : candidates) {
        const auto id = value.toString().toULongLong();
        if (doc_.bodies().at(offsetBody_)->surface.faces.contains(id))
            selected.insert({offsetBody_, SelectionKind::Face, id});
    }
    selectEntities(selected);
    emit changed();
    emit message(QString("Offset %1 · Source faces and holes retained · Enter a new distance to "
                         "revise · Ctrl+Z undoes")
                     .arg(displayLength(distance, doc_.displayUnits(), doc_.displayPrecision())));
}
} // namespace sketchy
