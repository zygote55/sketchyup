#include "automation/component_parameter_commands.hpp"
#include "automation/inspection.hpp"
#include "core/component_parameters.hpp"
#include "core/component_regeneration.hpp"
#include "core/drawing_sheet_records.hpp"
#include "io/drawing_items.hpp"
#include <QJsonValue>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace sketchy {
namespace {
using Context = ComponentParameterErrorContext;
[[noreturn]] void refuse(const char *code, const std::string &message) {
    throw InspectionError(code, message);
}
Id decimal(const QJsonValue &value) {
    bool ok{};
    const auto n = value.toString().toULongLong(&ok);
    if (!value.isString() || !ok || !n || QString::number(n) != value.toString())
        throw std::runtime_error("Expected canonical uint64 decimal string");
    return n;
}
std::string text(const QJsonValue &value, const char *what) {
    if (!value.isString())
        throw std::runtime_error(std::string("Component parameter ") + what + " must be a string");
    return value.toString().toStdString();
}
double finite(const QJsonValue &value, const char *what) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error(std::string("Component parameter ") + what +
                                 " must be a finite number");
    return value.toDouble();
}
void onlyFields(const QJsonObject &object, std::initializer_list<const char *> allowed,
                const char *what) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(),
                         [&](const char *key) { return it.key() == key; }))
            throw std::runtime_error(std::string("Unknown ") + what + " field");
}
ParameterType parseType(const QJsonValue &value) {
    const auto name = value.toString();
    for (auto type : {ParameterType::Length, ParameterType::Area, ParameterType::Angle,
                      ParameterType::Number, ParameterType::Count, ParameterType::Boolean,
                      ParameterType::Choice})
        if (name == QString::fromLatin1(parameterTypeName(type).data(),
                                        qsizetype(parameterTypeName(type).size())))
            return type;
    throw std::runtime_error("Unknown component parameter type");
}
QString typeName(ParameterType type) {
    const auto name = parameterTypeName(type);
    return QString::fromLatin1(name.data(), qsizetype(name.size()));
}
ComponentParameterMode parseMode(const QJsonValue &value) {
    if (value == "input")
        return ComponentParameterMode::Input;
    if (value == "derived")
        return ComponentParameterMode::Derived;
    throw std::runtime_error("Component parameter mode must be input or derived");
}
ComponentAxis parseAxis(const QJsonValue &value) {
    if (value == "x")
        return ComponentAxis::X;
    if (value == "y")
        return ComponentAxis::Y;
    if (value == "z")
        return ComponentAxis::Z;
    throw std::runtime_error("Component driver axis must be x, y or z");
}
ComponentDriverKind parseKind(const QJsonValue &value) {
    for (auto kind : {ComponentDriverKind::Stretch, ComponentDriverKind::Position,
                      ComponentDriverKind::Rotation, ComponentDriverKind::Visibility,
                      ComponentDriverKind::Repeat, ComponentDriverKind::ChildParameter}) {
        const auto name = componentDriverKindName(kind);
        if (value == QString::fromLatin1(name.data(), qsizetype(name.size())))
            return kind;
    }
    throw std::runtime_error("Unknown component driver kind");
}
std::optional<std::size_t> position(const QJsonObject &command) {
    if (!command.contains("position"))
        return {};
    const auto value = finite(command["position"], "position");
    if (value < 0 || value > double(ComponentParameterLimits::parameters) ||
        std::floor(value) != value)
        throw std::runtime_error("Component parameter position must be a whole number 0-128");
    return std::size_t(value);
}
std::vector<ComponentParameterOption> options(const QJsonValue &value) {
    if (!value.isArray())
        throw std::runtime_error("Component parameter options must be an array");
    std::vector<ComponentParameterOption> result;
    for (const auto &entry : value.toArray()) {
        if (!entry.isObject())
            throw std::runtime_error("Component parameter option must be an object");
        const auto option = entry.toObject();
        onlyFields(option, {"value", "label"}, "component parameter option");
        result.push_back({text(option["value"], "option value"), text(option["label"], "option label")});
    }
    return result;
}
// A bound field: a number sets it; null clears it (update only).
void bound(const QJsonObject &command, const char *key, std::optional<double> &value, bool *clear) {
    if (!command.contains(key))
        return;
    if (command[key].isNull()) {
        if (!clear)
            throw std::runtime_error("Component parameter bounds must be numbers");
        *clear = true;
        return;
    }
    value = finite(command[key], key);
}
const ComponentParameter *parameterNamed(const Document &doc, Id definition,
                                         const std::string &name) {
    const auto found = doc.definitions().find(definition);
    if (found == doc.definitions().end() || !found->second->parameters)
        return nullptr;
    for (const auto &parameter : found->second->parameters->parameters)
        if (parameter.name == name)
            return &parameter;
    return nullptr;
}
bool quantityType(ParameterType type) {
    return type != ParameterType::Boolean && type != ParameterType::Choice;
}
// ADR 0165 §2 "Override literals": a JSON number in SI units, a string holding one
// signed quantity literal ("1200 mm"), a JSON boolean or a choice value. A unitless
// literal is read in SI units, like a JSON number.
ParameterValue overrideLiteral(const QJsonValue &value, const ComponentParameter *parameter,
                               const Context &context) {
    const auto dimension = parameter && quantityType(parameter->type)
                               ? parameterDimension(parameter->type)
                               : ParameterDimension{};
    if (value.isBool())
        return value.toBool();
    if (value.isDouble()) {
        if (!std::isfinite(value.toDouble()))
            throw ComponentParameterError("PARAMETER_DOMAIN", "Override values must be finite",
                                          context);
        return ParameterQuantity{value.toDouble(), dimension};
    }
    if (!value.isString())
        throw ComponentParameterError("PARAMETER_TYPE",
                                      "Override values are numbers, quantity strings, booleans "
                                      "or choice values",
                                      context);
    const auto literal = value.toString().toStdString();
    if (parameter && parameter->type == ParameterType::Choice)
        return ParameterChoice{literal};
    ParameterExpression expression;
    try {
        expression = parseParameterExpression(literal);
    } catch (const ParameterExpressionError &error) {
        throwParameterExpressionError(error, context);
    }
    const auto &nodes = expression.nodes;
    const bool single = nodes.size() == 1 && nodes[0].op == ParameterOp::Number;
    const bool sign = nodes.size() == 2 && nodes[0].op == ParameterOp::Number &&
                      (nodes[1].op == ParameterOp::Negate || nodes[1].op == ParameterOp::Plus);
    if (!single && !sign) {
        auto failure = context;
        failure.offset = 1;
        failure.excerpt = literal.substr(0, 64);
        throw ComponentParameterError(
            "PARAMETER_SYNTAX",
            "Override " + quotedParameterText(literal) +
                " must be one signed quantity literal such as \"1200 mm\"",
            failure);
    }
    const auto &number = nodes[0];
    ParameterDimension unit = dimension;
    switch (number.unit) {
    case ParameterUnit::None:
        break;
    case ParameterUnit::Mm:
    case ParameterUnit::Cm:
    case ParameterUnit::M:
    case ParameterUnit::In:
    case ParameterUnit::Ft:
        unit = {1, 0};
        break;
    case ParameterUnit::Deg:
    case ParameterUnit::Rad:
        unit = {0, 1};
        break;
    }
    const auto si = sign && nodes[1].op == ParameterOp::Negate ? -number.si : number.si;
    return ParameterQuantity{si, unit};
}
Id definitionOf(const Document &doc, Id placement) {
    const auto found = doc.instances().find(placement);
    return found == doc.instances().end() ? 0 : found->second->definition;
}
std::vector<std::string> names(const QJsonValue &value) {
    if (!value.isArray())
        throw std::runtime_error("Component parameter names must be an array");
    std::vector<std::string> result;
    for (const auto &name : value.toArray())
        result.push_back(text(name, "name"));
    return result;
}
// {…, drivers} or {…, all: true}; exactly one of the two.
bool selectsAll(const QJsonObject &command, const char *list) {
    const bool all = command.contains("all");
    if (all == command.contains(list))
        throw std::runtime_error(std::string("Name either ") + list + " or all: true");
    if (all && command["all"] != true)
        throw std::runtime_error("all must be true");
    return all;
}
ComponentDriverSpec driverSpec(const QJsonObject &command) {
    ComponentDriverSpec spec;
    spec.kind = parseKind(command["kind"]);
    spec.member = decimal(command["member"]);
    spec.expression = text(command["expression"], "expression");
    if (!command["data"].isObject())
        throw std::runtime_error("Component driver data must be an object");
    const auto data = command["data"].toObject();
    auto require = [&](std::initializer_list<const char *> keys) {
        onlyFields(data, keys, "component driver data");
        for (const auto *key : keys)
            if (!data.contains(key))
                throw std::runtime_error(std::string("Component driver data needs ") + key);
    };
    switch (spec.kind) {
    case ComponentDriverKind::Stretch:
        require({"axis", "at"});
        spec.axis = parseAxis(data["axis"]);
        spec.at = finite(data["at"], "stretch plane");
        break;
    case ComponentDriverKind::Position:
    case ComponentDriverKind::Rotation:
        require({"axis"});
        spec.axis = parseAxis(data["axis"]);
        break;
    case ComponentDriverKind::Visibility:
        require({});
        break;
    case ComponentDriverKind::Repeat: {
        require({"axis", "maxCount", "spacing"});
        spec.axis = parseAxis(data["axis"]);
        const auto count = finite(data["maxCount"], "maxCount");
        if (count < 1 || count > double(ComponentParameterLimits::repeatMaxCount) ||
            std::floor(count) != count)
            throw ComponentParameterError("LIMIT_EXCEEDED",
                                          "Repeat maxCount must be a whole number 1-1000");
        spec.maxCount = std::uint32_t(count);
        spec.spacing = text(data["spacing"], "spacing");
        break;
    }
    case ComponentDriverKind::ChildParameter:
        require({"parameter"});
        spec.parameter = text(data["parameter"], "child parameter");
        break;
    }
    return spec;
}
QJsonValue valueJson(const ParameterValue &value) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag;
    if (const auto *choice = std::get_if<ParameterChoice>(&value))
        return QString::fromStdString(choice->value);
    return std::get<ParameterQuantity>(value).value;
}
QString trimmed(double value, int precision) {
    if (precision == fullDisplayPrecision)
        return QString::number(value, 'g', 15);
    auto result = QString::number(value, 'f', precision);
    if (result.startsWith('-') && result.mid(1).toDouble() == 0)
        result.remove(0, 1);
    return result;
}
// The value in the document's display units and precision (ADR 0018).
QString displayValue(const Document &doc, const ComponentParameter &parameter,
                     const ParameterValue &value) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag ? "true" : "false";
    if (const auto *choice = std::get_if<ParameterChoice>(&value)) {
        for (const auto &option : parameter.options)
            if (option.value == choice->value)
                return QString::fromStdString(option.label);
        return QString::fromStdString(choice->value);
    }
    const auto si = std::get<ParameterQuantity>(value).value;
    const auto units = doc.displayUnits();
    const auto precision = doc.displayPrecision();
    switch (parameter.type) {
    case ParameterType::Length:
        if (units == DisplayUnit::FeetInches)
            return QString::fromStdString(formatDrawingDimension(
                si, {DrawingDimensionUnit::FeetInches, precision == 0 ? 0 : 1, true}));
        return units == DisplayUnit::Millimeters ? trimmed(si * 1000, precision) + " mm"
                                                 : trimmed(si, precision) + " m";
    case ParameterType::Area:
        if (units == DisplayUnit::FeetInches)
            return trimmed(si / (.3048 * .3048), precision) + " ft²";
        return units == DisplayUnit::Millimeters ? trimmed(si * 1e6, precision) + " mm²"
                                                 : trimmed(si, precision) + " m²";
    case ParameterType::Angle:
        return trimmed(si * 180 / std::numbers::pi, precision) + "°";
    case ParameterType::Count:
        return QString::number(static_cast<long long>(si));
    default:
        return QString::number(si, 'g', 15);
    }
}
QJsonArray strings(const std::vector<std::string> &values) {
    QJsonArray result;
    for (const auto &value : values)
        result.append(QString::fromStdString(value));
    return result;
}
std::vector<std::string> referencesOf(const std::string &expression) {
    std::vector<std::string> result;
    if (expression.empty())
        return result;
    for (const auto &node : parseParameterExpression(expression).nodes)
        if (node.op == ParameterOp::Name &&
            std::find(result.begin(), result.end(), node.text) == result.end())
            result.push_back(node.text);
    std::sort(result.begin(), result.end());
    return result;
}
QJsonObject driverData(const ComponentDriver &driver) {
    const auto axis = QString::fromLatin1(componentAxisName(driver.axis).data());
    switch (driver.kind) {
    case ComponentDriverKind::Stretch:
        return {{"axis", axis}, {"at", driver.at}};
    case ComponentDriverKind::Position:
    case ComponentDriverKind::Rotation:
        return {{"axis", axis}};
    case ComponentDriverKind::Visibility:
        return {};
    case ComponentDriverKind::Repeat:
        return {{"axis", axis},
                {"maxCount", int(driver.maxCount)},
                {"spacing", QString::fromStdString(driver.spacing)},
                {"firstMember", QString::number(driver.firstMember)}};
    case ComponentDriverKind::ChildParameter:
        return {{"parameter", QString::fromStdString(driver.parameter)}};
    }
    return {};
}
QString stateName(ComponentDriverState state) {
    return state == ComponentDriverState::Bound ? "bound" : "changedSinceBound";
}
const DefinitionPtr &requireDefinition(const Document &doc, Id definition) {
    const auto found = doc.definitions().find(definition);
    if (found == doc.definitions().end())
        refuse("NOT_FOUND", "Component definition does not exist");
    return found->second;
}
QJsonObject errorJson(const ComponentParameterError &error) {
    const auto &context = error.context();
    QJsonObject result{{"code", QString::fromStdString(error.code())},
                       {"message", QString::fromUtf8(error.what())}};
    if (context.offset)
        result["offset"] = double(context.offset);
    if (!context.excerpt.empty())
        result["excerpt"] = QString::fromStdString(context.excerpt);
    if (!context.parameter.empty())
        result["parameter"] = QString::fromStdString(context.parameter);
    if (context.driver)
        result["driver"] = QString::number(context.driver);
    if (!context.cycle.empty())
        result["cycle"] = strings(context.cycle);
    if (!context.referrers.empty())
        result["referrers"] = strings(context.referrers);
    return result;
}
struct Symbols final : ParameterSymbols {
    const CompiledComponentParameters *compiled{};
    const ParameterSignature *find(std::string_view name) const override {
        if (!compiled)
            return nullptr;
        const auto found = compiled->index.find(name);
        return found == compiled->index.end() ? nullptr : &compiled->signatures[found->second];
    }
};
} // namespace

