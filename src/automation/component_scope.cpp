#include "automation/component_scope.hpp"
#include "core/components.hpp"
namespace sketchy {
QJsonObject componentScopeCommand(const Document &doc, Id instance, const QJsonArray &commands) {
    return {{"command", "component.edit"},
            {"definition", QString::number(doc.instances().at(instance)->definition)},
            {"instance", QString::number(instance)},
            {"commands", commands}};
}
QJsonArray canonicalComponentCommands(const Document &doc, const Document &draft, Id instance,
                                      const QJsonArray &commands) {
    std::map<QString, QString> members{{"0", "0"}};
    for (auto [canonical, scene] : componentScopeMembers(doc, draft, instance))
        members[QString::number(scene)] = QString::number(canonical);
    auto mapped = [&](const QJsonValue &value) -> QString {
        if (!value.isString() || !members.contains(value.toString()))
            throw std::runtime_error(
                "The command targets geometry outside this component's edit scope");
        return members.at(value.toString());
    };
    QJsonArray result;
    for (auto value : commands) {
        if (!value.isObject())
            throw std::runtime_error("Expected component command object");
        auto command = value.toObject();
        for (const auto &key : {"body", "context", "parent"})
            if (command.contains(key))
                command[key] = mapped(command.value(key));
        if ((command["command"] == "geometry.boolean" || command["command"] == "geometry.trim" ||
             command["command"] == "geometry.split" ||
             command["command"] == "geometry.outer_shell") &&
            command.contains("tool"))
            command["tool"] = mapped(command.value("tool"));
        if (command.contains("members")) {
            if (!command["members"].isArray())
                throw std::runtime_error("Expected member array");
            QJsonArray ids;
            for (auto id : command["members"].toArray())
                ids.append(mapped(id));
            command["members"] = ids;
        }
        if (command.contains("entities")) {
            if (!command["entities"].isArray())
                throw std::runtime_error("Expected entity array");
            QJsonArray entities;
            for (auto entity : command["entities"].toArray()) {
                auto record = entity.toObject();
                record["body"] = mapped(record.value("body"));
                entities.append(record);
            }
            command["entities"] = entities;
        }
        result.append(command);
    }
    return result;
}
QJsonObject componentScopeResult(QJsonObject result, Id instance) {
    if (!instance)
        return result;
    for (auto value : result["componentOperations"].toArray()) {
        const auto operation = value.toObject();
        if (operation["instance"].toString() == QString::number(instance) &&
            operation.contains("created")) {
            result["created"] = operation["created"];
            break;
        }
    }
    return result;
}
void rejectScopedParameterCommands(const QJsonArray &commands, Id scopedDefinition) {
    for (const auto &value : commands) {
        const auto command = value.toObject();
        const auto name = command.value("command").toString();
        if (!name.startsWith(QStringLiteral("component_parameter")))
            continue;
        bool ok{};
        const auto named = command.value("definition").toString().toULongLong(&ok);
        if (!ok || named != scopedDefinition)
            throw std::runtime_error("Edit another definition's parameters, or a placement's "
                                     "values, outside this component's edit scope");
        throw std::runtime_error("Change the scoped definition's parameters in the outer batch, "
                                 "beside the template edit");
    }
}
std::set<Id> scopedCommandRecords(const QJsonArray &commands) {
    std::set<Id> result;
    auto add = [&](const QJsonValue &value) {
        bool ok{};
        const auto id = value.toString().toULongLong(&ok);
        if (value.isString() && ok && id)
            result.insert(id);
    };
    for (const auto &value : commands) {
        const auto command = value.toObject();
        for (const auto *key : {"body", "context", "parent", "tool"})
            if (command.contains(key))
                add(command.value(key));
        for (const auto &member : command.value("members").toArray())
            add(member);
        for (const auto &entity : command.value("entities").toArray())
            add(entity.toObject().value("body"));
    }
    return result;
}
} // namespace sketchy
