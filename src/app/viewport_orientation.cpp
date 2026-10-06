#include "app/viewport.hpp"
#include <algorithm>
namespace sketchy {
void Viewport::setOrientationMode(bool orient) {
    orientConnected_ = orient;
    if (tool_ == Tool::Orientation)
        setTool(Tool::Orientation);
}
QString Viewport::orientationSummary() const {
    const auto mode = orientConnected_ ? "Orient connected faces" : "Reverse selected faces";
    return QString("%1 · %2 face(s) change · Arrows show new fronts · Physical materials retained")
        .arg(mode)
        .arg(orientationNormals_.size());
}
void Viewport::beginOrientation() {
    syncSelection();
    QJsonArray faces;
    for (auto entity : selection_.entities()) {
        if (entity.kind != SelectionKind::Face || !selectable(entity))
            throw std::runtime_error("Select editable faces; enter group containers first");
        faces.append(QJsonObject{{"body", QString::number(entity.body)},
                                 {"face", QString::number(entity.entity)}});
    }
    if (faces.empty() || (orientConnected_ && faces.size() != 1))
        throw std::runtime_error(
            orientConnected_ ? "Select exactly one reference face to orient its connected surface"
                             : "Select faces to reverse, then Shift+O");
    orientationCommand_ = QJsonObject{
        {"command", orientConnected_ ? "geometry.orient_faces" : "geometry.reverse_faces"},
        {"context", QString::number(selection_.context())}};
    if (orientConnected_) {
        const auto reference = faces[0].toObject();
        (*orientationCommand_)["body"] = reference["body"];
        (*orientationCommand_)["face"] = reference["face"];
    } else {
        (*orientationCommand_)["entities"] = faces;
    }
    session_.begin();
    const auto preview = previewCommand(*orientationCommand_);
    if (!previewValid_) {
        if (previewError_ == "Batch has no committed changes") {
            previewError_ = "Connected faces already match the reference; no orientation changes";
            emit message(previewError_);
        }
        return;
    }
    const auto changes = preview["changes"].toObject();
    for (auto it = changes.begin(); it != changes.end(); ++it) {
        const auto bodyId = it.key().toULongLong();
        if (!doc_.bodies().contains(bodyId))
            continue;
        const auto &body = *doc_.bodies().at(bodyId);
        const auto frame = doc_.worldTransform(bodyId), inverse = frame.inverse();
        for (auto value : it.value().toObject()["faces"].toObject()["modified"].toArray()) {
            const auto face = value.toString().toULongLong();
            if (!selectable({bodyId, SelectionKind::Face, face}))
                continue;
            const auto triangles = body.surface.triangulate(face);
            const auto triangle = *std::max_element(triangles.begin(), triangles.end(),
                                                    [](const Triangle &a, const Triangle &b) {
                                                        return length(cross(a.b - a.a, a.c - a.a)) <
                                                               length(cross(b.b - b.a, b.c - b.a));
                                                    });
            const auto origin = frame.point((triangle.a + triangle.b + triangle.c) * (1.0 / 3));
            const auto old = body.surface.normal(face);
            const auto newFront =
                normalized(
                    Vec3{inverse.m[0] * old.x + inverse.m[1] * old.y + inverse.m[2] * old.z,
                         inverse.m[4] * old.x + inverse.m[5] * old.y + inverse.m[6] * old.z,
                         inverse.m[8] * old.x + inverse.m[9] * old.y + inverse.m[10] * old.z}) *
                -1;
            const auto size = .22 * std::max(length(frame.vector(triangle.b - triangle.a)),
                                             length(frame.vector(triangle.c - triangle.a)));
            orientationNormals_.push_back({origin, origin + newFront * size});
        }
    }
    emit message(orientationSummary() +
                 " · Enter or click applies · Esc cancels · Alt-drag orbits");
    update();
}
void Viewport::finishOrientation() {
    if (!orientationCommand_ || !session_.active())
        throw std::runtime_error("Select faces and press Shift+O to preview orientation");
    if (!previewValid_)
        throw std::runtime_error(previewError_.toStdString());
    const auto selected = selection_.entities();
    session_.commit(*orientationCommand_);
    cancel();
    refresh();
    selectEntities(selected);
    emit changed();
    emit message(
        "Face orientation applied · Selection and physical materials retained · Ctrl+Z undoes");
}
} // namespace sketchy