bool isComponentParameterCommand(const QString &name) {
    return name.startsWith(QStringLiteral("component_parameter."));
}
ComponentResult executeComponentParameterCommand(Document &doc, const QJsonObject &command) {
    const auto name = command["command"].toString();
    if (name == "component_parameter.override" || name == "component_parameter.reset") {
        const auto placement = decimal(command["body"]);
        if (name == "component_parameter.reset")
            return resetComponentParameters(doc, placement,
                                            selectsAll(command, "names")
                                                ? std::vector<std::string>{}
                                                : names(command["names"]));
        if (!command["values"].isObject())
            throw std::runtime_error("Override values must be an object");
        const auto values = command["values"].toObject();
        const auto definition = definitionOf(doc, placement);
        ComponentOverrides overrides;
        for (auto it = values.begin(); it != values.end(); ++it) {
            const auto key = it.key().toStdString();
            const Context context{.definition = definition, .placement = placement,
                                  .parameter = key};
            overrides[key] =
                overrideLiteral(it.value(), parameterNamed(doc, definition, key), context);
        }
        return overrideComponentParameters(doc, placement, overrides);
    }
    const auto definition = decimal(command["definition"]);
    if (name == "component_parameter.define") {
        ComponentParameterSpec spec;
        spec.name = text(command["name"], "name");
        spec.label = text(command["label"], "label");
        spec.type = parseType(command["type"]);
        spec.mode = parseMode(command["mode"]);
        spec.expression = text(command["expression"], "expression");
        bound(command, "min", spec.min, nullptr);
        bound(command, "max", spec.max, nullptr);
        bound(command, "step", spec.step, nullptr);
        if (command.contains("options"))
            spec.options = options(command["options"]);
        return defineComponentParameter(doc, definition, std::move(spec), position(command));
    }
    if (name == "component_parameter.update") {
        ComponentParameterUpdate update;
        if (command.contains("label"))
            update.label = text(command["label"], "label");
        if (command.contains("expression"))
            update.expression = text(command["expression"], "expression");
        bound(command, "min", update.min, &update.clearMin);
        bound(command, "max", update.max, &update.clearMax);
        bound(command, "step", update.step, &update.clearStep);
        if (command.contains("options"))
            update.options = options(command["options"]);
        update.position = position(command);
        return updateComponentParameter(doc, definition, text(command["name"], "name"), update);
    }
    if (name == "component_parameter.rename")
        return renameComponentParameter(doc, definition, text(command["name"], "name"),
                                        text(command["newName"], "new name"));
    if (name == "component_parameter.remove")
        return removeComponentParameter(doc, definition, text(command["name"], "name"));
    if (name == "component_parameter.drive")
        return driveComponent(doc, definition,
                              command.contains("driver") ? decimal(command["driver"]) : 0,
                              driverSpec(command));
    if (name == "component_parameter.undrive")
        return undriveComponent(doc, definition, decimal(command["driver"]));
    if (name == "component_parameter.rebind") {
        std::vector<Id> drivers;
        if (!selectsAll(command, "drivers")) {
            if (!command["drivers"].isArray() || command["drivers"].toArray().isEmpty())
                throw std::runtime_error("Name at least one driver to rebind");
            for (const auto &driver : command["drivers"].toArray())
                drivers.push_back(decimal(driver));
        }
        return rebindComponentDrivers(doc, definition, drivers);
    }
    throw std::runtime_error("Unavailable component parameter command");
}

