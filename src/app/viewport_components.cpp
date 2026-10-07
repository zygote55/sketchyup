#include "app/viewport.hpp"
#include "automation/component_scope.hpp"
#include "io/component_library.hpp"
namespace sketchy {
Id Viewport::componentScope() const {
    for (auto context = selection_.context(); context && doc_.bodies().contains(context);
         context = doc_.bodies().at(context)->parent)
        if (doc_.instances().contains(context))
            return context;
    return 0;
}
QJsonObject Viewport::commitCommands(const QJsonArray &commands, bool shared) {
    const auto scope = shared ? componentScope() : 0;
    const auto result = executeBatch(
        doc_, {{"apiVersion", 1},
               {"documentId", QString::fromStdString(doc_.identity())},
               {"expectedRevision", QString::number(doc_.revision())},
               {"commands",
                scope ? QJsonArray{componentScopeCommand(doc_, scope, commands)} : commands}});
    return componentScopeResult(result, scope);
}
void Viewport::applyTextEdit(const Document::PreparedEdit &edit) {
    if (!doc_.canApply(edit)) throw std::runtime_error("The model changed while text was being generated. Reopen the editor to continue.");
    cancel();
    doc_.applyPrepared(edit);
    refresh();
    emit changed();
}
void Viewport::organize(const QJsonArray &commands) {
    const auto scope = componentScope();
    bool globalTags = false, entities = false, root = false, members = false;
    for (auto value : commands) {
        const auto command = value.toObject();
        const auto name = command.value("command").toString();
        if (name.startsWith("tag.") && name != "tag.assign") {
            globalTags = true;
        } else {
            if (name != "tag.assign" && name != "scene.rename" && name != "scene.state" &&
                name != "scene.reparent" && name != "entity.position" &&
                name != "entity.dimensions" && name != "entity.properties")
                throw std::runtime_error("Unsupported organization operation");
            entities = true;
            const auto body = command.value("body").toString().toULongLong();
            root |= scope && body == scope;
            members |= scope && body != scope;
        }
    }
    if ((globalTags && entities) || (root && members))
        throw std::runtime_error("Edit placement state and shared members separately");
    cancel();
    commitCommands(commands, !globalTags && !root);
    refresh();
    emit changed();
}
Id Viewport::selectedComponent() const {
    if (selection_.entities().size() == 1) {
        const auto entity = *selection_.entities().begin();
        if (entity.kind == SelectionKind::Body && doc_.instances().contains(entity.body))
            return entity.body;
    }
    if (const auto scope = componentScope())
        return scope;
    throw std::runtime_error("Select a whole component or open a component to edit");
}
void Viewport::makeComponent(const QString &name) {
    cancel();
    selection_.sync(doc_);
    Id root = 0;
    if (selection_.entities().size() == 1 &&
        selection_.entities().begin()->kind == SelectionKind::Body &&
        !doc_.instances().contains(selection_.entities().begin()->body)) {
        root = selection_.entities().begin()->body;
        commitCommands({QJsonObject{
            {"command", "component.create"}, {"body", QString::number(root)}, {"name", name}}});
    } else {
        QJsonArray entities;
        for (auto entity : selection_.entities()) {
            const char *kind = entity.kind == SelectionKind::Body   ? "context"
                               : entity.kind == SelectionKind::Face ? "face"
                               : entity.kind == SelectionKind::Edge ? "edge"
                                                                    : "guide";
            entities.append(QJsonObject{{"body", QString::number(entity.body)},
                                        {"kind", kind},
                                        {"entity", QString::number(entity.entity)}});
        }
        const auto result =
            commitCommands({QJsonObject{{"command", "component.selection"},
                                        {"name", name},
                                        {"context", QString::number(selection_.context())},
                                        {"showHidden", selection_.showingHidden()},
                                        {"entities", entities}}});
        for (auto value : result["created"].toArray()) {
            const auto id = value.toString().toULongLong();
            if (doc_.instances().contains(id)) {
                root = id;
                break;
            }
        }
    }
    refresh();
    selectEntities({{root, SelectionKind::Body, 0}});
    emit changed();
    emit message("Component created · Enter to edit its shared definition");
}
void Viewport::makeComponentUnique(bool activeScope) {
    const auto root = activeScope && componentScope() ? componentScope() : selectedComponent();
    cancel();
    commitCommands(
        {QJsonObject{{"command", "component.make_unique"}, {"body", QString::number(root)}}},
        false);
    refresh();
    emit changed();
    emit message("Component made unique · Other placements keep their original definition");
}
void Viewport::replaceComponent(Id definition) {
    const auto root = selectedComponent();
    if (doc_.instances().at(root)->definition == definition)
        return;
    cancel();
    commitCommands({QJsonObject{{"command", "component.replace"},
                                {"body", QString::number(root)},
                                {"definition", QString::number(definition)}}},
                   false);
    refresh();
    selectionChanged(true);
    emit changed();
    emit message("Component replaced · Placement preserved");
}
void Viewport::placeComponent(Id definition, Vec3 position) {
    cancel();
    const auto parent = selection_.context();
    const auto local = parent ? doc_.worldTransform(parent).inverse().point(position) : position;
    QJsonArray matrix;
    for (auto value : Transform::translation(local).m)
        matrix.append(value);
    const auto result = commitCommands({QJsonObject{{"command", "component.instance"},
                                                    {"definition", QString::number(definition)},
                                                    {"parent", QString::number(parent)},
                                                    {"matrix", matrix}}});
    Id root = 0;
    for (auto value : result["created"].toArray()) {
        const auto id = value.toString().toULongLong();
        if (doc_.instances().contains(id)) {
            root = id;
            break;
        }
    }
    refresh();
    selectEntities({{root, SelectionKind::Body, 0}});
    emit changed();
    emit message("Component placed · Copies share this definition");
}
void Viewport::changeComponentAxes(Transform axes) {
    const auto root = selectedComponent();
    if (axes == Transform{})
        return;
    cancel();
    QJsonArray matrix;
    for (auto value : axes.m)
        matrix.append(value);
    commitCommands(
        {QJsonObject{{"command", "component.axes"},
                     {"definition", QString::number(doc_.instances().at(root)->definition)},
                     {"matrix", matrix}}},
        false);
    refresh();
    emit changed();
    emit message("Component origin changed · World geometry preserved in every placement");
}
void Viewport::paintSelection(std::array<float, 3> color) {
    if (!selected_)
        return;
    cancel();
    commitCommands({QJsonObject{{"command", "material.color"},
                                {"body", QString::number(selected_)},
                                {"color", QJsonArray{color[0], color[1], color[2]}}}});
    refresh();
    emit changed();
}
void Viewport::insertLibrary(const ComponentBundle &bundle, Vec3 worldPosition) {
    if (componentScope())
        throw std::runtime_error("Close component editing before inserting a library component");
    const auto parent = selection_.context();
    const auto local =
        parent ? doc_.worldTransform(parent).inverse().point(worldPosition) : worldPosition;
    cancel();
    const auto result = insertLibraryComponent(doc_, bundle, Transform::translation(local), parent);
    refresh();
    selectEntities({{result.component.instance, SelectionKind::Body, 0}});
    emit changed();
    emit message(QString("Inserted independent component · %1 resources reused · %2 names adjusted")
                     .arg(result.reusedAssets + result.reusedMaterials)
                     .arg(result.renamedResources));
}
} // namespace sketchy
