#include "app/viewport.hpp"
#include "automation/commands.hpp"
#include <algorithm>
namespace sketchy {
namespace {
QJsonObject commit(Document &doc, const QJsonArray &commands) {
    return executeBatch(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", commands}});
}
QJsonArray entities(const Selection &selection) {
    QJsonArray result;
    for (auto entity : selection.entities()) {
        const char *kind = entity.kind == SelectionKind::Body   ? "context"
                           : entity.kind == SelectionKind::Face ? "face"
                           : entity.kind == SelectionKind::Edge ? "edge"
                                                                : "guide";
        result.append(QJsonObject{{"body", QString::number(entity.body)},
                                  {"kind", kind},
                                  {"entity", QString::number(entity.entity)}});
    }
    return result;
}
QJsonObject mergeCommand(const Document &doc, const Selection &selection) {
    QJsonArray members;
    std::set<Id> hiddenParts;
    for (auto entity : selection.hiddenEntities())
        hiddenParts.insert(entity.body);
    for (const auto &[id, body] : doc.bodies()) {
        if (selection.inContext(doc, id) && !selection.locked(doc, id) &&
            !hiddenParts.contains(id) && !selection.hidden(doc, {id, SelectionKind::Body, 0}))
            members.append(QString::number(id));
    }
    return {{"command", "geometry.merge_context"},
            {"context", QString::number(selection.context())},
            {"members", members}};
}
} // namespace
void Viewport::makeGroup() {
    cancel();
    selection_.sync(doc_);
    const auto result =
        commit(doc_, {QJsonObject{{"command", "group.selection"},
                                  {"context", QString::number(selection_.context())},
                                  {"showHidden", selection_.showingHidden()},
                                  {"entities", entities(selection_)}}});
    Id group = 0;
    for (const auto &created : result["created"].toArray()) {
        const auto id = created.toString().toULongLong();
        if (doc_.bodies().at(id)->kind == BodyKind::Group) {
            group = id;
            break;
        }
    }
    refresh();
    selectEntities({{group, SelectionKind::Body, 0}});
    emit changed();
    emit message("Group created · Enter or double-click to edit · Esc closes one level");
}
void Viewport::explodeGroups() {
    cancel();
    selection_.sync(doc_);
    QJsonArray commands;
    std::set<Id> groups;
    SelectionSet promoted;
    for (auto entity : selection_.entities()) {
        if (entity.kind != SelectionKind::Body ||
            doc_.bodies().at(entity.body)->kind != BodyKind::Group)
            throw std::runtime_error("Select whole groups to explode");
        groups.insert(entity.body);
        commands.append(QJsonObject{{"command", "group.explode"},
                                    {"body", QString::number(entity.body)},
                                    {"merge", false}});
    }
    if (commands.empty())
        throw std::runtime_error("Select a group to explode");
    for (const auto &[id, body] : doc_.bodies())
        if (groups.contains(id) || groups.contains(body->parent))
            promoted.insert({id, SelectionKind::Body, 0});
    // Evaluate the promoted hierarchy privately to preserve temporary editor
    // hiding/locking as well as the persistent policy used by the public command.
    Document preview = doc_;
    commit(preview, commands);
    auto policy = selection_;
    policy.sync(preview);
    commands.append(mergeCommand(preview, policy));
    const auto result = commit(doc_, commands);
    for (const auto &value : result["transfers"].toArray()) {
        const auto transfer = value.toObject();
        const auto source = transfer["sourceBody"].toString().toULongLong();
        if (!promoted.erase({source, SelectionKind::Body, 0}))
            continue;
        const auto target = transfer["body"].toString().toULongLong();
        if (target != policy.context())
            promoted.insert({target, SelectionKind::Body, 0});
        else
            for (auto [key, kind] :
                 {std::pair{"faces", SelectionKind::Face}, std::pair{"edges", SelectionKind::Edge},
                  std::pair{"guides", SelectionKind::Guide}}) {
                const auto mapping = transfer[key].toObject();
                for (auto it = mapping.begin(); it != mapping.end(); ++it)
                    promoted.insert({target, kind, it.value().toString().toULongLong()});
            }
    }
    refresh();
    selectEntities(promoted);
    emit changed();
    emit message("Group exploded · Raw geometry merged with appearance and placement preserved");
}
void Viewport::mergeContextGeometry() {
    cancel();
    selection_.sync(doc_);
    commit(doc_, {mergeCommand(doc_, selection_)});
    refresh();
    selectEntities({});
    emit changed();
    emit message("Raw geometry merged in the current editing context");
}
void Viewport::setPersistentState(bool hide, bool lock) {
    cancel();
    selection_.sync(doc_);
    QJsonArray commands;
    std::set<Id> bodies;
    for (auto entity : selection_.entities()) {
        if (entity.kind != SelectionKind::Body)
            throw std::runtime_error("Select whole groups or contexts for persistent hide/lock");
        bodies.insert(entity.body);
    }
    for (auto id : bodies) {
        QJsonObject command{{"command", "scene.state"}, {"body", QString::number(id)}};
        if (hide)
            command["hidden"] = true;
        if (lock)
            command["locked"] = true;
        commands.append(command);
    }
    if (commands.empty())
        throw std::runtime_error("Select a whole group or context first");
    commit(doc_, commands);
    selectionChanged(true);
    emit changed();
}
void Viewport::revealPersistentEntities() {
    QJsonArray commands;
    for (const auto &[id, body] : doc_.bodies())
        if (body->hidden)
            commands.append(QJsonObject{
                {"command", "scene.state"}, {"body", QString::number(id)}, {"hidden", false}});
    if (commands.empty())
        return;
    cancel();
    commit(doc_, commands);
    selectionChanged(true);
    emit changed();
}
void Viewport::unlockPersistentEntities() {
    QJsonArray commands;
    for (const auto &[id, body] : doc_.bodies())
        if (body->locked)
            commands.append(QJsonObject{
                {"command", "scene.state"}, {"body", QString::number(id)}, {"locked", false}});
    if (commands.empty())
        return;
    cancel();
    commit(doc_, commands);
    selectionChanged(true);
    emit changed();
}
} // namespace sketchy