QJsonArray componentParameterCommandSpecs() {
    auto stableId = [] {
        return QJsonObject{{"type", "string"},
                           {"pattern", "^[1-9][0-9]*$"},
                           {"maxLength", 20},
                           {"description", "Canonical uint64 decimal string"}};
    };
    auto string = [](int maximum, QString description = {}) {
        QJsonObject result{{"type", "string"}, {"maxLength", maximum}};
        if (!description.isEmpty())
            result["description"] = description;
        return result;
    };
    const auto parameterName =
        string(64, "Parameter name: [a-z][a-z0-9_]{0,31}, not a reserved word");
    const auto expression = string(2048, "Expression text (ADR 0165 §2); stored as canonical text");
    const auto label = string(512, "Display label: 1-128 UTF-8 bytes");
    const QJsonObject boundValue{{"type", "number"},
                                 {"description", "Literal bound in SI units"}};
    const QJsonObject clearableBound{{"type", QJsonArray{"number", "null"}},
                                     {"description", "Literal bound in SI units; null removes it"}};
    const QJsonObject options{
        {"type", "array"},
        {"minItems", 1},
        {"maxItems", 32},
        {"items", QJsonObject{{"type", "object"},
                              {"properties", QJsonObject{{"value", string(64, "[a-z0-9_-]{1,32}")},
                                                         {"label", string(512)}}},
                              {"required", QJsonArray{"value", "label"}},
                              {"additionalProperties", false}}}};
    const QJsonObject positionSchema{{"type", "integer"},
                                     {"minimum", 0},
                                     {"maximum", 128},
                                     {"description", "Display position; the end by default"}};
    const QJsonObject axis{{"type", "string"}, {"enum", QJsonArray{"x", "y", "z"}}};
    const QJsonObject driverData{
        {"type", "object"},
        {"description",
         "Exactly the kind's fields: stretch {axis, at}, position {axis}, rotation {axis}, "
         "visibility {}, repeat {axis, maxCount, spacing}, childParameter {parameter}"},
        {"properties",
         QJsonObject{{"axis", axis},
                     {"at", QJsonObject{{"type", "number"},
                                        {"minimum", -1e6},
                                        {"maximum", 1e6},
                                        {"description", "Stretch plane, member-local metres"}}},
                     {"maxCount", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
                     {"spacing", expression},
                     {"parameter", parameterName}}},
        {"additionalProperties", false}};
    const QJsonObject overrideValues{
        {"type", "object"},
        {"minProperties", 1},
        {"maxProperties", 128},
        {"propertyNames", QJsonObject{{"minLength", 1}, {"maxLength", 64}}},
        {"additionalProperties",
         QJsonObject{{"type", QJsonArray{"number", "string", "boolean"}},
                     {"maxLength", 64},
                     {"description", "A number in SI units, one signed quantity literal such as "
                                     "\"1200 mm\", a boolean or a choice value"}}}};
    auto spec = [](QString name, QString label, QJsonObject properties, QJsonArray required,
                   QString description = {}, int minProperties = 0, int maxProperties = 0) {
        properties["command"] = QJsonObject{{"const", name}};
        required.append("command");
        QJsonObject parameters{{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                               {"type", "object"},
                               {"description", description.isEmpty() ? label : description},
                               {"properties", properties},
                               {"required", required},
                               {"additionalProperties", false}};
        if (minProperties)
            parameters["minProperties"] = minProperties;
        if (maxProperties)
            parameters["maxProperties"] = maxProperties;
        return QJsonObject{
            {"name", name},
            {"label", label},
            {"category", "Component parameters"},
            {"parameters", parameters},
            {"validation", "Schema plus authoritative expression, graph, regeneration, document "
                           "and resource validation"},
            {"undo", "one batch history item"}};
    };
    const QJsonObject types{
        {"type", "string"},
        {"enum", QJsonArray{"length", "area", "angle", "number", "count", "boolean", "choice"}}};
    return {
        spec("component_parameter.define", "Define a component parameter",
             {{"definition", stableId()},
              {"name", parameterName},
              {"label", label},
              {"type", types},
              {"mode", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"input", "derived"}}}},
              {"expression", expression},
              {"min", boundValue},
              {"max", boundValue},
              {"step", boundValue},
              {"options", options},
              {"position", positionSchema}},
             {"definition", "name", "label", "type", "mode", "expression"},
             "Define a parameter on a definition: an input's default or a derived formula. "
             "Shared-definition edit; every placement regenerates"),
        spec("component_parameter.update", "Update a component parameter",
             {{"definition", stableId()},
              {"name", parameterName},
              {"label", label},
              {"expression", expression},
              {"min", clearableBound},
              {"max", clearableBound},
              {"step", clearableBound},
              {"options", options},
              {"position", positionSchema}},
             {"definition", "name"},
             "Change a parameter's label, expression, bounds, options or position; type and "
             "mode are fixed. A default change re-baselines the template",
             4),
        spec("component_parameter.rename", "Rename a component parameter",
             {{"definition", stableId()}, {"name", parameterName}, {"newName", parameterName}},
             {"definition", "name", "newName"},
             "Rename a parameter; references in the definition, overrides on its placements "
             "and parent childParameter drivers follow"),
        spec("component_parameter.remove", "Remove a component parameter",
             {{"definition", stableId()}, {"name", parameterName}},
             {"definition", "name"},
             "Remove a parameter; refused with PARAMETER_NAME while anything refers to it"),
        spec("component_parameter.drive", "Drive a component member",
             {{"definition", stableId()},
              {"driver", stableId()},
              {"kind", QJsonObject{{"type", "string"},
                                   {"enum", QJsonArray{"stretch", "position", "rotation",
                                                       "visibility", "repeat",
                                                       "childParameter"}}}},
              {"member", stableId()},
              {"expression", expression},
              {"data", driverData}},
             {"definition", "kind", "member", "expression", "data"},
             "Create a driver, or update the named driver, on a canonical definition member; "
             "stamps its bound digest"),
        spec("component_parameter.undrive", "Remove a component driver",
             {{"definition", stableId()}, {"driver", stableId()}}, {"definition", "driver"}),
        spec("component_parameter.rebind", "Rebind component drivers to their current targets",
             {{"definition", stableId()},
              {"drivers", QJsonObject{{"type", "array"},
                                      {"items", stableId()},
                                      {"minItems", 1},
                                      {"maxItems", 256}}},
              {"all", QJsonObject{{"const", true}}}},
             {"definition"},
             "Restamp the named drivers, or every driver (all: true), so drivers whose targets "
             "changed become bound again",
             3, 3),
        spec("component_parameter.override", "Set component parameter values on a placement",
             {{"body", stableId()}, {"values", overrideValues}}, {"body", "values"},
             "Instance-scoped: set input values on a top-level placement. Values outside the "
             "parameter's type, bounds or step are refused, never clamped"),
        spec("component_parameter.reset", "Reset component parameter values on a placement",
             {{"body", stableId()},
              {"names", QJsonObject{{"type", "array"},
                                    {"items", parameterName},
                                    {"minItems", 1},
                                    {"maxItems", 128}}},
              {"all", QJsonObject{{"const", true}}}},
             {"body"},
             "Instance-scoped: remove the named overrides, or every override (all: true)",
             3, 3)};
}

