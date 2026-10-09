#include "automation/extension.hpp"
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void fields(const QJsonObject &object, const QStringList &names) {
    require(object.size() == names.size(), "Extension object has missing or extra fields");
    for (const auto &name : names)
        require(object.contains(name), "Extension field missing");
}
QString text(const QJsonValue &value, qsizetype limit, bool allowEmpty = false) {
    require(value.isString(), "Extension metadata must be text");
    const auto string = value.toString();
    require((allowEmpty || !string.trimmed().isEmpty()) && string.toUtf8().size() <= limit &&
                QString::fromUtf8(string.toUtf8()) == string,
            "Invalid or oversized extension text");
    for (auto c : string)
        require(c.unicode() >= 32 && c.unicode() != 127, "Extension metadata contains controls");
    return string;
}
QString identifier(const QJsonValue &value) {
    const auto id = text(value, 64);
    require(QRegularExpression("^[a-z][a-z0-9_-]{0,63}$").match(id).hasMatch(),
            "Invalid extension action or parameter identity");
    return id;
}
void validateValue(const QJsonValue &value, const QJsonObject &spec) {
    const auto type = spec["type"].toString();
    if (type == "number")
        require(value.isDouble() && std::isfinite(value.toDouble()) &&
                    value.toDouble() >= spec["minimum"].toDouble() &&
                    value.toDouble() <= spec["maximum"].toDouble(),
                "Extension numeric parameter is outside its declared bounds");
    else if (type == "boolean")
        require(value.isBool(), "Extension parameter must be boolean");
    else if (type == "string")
        (void)text(value, 256, true);
    else
        throw std::runtime_error("Unsupported extension parameter type");
}
std::map<QString, QJsonObject> parameters(const QJsonValue &value) {
    require(value.isArray() && value.toArray().size() <= 32,
            "Extension actions support at most 32 parameters");
    std::map<QString, QJsonObject> result;
    for (const auto &item : value.toArray()) {
        require(item.isObject(), "Extension parameter must be an object");
        const auto spec = item.toObject();
        const auto name = identifier(spec["name"]);
        const auto type = spec["type"].toString();
        fields(spec, type == "number"
                         ? QStringList{"name", "label", "type", "default", "minimum", "maximum"}
                         : QStringList{"name", "label", "type", "default"});
        text(spec["label"], 128);
        if (type == "number")
            require(spec["minimum"].isDouble() && spec["maximum"].isDouble() &&
                        std::isfinite(spec["minimum"].toDouble()) &&
                        std::isfinite(spec["maximum"].toDouble()) &&
                        spec["minimum"].toDouble() <= spec["maximum"].toDouble(),
                    "Invalid extension parameter range");
        validateValue(spec["default"], spec);
        require(result.emplace(name, spec).second, "Duplicate extension parameter");
    }
    return result;
}
QJsonValue expand(const QJsonValue &value, const std::map<QString, QJsonValue> &values,
                  const std::set<QString> &allowed, size_t &nodes, unsigned depth = 0) {
    require(++nodes <= 10000 && depth <= 32, "Extension action exceeds structural limits");
    if (value.isObject()) {
        const auto object = value.toObject();
        if (object.contains("$parameter")) {
            fields(object, {"$parameter"});
            const auto name = identifier(object["$parameter"]);
            require(values.contains(name), "Extension action references an undeclared parameter");
            return values.at(name);
        }
        if (object.contains("command")) {
            require(object["command"].isString() && allowed.contains(object["command"].toString()),
                    "Extension action uses an undeclared or dynamic command");
        }
        QJsonObject result;
        for (auto it = object.begin(); it != object.end(); ++it)
            result[it.key()] = expand(it.value(), values, allowed, nodes, depth + 1);
        return result;
    }
    if (value.isArray()) {
        QJsonArray result;
        for (const auto &item : value.toArray())
            result.append(expand(item, values, allowed, nodes, depth + 1));
        return result;
    }
    return value;
}
QJsonArray resolve(const QJsonObject &action, const QJsonObject &input,
                   const std::set<QString> &allowed) {
    const auto specs = parameters(action["parameters"]);
    std::map<QString, QJsonValue> values;
    for (auto it = input.begin(); it != input.end(); ++it)
        require(specs.contains(it.key()), "Unknown extension parameter");
    for (const auto &[name, spec] : specs) {
        const auto value = input.contains(name) ? input[name] : spec["default"];
        validateValue(value, spec);
        values[name] = value;
    }
    size_t nodes{};
    const auto commands = expand(action["commands"], values, allowed, nodes).toArray();
    require(!commands.empty() && commands.size() <= 100,
            "Extension action requires 1–100 commands");
    for (const auto &command : commands)
        require(command.isObject() && command.toObject()["command"].isString(),
                "Extension batch items require a command name");
    require(QJsonDocument(commands).toJson(QJsonDocument::Compact).size() <= 65536,
            "Expanded extension action exceeds 64 KiB");
    return commands;
}
} // namespace
ExtensionManifest parseExtensionManifest(const QByteArray &bytes) {
    require(!bytes.isEmpty() && bytes.size() <= extensionManifestLimit,
            "Extension manifest exceeds 256 KiB");
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    require(error.error == QJsonParseError::NoError && json.isObject(),
            "Invalid extension manifest JSON");
    const auto object = json.object();
    fields(object, {"manifestVersion", "commandApiVersion", "execution", "id", "name", "version",
                    "description", "capabilities", "actions"});
    require(object["manifestVersion"] == 1 && object["commandApiVersion"] == 1 &&
                object["execution"] == "command-batch-v1",
            "EXTENSION_INCOMPATIBLE: unsupported manifest, command API or execution model");
    ExtensionManifest result;
    result.id = text(object["id"], 128);
    require(
        QRegularExpression("^[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*)+$").match(result.id).hasMatch(),
        "Extension identity must be a lowercase reverse-domain name");
    result.name = text(object["name"], 256);
    result.version = text(object["version"], 32);
    require(QRegularExpression("^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$")
                .match(result.version)
                .hasMatch(),
            "Extension version must be three bounded numeric components");
    result.description = text(object["description"], 4096, true);
    require(object["capabilities"].isObject(), "Extension capabilities must be an object");
    const auto caps = object["capabilities"].toObject();
    fields(caps, {"commands"});
    require(caps["commands"].isArray() && !caps["commands"].toArray().empty() &&
                caps["commands"].toArray().size() <= 100,
            "Extension must declare 1–100 command capabilities");
    std::set<QString> available, allowed;
    for (const auto &command : commandCatalog())
        available.insert(command.toObject()["name"].toString());
    for (const auto &command : caps["commands"].toArray()) {
        const auto name = text(command, 128);
        require(available.contains(name),
                "EXTENSION_INCOMPATIBLE: required command is unavailable");
        require(allowed.insert(name).second, "Duplicate extension command capability");
        result.commands.append(name);
    }
    require(object["actions"].isArray() && !object["actions"].toArray().empty() &&
                object["actions"].toArray().size() <= 32,
            "Extension must provide 1–32 actions");
    std::set<QString> actionIds;
    for (const auto &item : object["actions"].toArray()) {
        require(item.isObject(), "Extension action must be an object");
        const auto action = item.toObject();
        fields(action, {"id", "name", "description", "parameters", "commands"});
        require(actionIds.insert(identifier(action["id"])).second,
                "Duplicate extension action identity");
        text(action["name"], 128);
        text(action["description"], 1024, true);
        require(action["commands"].isArray(), "Extension action commands must be an array");
        (void)resolve(action, {}, allowed);
        result.actions.append(action);
    }
    result.source = bytes;
    return result;
}
QJsonArray resolveExtensionAction(const ExtensionManifest &manifest, const QString &id,
                                  const QJsonObject &input) {
    // Public structs can be caller-constructed. The retained source is authoritative.
    const auto verified = parseExtensionManifest(manifest.source);
    for (const auto &value : verified.actions) {
        const auto action = value.toObject();
        if (action["id"] == id)
            return resolve(action, input,
                           std::set<QString>(verified.commands.begin(), verified.commands.end()));
    }
    throw std::runtime_error("Extension action is unavailable");
}
QJsonObject extensionCapabilities() {
    return {{"manifestVersion", 1},
            {"commandApiVersion", 1},
            {"execution", "command-batch-v1"},
            {"parameterTypes", QJsonArray{"number", "boolean", "string"}},
            {"atomicEdits", true},
            {"externalCode", false},
            {"fileAccess", false},
            {"networkAccess", false},
            {"manifestBytes", extensionManifestLimit},
            {"actions", 32},
            {"parameters", 32},
            {"commandsPerAction", 100},
            {"expandedBytes", 65536},
            {"depth", 32}};
}
} // namespace sketchy
