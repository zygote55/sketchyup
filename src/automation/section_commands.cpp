#include "automation/section_commands.hpp"
#include "io/sections_io.hpp"
namespace sketchy {
namespace {
Id id(const QJsonValue &value, bool zero = false) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || (!zero && !result) || QString::number(result) != text)
        throw std::runtime_error("Section requires a canonical decimal identity");
    return result;
}
} // namespace
bool isSectionCommand(const QString &name) {
    return name == "section.create" || name == "section.update" || name == "section.delete" ||
           name == "section.activate";
}
void executeSectionCommand(Document &doc, const QJsonObject &command) {
    const auto operation = command["command"].toString();
    if (operation == "section.delete") {
        eraseSection(doc, id(command["section"]));
        return;
    }
    if (operation == "section.activate") {
        const auto value = command["section"];
        setActiveSection(doc, id(command["context"], true),
                         value.isNull() ? std::optional<Id>{} : id(value));
        return;
    }
    if (operation != "section.create" && operation != "section.update")
        throw std::runtime_error("Unknown section command");
    const auto context = id(command["context"], true);
    const auto space = command["space"].toString();
    if (space != "local" && space != "world")
        throw std::runtime_error("Choose local or world section coordinates");
    if (context && !doc.bodies().contains(context)) {
        const auto existing = operation == "section.update" ? id(command["section"]) : 0;
        if (space == "world" || !doc.sections().contains(existing) ||
            doc.sections().at(existing)->context != context)
            throw std::runtime_error("Section command requires an existing editing context");
    }
    if (operation == "section.update" &&
        (!command.contains("fill") || !command.contains("edges") || !command.contains("color")))
        throw std::runtime_error("Section update requires all display properties");
    const auto plane = command["plane"].toObject();
    if (plane.size() != 2 || !plane.contains("normal") || !plane.contains("offset") ||
        !plane["normal"].isArray() || plane["normal"].toArray().size() != 3)
        throw std::runtime_error("Section plane requires a unit normal and signed offset");
    auto coefficients = plane["normal"].toArray();
    coefficients.append(plane["offset"]);
    // Reuse strict record decoding for names, planes, flags and colors, with a
    // temporary safe ID so validation never loses uint64 identity precision.
    const QJsonObject encoded{
        {"id", "1"},
        {"name", command["name"]},
        {"context", QString::number(context)},
        {"plane", coefficients},
        {"fill", command.contains("fill") ? command["fill"] : QJsonValue(true)},
        {"edges", command.contains("edges") ? command["edges"] : QJsonValue(true)},
        {"color",
         command.contains("color") ? command["color"] : QJsonValue(QJsonArray{.85, .75, .55})}};
    auto value = *decodeSections(QJsonArray{encoded}, 2).at(1);
    if (space == "world" && context)
        value.plane = value.plane.transformed(doc.worldTransform(context).inverse());
    if (operation == "section.create") {
        value.id = doc.nextSectionId();
        Edit edit{"Create section plane", {}};
        edit.sections.push_back({value.id, nullptr, std::make_shared<SectionRecord>(value)});
        doc.apply(std::move(edit), doc.revision());
    } else {
        value.id = id(command["section"]);
        updateSection(doc, value.id, value);
    }
}
QJsonObject sectionPlaneSchema() {
    const QJsonObject coefficient{{"type", "number"}, {"minimum", -1}, {"maximum", 1}};
    return {{"type", "object"},
            {"properties",
             QJsonObject{
                 {"normal",
                  QJsonObject{
                      {"type", "array"}, {"minItems", 3}, {"maxItems", 3}, {"items", coefficient}}},
                 {"offset", QJsonObject{{"type", "number"}, {"minimum", -2e6}, {"maximum", 2e6}}}}},
            {"required", QJsonArray{"normal", "offset"}},
            {"additionalProperties", false},
            {"description", "Unit normal and signed offset in metres; retained side is "
                            "dot(normal,point)+offset >= 0"}};
}
QJsonObject sectionDescription(const Document &doc, Id section) {
    if (!doc.sections().contains(section))
        throw std::runtime_error("Section plane does not exist");
    const auto &record = *doc.sections().at(section);
    auto result = encodeSections({{section, doc.sections().at(section)}})[0].toObject();
    const bool missing = record.context && !doc.bodies().contains(record.context);
    const bool active = doc.activeSections().contains(record.context) &&
                        doc.activeSections().at(record.context) == section;
    result["active"] = active;
    result["effective"] = active && !missing;
    result["missingContext"] = missing;
    if (missing)
        result["worldPlane"] = QJsonValue::Null;
    else {
        const auto p = record.context ? record.plane.transformed(doc.worldTransform(record.context))
                                      : record.plane;
        result["worldPlane"] = QJsonArray{p.normal.x, p.normal.y, p.normal.z, p.offset};
    }
    return result;
}
} // namespace sketchy
