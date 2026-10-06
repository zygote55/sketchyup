#include "app/unit_display.hpp"
#include "app/viewport.hpp"
#include "automation/hosted_commands.hpp"
#include "automation/measurements.hpp"
#include <algorithm>
namespace sketchy {
namespace {
QJsonArray point(Vec3 value) { return {value.x, value.y, value.z}; }
Vec3 faceAnchor(const Surface &surface, Id face) {
    const auto triangles = surface.triangulate(face);
    if (triangles.empty())
        throw std::runtime_error("Choose a nondegenerate face");
    const auto triangle = *std::max_element(
        triangles.begin(), triangles.end(), [](const Triangle &a, const Triangle &b) {
            return length(cross(a.b - a.a, a.c - a.a)) < length(cross(b.b - b.a, b.c - b.a));
        });
    return (triangle.a + triangle.b + triangle.c) * (1.0 / 3);
}
Vec3 faceTangent(const Surface &surface, Id face) {
    const auto &loop = surface.faces.at(face).loops.at(0);
    return normalized(surface.vertices.at(loop.at(1)) - surface.vertices.at(loop.at(0)));
}
} // namespace
Id Viewport::selectedComponentDefinition() const {
    return doc_.instances().at(selectedComponent())->definition;
}
ComponentGlue Viewport::selectedGlueFace() const {
    const auto scope = componentScope();
    if (!scope || selection_.entities().size() != 1)
        throw std::runtime_error("Enter a component and select one face for its glue behavior");
    const auto face = *selection_.entities().begin();
    if (face.kind != SelectionKind::Face || !selectable(face))
        throw std::runtime_error("Select one editable component face");
    const auto &instance = *doc_.instances().at(scope);
    for (const auto &[member, body] : instance.members) {
        if (body != face.body)
            continue;
        const auto &definition = *doc_.definitions().at(instance.definition);
        if (definition.references.contains(member))
            break;
        if (definition.glue && definition.glue->member == member &&
            definition.glue->face == face.entity)
            return *definition.glue;
        const auto &surface = definition.members.at(member)->surface;
        return {member, face.entity, faceAnchor(surface, face.entity),
                faceTangent(surface, face.entity), false};
    }
    throw std::runtime_error("Enter the component that directly owns the selected face");
}
void Viewport::validateHostedLocks(const QJsonObject &command) const {
    std::set<Id> bodies;
    auto placement = [&](Id root) {
        bodies.insert(root);
        if (doc_.instances().contains(root))
            for (const auto &[member, body] : doc_.instances().at(root)->members)
                bodies.insert(body);
        if (doc_.hostedComponents().attachments.contains(root))
            bodies.insert(doc_.hostedComponents().attachments.at(root)->host);
    };
    const auto name = command["command"].toString();
    if (name == "component.glue") {
        const auto definition = command["definition"].toString().toULongLong();
        for (const auto &[root, instance] : doc_.instances())
            if (instance->definition == definition)
                placement(root);
    } else if (name == "component.bake_host") {
        const auto host = command["body"].toString().toULongLong();
        bodies.insert(host);
        for (const auto &[root, attachment] : doc_.hostedComponents().attachments)
            if (attachment->host == host)
                placement(root);
    } else {
        placement(command["body"].toString().toULongLong());
        if (command.contains("host"))
            bodies.insert(command["host"].toString().toULongLong());
    }
    for (auto body : bodies)
        if (selection_.locked(doc_, body))
            throw std::runtime_error(
                "Unlock every affected component and host before changing attachments");
}
void Viewport::configureComponentGlue(std::optional<ComponentGlue> glue) {
    const auto definition = selectedComponentDefinition();
    if (doc_.definitions().at(definition)->glue == glue)
        return;
    ComponentDefinition description;
    description.glue = glue;
    const QJsonObject command{{"command", "component.glue"},
                              {"definition", QString::number(definition)},
                              {"glue", componentGlueDescription(description)}};
    validateHostedLocks(command);
    cancel();
    commitCommands({command}, false);
    refresh();
    emit changed();
    emit message("Shared glue behavior updated · Attached openings regenerate · Ctrl+Z undoes");
}
void Viewport::validateHostedTransform(const QJsonObject &command) const {
    const auto name = command["command"].toString();
    if (name != "geometry.transform_selection" && name != "geometry.array_selection")
        return;
    const bool copy = name == "geometry.array_selection" || command["copy"].toBool();
    std::set<Id> contexts;
    for (const auto &value : command["entities"].toArray()) {
        const auto target = value.toObject();
        if (target["kind"] == "context")
            contexts.insert(target["body"].toString().toULongLong());
    }
    auto covers = [&](Id body) {
        for (auto id = body; id; id = doc_.bodies().at(id)->parent)
            if (contexts.contains(id))
                return true;
        return false;
    };
    const auto scope = componentScope();
    const auto definition = scope ? doc_.instances().at(scope)->definition : 0;
    for (const auto &[root, attachment] : doc_.hostedComponents().attachments) {
        const bool shared = definition && doc_.instances().at(root)->definition == definition;
        const bool rootAffected = covers(root), hostAffected = covers(attachment->host);
        // Copying a complete host assembly changes only its new records. A
        // component-only copy edits the existing host, even for alignment only.
        const bool existingHost =
            shared || (copy ? rootAffected && !hostAffected : rootAffected || hostAffected);
        if (existingHost && (selection_.locked(doc_, attachment->host) ||
                             ((shared || !copy) && selection_.locked(doc_, root))))
            throw std::runtime_error("Unlock the affected host and attached components before "
                                     "moving or copying this selection");
    }
}
void Viewport::setHostedPlacementOptions(HostedPlacementOptions options) {
    if (!std::isfinite(options.angle) || !std::isfinite(options.inset))
        throw std::runtime_error("Enter finite rotation and inset values");
    Transform::scaling(options.scale).validate();
    cancel();
    hostedOptions_ = options;
    emit message("Placement options updated · Select a component and host face, then Shift+H");
}
void Viewport::startHostedPlacement(bool retainPose) {
    hostedRetainPose_ = retainPose;
    setTool(Tool::HostedPlacement);
    setFocus();
}
QString Viewport::hostedPlacementSummary() const {
    if (session_.canRevise())
        return "Component attached · Type " +
               (hostedRetainPose_ ? QString("an inset")
                                  : QString("anchor coordinates or an inset")) +
               " to revise · Ctrl+Z undoes";
    if (!session_.active())
        return "Select one component and one host face · Shift+H previews attachment";
    return (hostedRetainPose_ ? QString("Bind current pose") : QString("Attach on selected face")) +
           " · Inset " + displayLength(hostedOptions_.inset, doc_.displayUnits()) +
           (previewValid_ ? " · Enter applies" : " · Enter valid placement coordinates") +
           " · Esc cancels · Alt-drag orbits";
}
void Viewport::beginHostedPlacement() {
    syncSelection();
    if (componentScope())
        throw std::runtime_error(
            "Leave component editing before attaching an independent placement");
    Id root = 0, host = 0, face = 0;
    for (const auto entity : selection_.entities()) {
        if (!selectable(entity))
            throw std::runtime_error("Choose editable component and host entities");
        if (entity.kind == SelectionKind::Body && doc_.instances().contains(entity.body) && !root)
            root = entity.body;
        else if (entity.kind == SelectionKind::Face && !host) {
            host = entity.body;
            face = entity.entity;
        } else
            throw std::runtime_error(
                "Select one whole component and one host face; Ctrl-click adds");
    }
    if (!root || !host)
        throw std::runtime_error("Select one whole component and one host face; Ctrl-click adds");
    if (!doc_.definitions().at(doc_.instances().at(root)->definition)->glue)
        throw std::runtime_error(
            "Enter the component, select its glue face and choose Edit → Set glue face");
    const auto &records = doc_.hostedComponents();
    const auto &surface = records.hosts.contains(host) ? records.hosts.at(host)->uncut
                                                       : doc_.bodies().at(host)->surface;
    const auto anchor = faceAnchor(surface, face);
    QJsonObject command{{"command", hostedRetainPose_ ? "component.bind" : "component.attach"},
                        {"body", QString::number(root)},
                        {"host", QString::number(host)},
                        {"face", QString::number(face)},
                        {"inset", hostedOptions_.inset}};
    if (!hostedRetainPose_) {
        command["anchor"] = point(anchor);
        command["tangent"] = point(faceTangent(surface, face));
        command["angle"] = hostedOptions_.angle;
        command["scale"] = point(hostedOptions_.scale);
    }
    validateHostedLocks(command);
    hostedCommand_ = command;
    session_.begin();
    anchor_ = doc_.worldTransform(host).point(anchor);
    previewCommand(command);
    if (previewValid_)
        emit message(hostedPlacementSummary());
}
void Viewport::updateHostedPlacement(QPointF position) {
    if (!session_.active() || !hostedCommand_ || hostedRetainPose_)
        return;
    try {
        validateHostedLocks(*hostedCommand_);
        if (!session_.current())
            throw std::runtime_error("Document changed; restart attachment placement");
        const auto host = (*hostedCommand_)["host"].toString().toULongLong();
        const auto face = (*hostedCommand_)["face"].toString().toULongLong();
        const auto &records = doc_.hostedComponents();
        const auto &surface = records.hosts.contains(host) ? records.hosts.at(host)->uncut
                                                           : doc_.bodies().at(host)->surface;
        const auto normal = surface.normal(face);
        const auto origin = surface.vertices.at(surface.faces.at(face).loops.at(0).at(0));
        const auto [rayOrigin, rayDirection] = ray(position);
        const auto inverse = doc_.worldTransform(host).inverse();
        const auto localOrigin = inverse.point(rayOrigin),
                   localDirection = inverse.vector(rayDirection);
        const auto denominator = dot(normal, localDirection);
        if (std::abs(denominator) < 1e-9 * length(localDirection))
            throw std::runtime_error(
                "Orbit to view the selected face or enter its anchor coordinates");
        const auto distance = dot(normal, origin - localOrigin) / denominator;
        if (distance <= 0)
            throw std::runtime_error("The selected face plane is behind the camera");
        const auto anchor = localOrigin + localDirection * distance;
        const auto encoded = point(anchor);
        if ((*hostedCommand_)["anchor"].toArray() == encoded && previewValid_)
            return;
        (*hostedCommand_)["anchor"] = encoded;
        previewCommand(*hostedCommand_);
        if (previewValid_)
            emit message(hostedPlacementSummary());
    } catch (const std::exception &error) {
        previewValid_ = false;
        previewEdges_.clear();
        previewError_ = error.what();
        emit message(previewError_);
        update();
    }
}
void Viewport::finishHostedPlacement() {
    if (!hostedCommand_ || (!session_.active() && !session_.canRevise()))
        throw std::runtime_error("Select a component and host face, then Shift+H to preview");
    if (!previewValid_)
        throw std::runtime_error(previewError_.isEmpty()
                                     ? "Enter anchor coordinates or an inset to revise placement"
                                     : previewError_.toStdString());
    validateHostedLocks(*hostedCommand_);
    const auto root = (*hostedCommand_)["body"].toString().toULongLong();
    session_.commit(*hostedCommand_);
    clearPreview();
    refresh();
    selectEntities({{root, SelectionKind::Body, 0}});
    emit changed();
    emit message(hostedPlacementSummary());
}
bool Viewport::hostedMeasurements(const QString &text) {
    if (!hostedCommand_ || (!session_.active() && !session_.canRevise()))
        throw std::runtime_error("Start an attachment preview before entering its anchor or inset");
    const auto input = parseMeasurements(text, inputUnit(doc_.displayUnits()), QLocale());
    auto command = *hostedCommand_;
    if (input.kind == MeasurementKind::Values && input.values.size() == 1)
        command["inset"] = input.values[0];
    else if (!hostedRetainPose_ &&
             (input.kind == MeasurementKind::Values ||
              input.kind == MeasurementKind::AbsolutePoint) &&
             input.values.size() == 3)
        command["anchor"] = QJsonArray{input.values[0], input.values[1], input.values[2]};
    else
        throw std::runtime_error("Enter one inset length or three host-local anchor coordinates");
    validateHostedLocks(command);
    previewCommand(command);
    if (!previewValid_)
        throw std::runtime_error(previewError_.toStdString());
    hostedCommand_ = command;
    finishHostedPlacement();
    hostedOptions_.inset = command["inset"].toDouble();
    return true;
}
void Viewport::detachSelectedComponent() {
    const QJsonObject command{{"command", "component.detach"},
                              {"body", QString::number(selectedComponent())}};
    validateHostedLocks(command);
    cancel();
    commitCommands({command}, false);
    refresh();
    emit changed();
    emit message("Component detached · Its opening is restored · Ctrl+Z undoes");
}
void Viewport::bakeSelectedHost() {
    if (selection_.entities().size() != 1)
        throw std::runtime_error("Select one host body or face");
    const auto entity = *selection_.entities().begin();
    if ((entity.kind != SelectionKind::Body && entity.kind != SelectionKind::Face) ||
        !selectable(entity))
        throw std::runtime_error("Select one editable host body or face");
    const QJsonObject command{{"command", "component.bake_host"},
                              {"body", QString::number(entity.body)}};
    validateHostedLocks(command);
    cancel();
    commitCommands({command}, false);
    refresh();
    emit changed();
    emit message("Host geometry baked · Attachments released · Ctrl+Z restores the relationships");
}
} // namespace sketchy
