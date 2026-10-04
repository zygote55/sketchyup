#include "automation/commands.hpp"
namespace sketchy {
namespace {
QJsonObject number() { return {{"type", "number"}}; }
QJsonObject stableId(bool zero = false) {
    return {{"type", "string"},
            {"pattern", zero ? "^(0|[1-9][0-9]*)$" : "^[1-9][0-9]*$"},
            {"maxLength", 20},
            {"description", "Canonical uint64 decimal string"}};
}
QJsonObject list(QJsonObject items, int minimum, int maximum) {
    return {{"type", "array"}, {"items", items}, {"minItems", minimum}, {"maxItems", maximum}};
}
QJsonObject spec(QString name, QString label, QString category, QJsonObject properties,
                 QJsonArray required) {
    properties["command"] = QJsonObject{{"const", name}};
    required.append("command");
    return {{"name", name},
            {"label", label},
            {"category", category},
            {"parameters", QJsonObject{{"$schema", "https://json-schema.org/draft/2020-12/schema"},
                                       {"type", "object"},
                                       {"properties", properties},
                                       {"required", required},
                                       {"additionalProperties", false}}},
            {"validation", "Schema plus authoritative geometry, document and resource validation"},
            {"undo", "one batch history item"}};
}
} // namespace
QJsonArray commandCatalog() {
    auto coordinate = QJsonObject{
        {"type", "number"}, {"minimum", -coordinateLimit}, {"maximum", coordinateLimit}};
    auto point = list(coordinate, 3, 3);
    const QJsonObject space{{"type", "string"}, {"enum", QJsonArray{"local", "world"}}};
    return {
        spec("geometry.face", "Draw face", "Geometry",
             {{"loops", list(list(point, 3, 10000), 1, 10000)},
              {"name", QJsonObject{{"type", "string"}, {"maxLength", 1024}}}},
             {"loops"}),
        spec("geometry.insert_edges", "Insert planar edges and form faces", "Geometry",
             {{"body", stableId(true)},
              {"origin", point},
              {"normal", point},
              {"edges", list(list(point, 2, 2), 1, 1024)}},
             {"body", "origin", "normal", "edges"}),
        spec("geometry.rectangle", "Draw rectangle on plane", "Geometry",
             {{"body", stableId(true)},
              {"space", space},
              {"origin", point},
              {"normal", point},
              {"xAxis", point},
              {"width", number()},
              {"height", number()}},
             {"body", "origin", "normal", "xAxis", "width", "height"}),
        spec("geometry.polygon", "Draw regular polygon", "Geometry",
             {{"body", stableId(true)},
              {"space", space},
              {"origin", point},
              {"normal", point},
              {"xAxis", point},
              {"radius", number()},
              {"sides", QJsonObject{{"type", "integer"}, {"minimum", 3}, {"maximum", 256}}}},
             {"body", "origin", "normal", "xAxis", "radius", "sides"}),
        spec("geometry.polyline", "Draw sampled polyline", "Geometry",
             {{"body", stableId(true)},
              {"space", space},
              {"origin", point},
              {"normal", point},
              {"points", list(point, 2, 512)},
              {"closed", QJsonObject{{"type", "boolean"}}}},
             {"body", "origin", "normal", "points", "closed"}),
        spec("geometry.wire", "Draw loose edge", "Geometry",
             {{"body", stableId(true)}, {"start", point}, {"end", point}},
             {"body", "start", "end"}),
        spec("geometry.split_edge", "Split edge", "Geometry",
             {{"body", stableId()},
              {"edge", stableId()},
              {"fraction",
               QJsonObject{{"type", "number"}, {"exclusiveMinimum", 0}, {"exclusiveMaximum", 1}}}},
             {"body", "edge", "fraction"}),
        spec("geometry.erase_face", "Erase face, retain boundary", "Geometry",
             {{"body", stableId()}, {"face", stableId()}}, {"body", "face"}),
        spec("geometry.erase_edge", "Erase edge or join coplanar faces", "Geometry",
             {{"body", stableId()}, {"edge", stableId()}}, {"body", "edge"}),
        spec("geometry.heal_face", "Heal closed planar region", "Geometry",
             {{"body", stableId()}, {"edge", stableId()}, {"origin", point}, {"normal", point}},
             {"body", "edge", "origin", "normal"}),
        spec("geometry.cleanup", "Merge coincident topology", "Geometry", {{"body", stableId()}},
             {"body"}),
        spec("geometry.push_pull", "Push/pull selected face", "Geometry",
             {{"body", stableId()}, {"face", stableId()}, {"distance", number()}},
             {"body", "face", "distance"}),
        spec("geometry.extrude_isolated", "Extrude isolated face", "Geometry",
             {{"body", stableId()}, {"face", stableId()}, {"distance", number()}},
             {"body", "face", "distance"}),
        spec("geometry.translate", "Move selection", "Geometry",
             {{"body", stableId()}, {"delta", point}}, {"body", "delta"}),
        spec("geometry.delete", "Delete selection", "Geometry", {{"body", stableId()}}, {"body"}),
        spec("material.color", "Paint selection", "Materials",
             {{"body", stableId()},
              {"color", list({{"type", "number"}, {"minimum", 0}, {"maximum", 1}}, 3, 3)}},
             {"body", "color"}),
        spec("scene.transform", "Set object transform", "Scene",
             {{"body", stableId()}, {"matrix", list(number(), 16, 16)}, {"parent", stableId(true)}},
             {"body", "matrix"})};
}
QJsonObject commandDescription(const QString &name) {
    for (const auto &item : commandCatalog()) {
        const auto command = item.toObject();
        if (command["name"] == name)
            return command;
    }
    throw std::runtime_error("Unavailable command");
}
} // namespace sketchy
