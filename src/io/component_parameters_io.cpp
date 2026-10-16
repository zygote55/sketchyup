#include "io/component_parameters_io.hpp"
#include <QJsonObject>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <set>
namespace sketchy {
namespace {
[[noreturn]] void refuse(const char *message) { throw std::runtime_error(message); }
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    if (!value.isObject())
        refuse("Component parameter record requires an object");
    const auto result = value.toObject();
    if (result.size() != fields.size())
        refuse("Component parameter record has missing or unknown fields");
    for (const auto &field : fields)
        if (!result.contains(field))
            refuse("Missing component parameter field");
    return result;
}
QJsonArray array(const QJsonValue &value, std::size_t maximum) {
    if (!value.isArray() || std::size_t(value.toArray().size()) > maximum)
        refuse("Component parameter table exceeds its shape or count limit");
    return value.toArray();
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        refuse("Component parameter records require finite numeric values");
    return value.toDouble();
}
std::optional<double> optionalNumber(const QJsonValue &value) {
    if (value.isNull())
        return std::nullopt;
    return number(value);
}
QJsonValue optionalNumber(const std::optional<double> &value) {
    return value ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}
Id id(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        refuse("Component parameter identity requires a canonical nonzero decimal string");
    return result;
}
QString sid(Id value) { return QString::number(value); }
std::string text(const QJsonValue &value) {
    if (!value.isString())
        refuse("Component parameter text field requires a string");
    return value.toString().toStdString();
}
QString str(const std::string &value) { return QString::fromStdString(value); }
QString typeCode(ParameterType type) { return QString::fromLatin1(parameterTypeName(type).data()); }
ParameterType typeValue(const QJsonValue &value) {
    for (const auto type :
         {ParameterType::Length, ParameterType::Area, ParameterType::Angle, ParameterType::Number,
          ParameterType::Count, ParameterType::Boolean, ParameterType::Choice})
        if (value == typeCode(type))
            return type;
    refuse("Unknown component parameter type");
}
bool quantity(ParameterType type) {
    return type != ParameterType::Boolean && type != ParameterType::Choice;
}
QString modeCode(ComponentParameterMode mode) {
    return mode == ComponentParameterMode::Derived ? "derived" : "input";
}
ComponentParameterMode modeValue(const QJsonValue &value) {
    if (value == "input")
        return ComponentParameterMode::Input;
    if (value == "derived")
        return ComponentParameterMode::Derived;
    refuse("Unknown component parameter mode");
}
QString kindCode(ComponentDriverKind kind) {
    return QString::fromLatin1(componentDriverKindName(kind).data());
}
ComponentDriverKind kindValue(const QJsonValue &value) {
    for (const auto kind :
         {ComponentDriverKind::Stretch, ComponentDriverKind::Position, ComponentDriverKind::Rotation,
          ComponentDriverKind::Visibility, ComponentDriverKind::Repeat,
          ComponentDriverKind::ChildParameter})
        if (value == kindCode(kind))
            return kind;
    refuse("Unknown component driver kind");
}
QString axisCode(ComponentAxis axis) { return QString::fromLatin1(componentAxisName(axis).data()); }
ComponentAxis axisValue(const QJsonValue &value) {
    for (const auto axis : {ComponentAxis::X, ComponentAxis::Y, ComponentAxis::Z})
        if (value == axisCode(axis))
            return axis;
    refuse("Unknown component driver axis");
}
ComponentDigest digest(const QJsonValue &value) {
    const auto hex = text(value);
    if (hex.size() != 64)
        refuse("Component driver bound requires 64 lowercase hexadecimal digits");
    ComponentDigest result{};
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        refuse("Component driver bound requires 64 lowercase hexadecimal digits");
    };
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = std::uint8_t(nibble(hex[2 * i]) << 4 | nibble(hex[2 * i + 1]));
    return result;
}
QJsonObject encodeParameter(const ComponentParameter &parameter) {
    QJsonValue options = QJsonValue::Null;
    if (parameter.type == ParameterType::Choice) {
        QJsonArray list;
        for (const auto &option : parameter.options)
            list.append(QJsonObject{{"value", str(option.value)}, {"label", str(option.label)}});
        options = list;
    }
    return {{"name", str(parameter.name)},
            {"label", str(parameter.label)},
            {"type", typeCode(parameter.type)},
            {"mode", modeCode(parameter.mode)},
            {"expression", str(parameter.expression)},
            {"min", optionalNumber(parameter.min)},
            {"max", optionalNumber(parameter.max)},
            {"step", optionalNumber(parameter.step)},
            {"options", options}};
}
ComponentParameter decodeParameter(const QJsonValue &value) {
    const auto record = object(value, {"name", "label", "type", "mode", "expression", "min", "max",
                                       "step", "options"});
    ComponentParameter parameter;
    parameter.name = text(record["name"]);
    parameter.label = text(record["label"]);
    parameter.type = typeValue(record["type"]);
    parameter.mode = modeValue(record["mode"]);
    parameter.expression = text(record["expression"]);
    // Bounds apply to quantities only; null where they do not apply or are unset.
    parameter.min = optionalNumber(record["min"]);
    parameter.max = optionalNumber(record["max"]);
    parameter.step = optionalNumber(record["step"]);
    if (!quantity(parameter.type) && (parameter.min || parameter.max || parameter.step))
        refuse("Component parameter bounds apply only to quantities");
    if (parameter.type == ParameterType::Choice) {
        for (const auto &entry : array(record["options"], ComponentParameterLimits::options)) {
            const auto option = object(entry, {"value", "label"});
            parameter.options.push_back({text(option["value"]), text(option["label"])});
        }
    } else if (!record["options"].isNull())
        refuse("Component parameter options apply only to choices");
    return parameter;
}
QJsonObject encodeDriverData(const ComponentDriver &driver) {
    switch (driver.kind) {
    case ComponentDriverKind::Stretch:
        return {{"axis", axisCode(driver.axis)}, {"at", driver.at}};
    case ComponentDriverKind::Position:
    case ComponentDriverKind::Rotation:
        return {{"axis", axisCode(driver.axis)}};
    case ComponentDriverKind::Visibility:
        return {};
    case ComponentDriverKind::Repeat:
        return {{"axis", axisCode(driver.axis)},
                {"maxCount", qint64(driver.maxCount)},
                {"spacing", str(driver.spacing)},
                {"firstMember", sid(driver.firstMember)}};
    case ComponentDriverKind::ChildParameter:
        return {{"parameter", str(driver.parameter)}};
    }
    refuse("Unknown component driver kind");
}
// Fields outside the driver's kind keep their defaults, as the builders store them.
void decodeDriverData(const QJsonValue &value, ComponentDriver &driver) {
    switch (driver.kind) {
    case ComponentDriverKind::Stretch: {
        const auto data = object(value, {"axis", "at"});
        driver.axis = axisValue(data["axis"]);
        driver.at = number(data["at"]);
        return;
    }
    case ComponentDriverKind::Position:
    case ComponentDriverKind::Rotation:
        driver.axis = axisValue(object(value, {"axis"})["axis"]);
        return;
    case ComponentDriverKind::Visibility:
        (void)object(value, {});
        return;
    case ComponentDriverKind::Repeat: {
        const auto data = object(value, {"axis", "maxCount", "spacing", "firstMember"});
        driver.axis = axisValue(data["axis"]);
        const auto count = number(data["maxCount"]);
        if (count != std::floor(count) || count < 1 ||
            count > double(ComponentParameterLimits::repeatMaxCount))
            refuse("Component repeat maxCount must be an integer from 1 to 1,000");
        driver.maxCount = std::uint32_t(count);
        driver.spacing = text(data["spacing"]);
        driver.firstMember = id(data["firstMember"]);
        return;
    }
    case ComponentDriverKind::ChildParameter:
        driver.parameter = text(object(value, {"parameter"})["parameter"]);
        return;
    }
    refuse("Unknown component driver kind");
}
QJsonObject encodeDriver(const ComponentDriver &driver) {
    return {{"id", sid(driver.id)},
            {"kind", kindCode(driver.kind)},
            {"member", sid(driver.member)},
            {"expression", str(driver.expression)},
            {"bound", QString::fromStdString(digestHex(driver.bound))},
            {"data", encodeDriverData(driver)}};
}
ComponentDriver decodeDriver(const QJsonValue &value) {
    const auto record = object(value, {"id", "kind", "member", "expression", "bound", "data"});
    ComponentDriver driver;
    driver.id = id(record["id"]);
    driver.kind = kindValue(record["kind"]);
    driver.member = id(record["member"]);
    driver.expression = text(record["expression"]);
    driver.bound = digest(record["bound"]);
    decodeDriverData(record["data"], driver);
    return driver;
}
QJsonValue encodeValue(const ParameterValue &value) {
    if (const auto *flag = std::get_if<bool>(&value))
        return *flag;
    if (const auto *choice = std::get_if<ParameterChoice>(&value))
        return str(choice->value);
    return std::get<ParameterQuantity>(value).value;
}
// Stored values carry no type: the named input parameter supplies it. Whether the
// value is canonical, in range and an input's is checked when restore evaluates it.
ParameterValue decodeValue(const QJsonValue &value, ParameterType type) {
    switch (type) {
    case ParameterType::Boolean:
        if (!value.isBool())
            refuse("Boolean component parameter override requires true or false");
        return value.toBool();
    case ParameterType::Choice:
        return ParameterChoice{text(value)};
    default:
        return ParameterQuantity{number(value), parameterDimension(type)};
    }
}
} // namespace
QJsonArray encodeComponentParameters(const ComponentDefinitions &definitions) {
    QJsonArray result;
    for (const auto &[key, definition] : definitions) {
        if (!definition->parameters)
            continue;
        QJsonArray parameters, drivers;
        for (const auto &parameter : definition->parameters->parameters)
            parameters.append(encodeParameter(parameter));
        for (const auto &driver : definition->parameters->drivers)
            drivers.append(encodeDriver(driver));
        QJsonObject record{
            {"definition", sid(key)}, {"parameters", parameters}, {"drivers", drivers}};
        // ADR 0166 §6.2: written only when the definition has a library identity.
        if (const auto &assembly = definition->parameters->assembly)
            record["assembly"] = QJsonObject{{"id", str(assembly->id)},
                                             {"version", qint64(assembly->version)}};
        result.append(record);
    }
    return result;
}
QJsonArray encodeComponentParameterOverrides(const ComponentInstances &instances) {
    QJsonArray result;
    for (const auto &[root, instance] : instances) {
        if (instance->overrides.empty())
            continue;
        QJsonObject values;
        for (const auto &[name, value] : instance->overrides)
            values[str(name)] = encodeValue(value);
        result.append(QJsonObject{{"instance", sid(root)}, {"values", values}});
    }
    return result;
}
void decodeComponentParameters(const QJsonValue &value, ComponentDefinitions &definitions) {
    Id previous = 0;
    for (const auto &entry : array(value, ComponentParameterLimits::parametricDefinitions)) {
        const bool identified = entry.isObject() && entry.toObject().contains("assembly");
        const auto record =
            object(entry, identified ? QStringList{"definition", "parameters", "drivers", "assembly"}
                                     : QStringList{"definition", "parameters", "drivers"});
        const auto definition = id(record["definition"]);
        if (definition <= previous)
            refuse("Component parameter records must be stored once each in definition order");
        previous = definition;
        const auto found = definitions.find(definition);
        if (found == definitions.end())
            refuse("Component parameter record names a missing definition");
        auto parameters = std::make_shared<ComponentParameters>();
        for (const auto &item : array(record["parameters"], ComponentParameterLimits::parameters))
            parameters->parameters.push_back(decodeParameter(item));
        for (const auto &item : array(record["drivers"], ComponentParameterLimits::drivers)) {
            auto driver = decodeDriver(item);
            if (!parameters->drivers.empty() && driver.id <= parameters->drivers.back().id)
                refuse("Component drivers must be stored once each in ID order");
            parameters->drivers.push_back(std::move(driver));
        }
        if (identified) {
            const auto assembly = object(record["assembly"], {"id", "version"});
            const auto version = number(assembly["version"]);
            if (!assembly["id"].isString() ||
                !validComponentAssembly(assembly["id"].toString().toStdString(), version))
                refuse("Component assembly identity requires an id matching "
                       "[a-z0-9_-]{1,64}(/[a-z0-9_-]{1,64})? and a version 1-2147483647");
            parameters->assembly =
                ComponentAssembly{text(assembly["id"]), std::uint32_t(version)};
        }
        auto updated = std::make_shared<ComponentDefinition>(*found->second);
        updated->parameters = std::move(parameters);
        found->second = std::move(updated);
    }
}
void decodeComponentParameterOverrides(const QJsonValue &value,
                                       const ComponentDefinitions &definitions,
                                       ComponentInstances &instances) {
    Id previous = 0;
    const auto limit = std::size_t(DocumentLimits::materialized.placements);
    for (const auto &entry : array(value, limit)) {
        const auto record = object(entry, {"instance", "values"});
        const auto root = id(record["instance"]);
        if (root <= previous)
            refuse("Component parameter overrides must be stored once each in placement order");
        previous = root;
        const auto found = instances.find(root);
        if (found == instances.end() || !found->second)
            refuse("Component parameter overrides name a missing placement");
        const auto definition = definitions.find(found->second->definition);
        if (definition == definitions.end() || !definition->second->parameters)
            refuse("Component parameter overrides name a definition without parameters");
        if (!record["values"].isObject())
            refuse("Component parameter override values require an object");
        const auto values = record["values"].toObject();
        if (values.isEmpty() ||
            std::size_t(values.size()) > ComponentParameterLimits::parameters)
            refuse("Component parameter overrides hold 1-128 values");
        ComponentOverrides overrides;
        const auto &list = definition->second->parameters->parameters;
        for (auto it = values.begin(); it != values.end(); ++it) {
            const auto name = it.key().toStdString();
            const auto parameter = std::find_if(list.begin(), list.end(), [&](const auto &p) {
                return p.name == name;
            });
            if (parameter == list.end())
                refuse("Component parameter override names an unknown parameter");
            overrides.emplace(name, decodeValue(it.value(), parameter->type));
        }
        auto updated = std::make_shared<ComponentInstance>(*found->second);
        updated->overrides = std::move(overrides);
        found->second = std::move(updated);
    }
}
} // namespace sketchy
