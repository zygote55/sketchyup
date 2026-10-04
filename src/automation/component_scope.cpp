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
} // namespace sketchy
