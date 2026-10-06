#include "app/viewport.hpp"
namespace sketchy {
Id Viewport::paintMaterial() const {
    return doc_.owns(paintSession_) && doc_.materials().contains(paintMaterial_) ? paintMaterial_
                                                                                 : 0;
}
void Viewport::setPaintMaterial(Id material, int side) {
    if ((material && !doc_.materials().contains(material)) || side < 0 || side > 2)
        throw std::runtime_error("Choose an existing material and face side");
    paintMaterial_ = material;
    paintSide_ = side;
    paintSession_ = doc_.saveStamp();
    emit materialChanged();
}
void Viewport::editMaterials(const QJsonArray &commands) {
    for (const auto &value : commands) {
        const auto name = value.toObject().value("command").toString();
        if (name != "material.create" && name != "material.edit" && name != "material.delete" &&
            name != "asset.import" && name != "asset.missing" && name != "asset.replace" &&
            name != "asset.delete")
            throw std::runtime_error("Unsupported material library operation");
    }
    cancel();
    commitCommands(commands, false);
    refresh();
    emit changed();
}
void Viewport::applyMaterialToSelection() {
    selection_.sync(doc_);
    QJsonArray commands;
    for (const auto entity : selection_.entities()) {
        if (entity.kind != SelectionKind::Face && entity.kind != SelectionKind::Body)
            continue;
        if (!selectable(entity) || doc_.bodies().at(entity.body)->surface.faces.empty())
            throw std::runtime_error(
                "Select faces or open a group/component to paint its geometry");
        QJsonObject command{{"command", "material.assign"},
                            {"body", QString::number(entity.body)},
                            {"material", QString::number(paintMaterial())},
                            {"side", paintSide_ == 0   ? "front"
                                     : paintSide_ == 1 ? "back"
                                                       : "both"}};
        if (entity.kind == SelectionKind::Face)
            command["face"] = QString::number(entity.entity);
        commands.append(command);
    }
    if (commands.empty())
        throw std::runtime_error("Select faces to apply the material");
    cancel();
    commitCommands(commands);
    refresh();
    emit changed();
}
void Viewport::setSelectedEdgeAppearance(const QString &flag, bool value) {
    if (flag != "hidden" && flag != "soft" && flag != "smooth")
        throw std::runtime_error("Choose hide, soften or smooth for selected edges");
    syncSelection();
    QJsonArray edges;
    for (auto entity : selection_.entities()) {
        if (entity.kind != SelectionKind::Edge || !selectable(entity))
            throw std::runtime_error("Select editable edges; open groups/components first");
        edges.append(QJsonObject{{"body", QString::number(entity.body)},
                                 {"edge", QString::number(entity.entity)}});
    }
    if (edges.empty())
        throw std::runtime_error(
            "Select edges; use View → Show hidden geometry to reveal hidden or softened edges");
    QJsonObject command{{"command", "geometry.edge_appearance"},
                        {"context", QString::number(selection_.context())},
                        {"entities", edges},
                        {flag, value}};
    cancel();
    commitCommands({command});
    refresh();
    emit changed();
    emit message(
        value && flag != "smooth"
            ? "Edge appearance applied · View → Show hidden geometry reveals suppressed edges"
            : "Edge appearance applied · Undo restores the previous flags");
}
void Viewport::paintAt(QPointF point, bool sample) {
    selection_.sync(doc_);
    const auto hit = nearestFace(point);
    if (!hit.body)
        throw std::runtime_error("Point at a visible face");
    if (sample) {
        const auto [origin, direction] = ray(point);
        const auto triangles = doc_.worldTriangles(hit.body);
        const auto triangle = std::find_if(triangles.begin(), triangles.end(),
                                           [&](const auto &t) { return t.face == hit.face; });
        const bool back = (dot(cross(triangle->b - triangle->a, triangle->c - triangle->a),
                               direction) > 0) != (doc_.worldTransform(hit.body).determinant() < 0);
        const auto appearance =
            surfaceAppearance(doc_.materials(), *doc_.bodies().at(hit.body), hit.face, back);
        setPaintMaterial(appearance.material, back ? 1 : 0);
        emit message(appearance.material
                         ? "Sampled " + QString::fromStdString(
                                            doc_.materials().at(appearance.material)->name)
                         : "Sampled original face color setting");
        return;
    }
    if (!selectable({hit.body, SelectionKind::Face, hit.face}))
        throw std::runtime_error("Open the group/component and unlock the face before painting");
    commitCommands({QJsonObject{{"command", "material.assign"},
                                {"body", QString::number(hit.body)},
                                {"face", QString::number(hit.face)},
                                {"material", QString::number(paintMaterial())},
                                {"side", paintSide_ == 0   ? "front"
                                         : paintSide_ == 1 ? "back"
                                                           : "both"}}});
    refresh();
    emit changed();
    emit message("Material applied · Alt-click samples the visible side");
}
} // namespace sketchy