QJsonArray createdComponentDrivers(const ComponentDefinitions &before,
                                   const ComponentDefinitions &after) {
    std::set<Id> existing, created;
    for (const auto &[id, definition] : before)
        if (definition->parameters)
            for (const auto &driver : definition->parameters->drivers)
                existing.insert(driver.id);
    for (const auto &[id, definition] : after) {
        const auto previous = before.find(id);
        if (!definition->parameters ||
            (previous != before.end() && previous->second == definition))
            continue;
        for (const auto &driver : definition->parameters->drivers)
            if (!existing.contains(driver.id))
                created.insert(driver.id);
    }
    QJsonArray result;
    for (const auto id : created)
        result.append(QString::number(id));
    return result;
}
QJsonArray componentParameterWarnings(const ComponentDefinitions &before,
                                      const ComponentDefinitions &after) {
    std::set<Id> warnings;
    for (const auto &[id, definition] : after) {
        const auto previous = before.find(id);
        if (!definition->parameters || previous == before.end() ||
            previous->second == definition || !previous->second->parameters)
            continue;
        for (const auto &driver : definition->parameters->drivers) {
            if (componentDriverState(*definition, after, driver) !=
                ComponentDriverState::ChangedSinceBound)
                continue;
            for (const auto &old : previous->second->parameters->drivers)
                if (old.id == driver.id &&
                    componentDriverState(*previous->second, before, old) ==
                        ComponentDriverState::Bound)
                    warnings.insert(driver.id);
        }
    }
    QJsonArray result;
    for (const auto id : warnings)
        result.append(QString::number(id));
    return result;
}

