#include "app/viewport.hpp"
namespace sketchy {
void Viewport::setIntersectionMode(const QString &mode) {
    if (mode != "selected" && mode != "context" && mode != "model")
        throw std::runtime_error("Choose selected, context or model intersection references");
    intersectionMode_ = mode;
    if (tool_ == Tool::Intersect)
        setTool(Tool::Intersect);
    else
        emit message("Intersection references: " + mode);
}
void Viewport::beginIntersection() {
    syncSelection();
    QJsonArray entities;
    for (auto entity : selection_.entities()) {
        if (entity.kind != SelectionKind::Face || !selectable(entity))
            throw std::runtime_error(
                "Select editable faces, then press I to preview intersections");
        entities.append(QJsonObject{{"body", QString::number(entity.body)},
                                    {"face", QString::number(entity.entity)}});
    }
    if (entities.empty() || (intersectionMode_ == "selected" && entities.size() < 2))
        throw std::runtime_error(
            intersectionMode_ == "selected"
                ? "Select at least two faces, or choose Context/Model references in Draw"
                : "Select a target face, then press I to preview intersections");
    intersectionCommand_ = QJsonObject{{"command", "geometry.intersect"},
                                       {"context", QString::number(selection_.context())},
                                       {"mode", intersectionMode_},
                                       {"entities", entities}};
    session_.begin();
    previewCommand(*intersectionCommand_);
    if (!previewValid_ && previewError_ == "Batch has no committed changes") {
        previewError_ = "No new intersection edges in this scope; choose crossing faces or another "
                        "reference mode";
        emit message(previewError_);
    } else if (previewValid_) {
        emit message("Intersect · References: " + intersectionMode_ +
                     " · Enter or click applies · Esc cancels · Reference bodies retained");
    }
}
void Viewport::finishIntersection() {
    if (!intersectionCommand_ || !session_.active())
        throw std::runtime_error("Select target faces and press I to preview intersections");
    if (!previewValid_)
        throw std::runtime_error(previewError_.toStdString());
    const auto selected = selection_.entities();
    const auto result = session_.commit(*intersectionCommand_);
    SelectionSet mapped;
    for (auto entity : selected) {
        const auto descendants = result["changes"]
                                     .toObject()[QString::number(entity.body)]
                                     .toObject()["faces"]
                                     .toObject()["descendants"]
                                     .toObject();
        const auto key = QString::number(entity.entity);
        if (descendants.contains(key)) {
            for (auto id : descendants[key].toArray())
                mapped.insert({entity.body, SelectionKind::Face, id.toString().toULongLong()});
        } else {
            mapped.insert(entity);
        }
    }
    cancel();
    refresh();
    selectEntities(mapped);
    emit changed();
    emit message("Intersection edges inserted · Target descendants selected · Ctrl+Z undoes");
}
} // namespace sketchy
