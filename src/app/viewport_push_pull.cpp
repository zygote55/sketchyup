#include "app/viewport.hpp"
namespace sketchy {
void Viewport::setPushPullNewFace(bool enabled) {
    pushNewFace_ = enabled;
    if (tool_ == Tool::Extrude && session_.active())
        previewCommand(extrusionCommand(previewDistance_));
    emit pushPullModeChanged(enabled);
    emit message(enabled ? "Create new face: on · Starting face and walls stay in place"
                         : "Create new face: off · Push/pull edits the existing cap");
    update();
}
void Viewport::beginExtrusion(Id body, Id face, Vec3 anchor) {
    if (!selectable({body, SelectionKind::Face, face}))
        throw std::runtime_error("Choose an editable face");
    session_.begin();
    anchor_ = anchor;
    const auto &surface = doc_.bodies().at(body)->surface;
    extrusionLocalNormal_ = surface.normal(face);
    extrusionLocalOrigin_ = surface.vertices.at(surface.faces.at(face).loops[0][0]);
    const auto vector = doc_.worldTransform(body).vector(extrusionLocalNormal_);
    extrusionScale_ = length(vector);
    extrusionAxis_ = vector * (1 / extrusionScale_);
    previewDistance_ = 0;
}
QJsonObject Viewport::extrusionCommand(double distance) const {
    return {{"command", "geometry.push_pull"},
            {"body", QString::number(session_.canRevise() ? committedBody_ : selected_)},
            {"face", QString::number(session_.canRevise() ? committedFace_ : selectedFace_)},
            {"distance", distance / extrusionScale_},
            {"newFace", pushNewFace_}};
}
void Viewport::finishExtrusion(double distance) {
    const auto body = session_.canRevise() ? committedBody_ : selected_;
    const auto face = session_.canRevise() ? committedFace_ : selectedFace_;
    const auto result = session_.commit(extrusionCommand(distance));
    committedBody_ = body;
    committedFace_ = face;
    lastPushDistance_ = distance;
    repeatSession_ = doc_.saveStamp();
    // Select the resulting cap, including a newly allocated cap after amendment.
    const auto changes =
        result["changes"].toObject()[QString::number(body)].toObject()["faces"].toObject();
    auto candidates = changes["created"].toArray();
    for (const auto &id : changes["descendants"].toObject()[QString::number(face)].toArray())
        candidates.append(id);
    candidates.append(QString::number(face));
    Id cap = 0;
    const auto &surface = doc_.bodies().at(body)->surface;
    for (const auto &value : candidates) {
        const auto id = value.toString().toULongLong();
        if (!surface.faces.contains(id))
            continue;
        bool destination = true;
        for (const auto &loop : surface.faces.at(id).loops)
            for (auto vertex : loop)
                destination &= std::abs(dot(surface.vertices.at(vertex) - extrusionLocalOrigin_,
                                            extrusionLocalNormal_) -
                                        distance / extrusionScale_) <= tolerance;
        if (destination) {
            cap = id;
            break;
        }
    }
    clearPreview();
    refresh();
    if (cap)
        setSelection(body, cap);
    emit changed();
    emit message(QString("Push/pull %1 m · Double-click another face to repeat · Ctrl+Z undoes")
                     .arg(distance, 0, 'g', 8));
}
void Viewport::repeatPushPull() {
    if (!lastPushDistance_ || !doc_.owns(repeatSession_))
        throw std::runtime_error("Complete a push/pull in this document before repeating");
    const auto body = selected_, face = selectedFace_;
    if (!body || !face || !selectable({body, SelectionKind::Face, face}))
        throw std::runtime_error("Select an editable face to repeat push/pull");
    const auto distance = *lastPushDistance_;
    setTool(Tool::Extrude);
    const auto &surface = doc_.bodies().at(body)->surface;
    const auto point =
        doc_.worldTransform(body).point(surface.vertices.at(surface.faces.at(face).loops[0][0]));
    beginExtrusion(body, face, point);
    finishExtrusion(distance);
}
} // namespace sketchy