QJsonObject componentParametersDescription(const Document &doc, Id id) {
    const auto &definition = requireDefinition(doc, id);
    int placements = 0, overridden = 0;
    for (const auto &[root, instance] : doc.instances())
        if (instance->definition == id) {
            ++placements;
            if (!instance->overrides.empty())
                ++overridden;
        }
    QJsonObject result{{"definition", QString::number(id)},
                       {"name", QString::fromStdString(definition->name)},
                       {"parametric", bool(definition->parameters)},
                       {"placements", placements},
                       {"overriddenPlacements", overridden},
                       {"parameters", QJsonArray{}},
                       {"drivers", QJsonArray{}}};
    if (!definition->parameters)
        return result;
    const auto &record = *definition->parameters;
    ComponentVariantEngine engine(doc.definitions(), &doc.componentVariants());
    const auto compiled = engine.compiled(id);
    const auto &defaults = engine.evaluate(id, {});
    std::map<std::string, std::vector<std::string>> usedBy;
    for (const auto &parameter : record.parameters)
        for (const auto &name : referencesOf(parameter.expression))
            usedBy[name].push_back("parameter " + parameter.name);
    for (const auto &driver : record.drivers) {
        auto references = referencesOf(driver.expression);
        for (const auto &name : referencesOf(driver.spacing))
            if (std::find(references.begin(), references.end(), name) == references.end())
                references.push_back(name);
        for (const auto &name : references)
            usedBy[name].push_back("driver " + std::to_string(driver.id));
    }
    QJsonArray parameters;
    for (const auto index : compiled->order) {
        const auto &parameter = record.parameters[index];
        QJsonArray options;
        for (const auto &option : parameter.options)
            options.append(QJsonObject{{"value", QString::fromStdString(option.value)},
                                       {"label", QString::fromStdString(option.label)}});
        auto optional = [](const std::optional<double> &value) {
            return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
        };
        parameters.append(QJsonObject{
            {"name", QString::fromStdString(parameter.name)},
            {"label", QString::fromStdString(parameter.label)},
            {"position", int(index)},
            {"type", typeName(parameter.type)},
            {"mode", parameter.mode == ComponentParameterMode::Input ? "input" : "derived"},
            {"expression", QString::fromStdString(parameter.expression)},
            {"defaultValue", valueJson(defaults.parameters[index])},
            {"display", displayValue(doc, parameter, defaults.parameters[index])},
            {"min", optional(parameter.min)},
            {"max", optional(parameter.max)},
            {"step", optional(parameter.step)},
            {"options", parameter.type == ParameterType::Choice ? QJsonValue(options)
                                                                : QJsonValue(QJsonValue::Null)},
            {"dependsOn", strings(referencesOf(parameter.expression))},
            {"usedBy", strings(usedBy[parameter.name])}});
    }
    QJsonArray drivers;
    for (const auto &driver : record.drivers) {
        auto references = referencesOf(driver.expression);
        for (const auto &name : referencesOf(driver.spacing))
            if (std::find(references.begin(), references.end(), name) == references.end())
                references.push_back(name);
        std::sort(references.begin(), references.end());
        drivers.append(QJsonObject{
            {"id", QString::number(driver.id)},
            {"kind", QString::fromLatin1(componentDriverKindName(driver.kind).data())},
            {"member", QString::number(driver.member)},
            {"expression", QString::fromStdString(driver.expression)},
            {"data", driverData(driver)},
            {"dependsOn", strings(references)},
            {"bound", QString::fromStdString(digestHex(driver.bound))},
            {"state", stateName(componentDriverState(*definition, doc.definitions(), driver))}});
    }
    result["parameters"] = parameters;
    result["drivers"] = drivers;
    return result;
}

QJsonObject componentParameterValues(const Document &doc, Id placement) {
    const auto found = doc.instances().find(placement);
    if (found == doc.instances().end())
        refuse("NOT_FOUND", "Component placement does not exist");
    const auto id = found->second->definition;
    const auto &definition = requireDefinition(doc, id);
    QJsonObject result{{"body", QString::number(placement)},
                       {"definition", QString::number(id)},
                       {"values", QJsonArray{}},
                       {"drivers", QJsonArray{}}};
    if (!definition->parameters)
        return result;
    const auto inputs = componentPlacementInputs(doc, placement);
    const bool nested = inputs != found->second->overrides;
    ComponentVariantEngine engine(doc.definitions(), &doc.componentVariants());
    const auto &evaluation = engine.evaluate(id, inputs);
    const auto &record = *definition->parameters;
    QJsonArray values;
    for (std::size_t i = 0; i < record.parameters.size(); ++i) {
        const auto &parameter = record.parameters[i];
        QString source = "default";
        if (parameter.mode == ComponentParameterMode::Derived)
            source = "derived";
        else if (inputs.contains(parameter.name))
            source = nested ? "parent" : "override";
        values.append(QJsonObject{{"name", QString::fromStdString(parameter.name)},
                                  {"type", typeName(parameter.type)},
                                  {"value", valueJson(evaluation.parameters[i])},
                                  {"display", displayValue(doc, parameter, evaluation.parameters[i])},
                                  {"source", source}});
    }
    QJsonArray drivers;
    for (std::size_t i = 0; i < record.drivers.size(); ++i) {
        const auto &driver = record.drivers[i];
        QJsonObject entry{
            {"id", QString::number(driver.id)},
            {"kind", QString::fromLatin1(componentDriverKindName(driver.kind).data())},
            {"value", valueJson(evaluation.drivers[i])},
            {"state", stateName(componentDriverState(*definition, doc.definitions(), driver))}};
        if (driver.kind == ComponentDriverKind::Repeat)
            entry["spacing"] = evaluation.spacing[i];
        drivers.append(entry);
    }
    result["values"] = values;
    result["drivers"] = drivers;
    result["variant"] = doc.componentVariant(placement) ? "variant" : "template";
    return result;
}

QJsonObject componentParametersEvaluation(const Document &doc, Id id, const QJsonObject &request) {
    const auto &definition = requireDefinition(doc, id);
    QJsonObject result{{"definition", QString::number(id)}};
    try {
        ComponentVariantEngine engine(doc.definitions(), &doc.componentVariants());
        CompiledParametersPtr compiled;
        if (definition->parameters)
            compiled = engine.compiled(id);
        ComponentOverrides overrides;
        if (request.contains("overrides")) {
            const auto values = request["overrides"].toObject();
            if (!compiled && !values.isEmpty())
                throw ComponentParameterError("PARAMETER_NAME",
                                              "Definition #" + std::to_string(id) +
                                                  " has no parameters",
                                              {.definition = id});
            for (auto it = values.begin(); it != values.end(); ++it) {
                const auto key = it.key().toStdString();
                overrides[key] = overrideLiteral(it.value(), parameterNamed(doc, id, key),
                                                 {.definition = id, .parameter = key});
            }
            if (compiled)
                overrides = canonicalComponentOverrides(*compiled, overrides);
        }
        std::optional<ComponentEvaluation> evaluation;
        if (compiled) {
            evaluation = engine.evaluate(id, overrides);
            QJsonArray values;
            const auto &record = *definition->parameters;
            for (std::size_t i = 0; i < record.parameters.size(); ++i)
                values.append(QJsonObject{
                    {"name", QString::fromStdString(record.parameters[i].name)},
                    {"value", valueJson(evaluation->parameters[i])},
                    {"display",
                     displayValue(doc, record.parameters[i], evaluation->parameters[i])}});
            result["values"] = values;
            // The regeneration size of a placement with these values, within the same
            // limits as an edit (distinct variants, evaluation budget).
            const auto variant = engine.variant(id, overrides);
            const auto sizes = validateComponentDefinitions(
                doc.definitions(), doc.nextDefinitionId(), doc.tags(), doc.nextTagId(),
                doc.materials(), doc.nextMaterialId(), doc.assets(), doc.nextAssetId());
            const auto size = componentVariantSize(*definition, variant, sizes);
            result["regeneration"] =
                QJsonObject{{"template", !variant},
                            {"records", double(size.records)},
                            {"vertices", double(size.vertices)},
                            {"faces", double(size.faces)},
                            {"edges", double(size.edges)},
                            {"placements", double(size.placements)},
                            {"generatedMembers", variant ? int(variant->generated.size()) : 0}};
        }
        if (request.contains("expression")) {
            const auto source = request["expression"].toString().toStdString();
            const Context context{.definition = id};
            ParameterExpression parsed;
            try {
                parsed = parseParameterExpression(source);
            } catch (const ParameterExpressionError &error) {
                throwParameterExpressionError(error, context);
            }
            Symbols symbols;
            symbols.compiled = compiled.get();
            // The expected type: the requested one, or the first that type-checks.
            // A choice result is checked against each choice parameter's options.
            const auto requested = request.contains("type")
                                       ? std::optional(parseType(request["type"]))
                                       : std::nullopt;
            std::vector<ParameterSignature> candidates;
            for (auto type : {ParameterType::Length, ParameterType::Area, ParameterType::Angle,
                              ParameterType::Number, ParameterType::Count,
                              ParameterType::Boolean})
                if (requested ? *requested == type : type != ParameterType::Count)
                    candidates.push_back({type, {}});
            if (compiled && (!requested || *requested == ParameterType::Choice))
                for (const auto &parameter : definition->parameters->parameters)
                    if (parameter.type == ParameterType::Choice) {
                        ParameterSignature signature{ParameterType::Choice, {}};
                        for (const auto &option : parameter.options)
                            signature.options.push_back(option.value);
                        candidates.push_back(signature);
                    }
            std::optional<CheckedParameterExpression> checked;
            std::optional<ParameterExpressionError> first;
            for (const auto &candidate : candidates) {
                try {
                    checked = checkParameterExpression(parsed, symbols, candidate);
                    break;
                } catch (const ParameterExpressionError &error) {
                    if (!first || (first->code() == ParameterErrorCode::Type &&
                                   error.code() != ParameterErrorCode::Type))
                        first = error;
                }
            }
            if (!checked) {
                if (!first)
                    throw ComponentParameterError("PARAMETER_TYPE",
                                                  "A choice expression needs a choice parameter",
                                                  context);
                throwParameterExpressionError(*first, context);
            }
            std::vector<ParameterValue> values;
            for (const auto &name : checked->references)
                values.push_back(evaluation->parameters[compiled->index.at(name)]);
            ParameterEvaluationBudget budget{ComponentParameterLimits::definitionVisits};
            ParameterValue value;
            try {
                value = evaluateParameterExpression(*checked, values, budget);
            } catch (const ParameterExpressionError &error) {
                throwParameterExpressionError(error, context);
            }
            ComponentParameter shown;
            shown.type = checked->result.type;
            result["expression"] = QJsonObject{
                {"canonical", QString::fromStdString(canonicalParameterExpression(parsed))},
                {"type", typeName(checked->result.type)},
                {"value", valueJson(value)},
                {"display", displayValue(doc, shown, value)},
                {"dependsOn", strings(referencesOf(canonicalParameterExpression(parsed)))}};
        }
        result["status"] = "ok";
    } catch (const ComponentParameterError &error) {
        result = {{"definition", QString::number(id)},
                  {"status", "error"},
                  {"error", errorJson(error)}};
    }
    return result;
}
} // namespace sketchy
