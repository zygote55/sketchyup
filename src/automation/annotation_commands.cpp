#include "automation/annotation_commands.hpp"
#include "io/annotations_io.hpp"
#include <QStringList>
namespace sketchy {
namespace {
Id id(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        throw std::runtime_error("Annotation requires a canonical decimal identity");
    return result;
}
void fields(const QJsonObject &object, const QStringList &names) {
    if (object.size() != names.size())
        throw std::runtime_error("Anchor has missing or unknown fields");
    for (const auto &name : names)
        if (!object.contains(name))
            throw std::runtime_error("Missing anchor field");
}
Vec3 point(const QJsonValue &value) {
    const auto a = value.toArray();
    if (!value.isArray() || a.size() != 3)
        throw std::runtime_error("Anchor requires three coordinates in metres");
    for (const auto v : a)
        if (!v.isDouble() || !std::isfinite(v.toDouble()))
            throw std::runtime_error("Anchor coordinate requires a finite number");
    const Vec3 p{a[0].toDouble(), a[1].toDouble(), a[2].toDouble()};
    checkPoint(p);
    return p;
}
AnnotationAnchor anchor(const Document &doc, const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Anchor requires an object");
    const auto o = value.toObject();
    const auto kind = o["kind"];
    if (kind == "point") {
        fields(o, {"kind", "space", "point"});
        if (o["space"] != "world")
            throw std::runtime_error("Fixed annotation point requires world coordinates");
        return pointAnchor(point(o["point"]));
    }
    if (kind == "vertex") {
        fields(o, {"kind", "body", "vertex"});
        return vertexAnchor(doc, id(o["body"]), id(o["vertex"]));
    }
    if (kind == "edge") {
        fields(o, {"kind", "body", "edge", "fraction"});
        if (!o["fraction"].isDouble())
            throw std::runtime_error("Edge fraction requires a number");
        return edgeAnchor(doc, id(o["body"]), id(o["edge"]), o["fraction"].toDouble());
    }
    if (kind == "face") {
        fields(o, {"kind", "body", "face", "space", "point"});
        const auto body = id(o["body"]);
        auto p = point(o["point"]);
        if (o["space"] == "world")
            p = doc.worldTransform(body).inverse().point(p);
        else if (o["space"] != "local")
            throw std::runtime_error("Face anchor requires local or world coordinates");
        return faceAnchor(doc, body, id(o["face"]), p);
    }
    throw std::runtime_error("Unknown annotation anchor kind");
}
const char *state(AnchorState s) {
    switch (s) {
    case AnchorState::Resolved:
        return "resolved";
    case AnchorState::Missing:
        return "missing";
    case AnchorState::Ambiguous:
        return "ambiguous";
    }
    throw std::runtime_error("Invalid annotation state");
}
QJsonObject object(QJsonObject properties, QJsonArray required) {
    return {{"type", "object"},
            {"properties", properties},
            {"required", required},
            {"additionalProperties", false}};
}
QJsonObject list(QJsonObject item, int minimum, int maximum) {
    return {{"type", "array"}, {"items", item}, {"minItems", minimum}, {"maxItems", maximum}};
}
} // namespace
bool isAnnotationCommand(const QString &name) {
    return name == "annotation.create" || name == "annotation.update" ||
           name == "annotation.delete";
}
void executeAnnotationCommand(Document &doc, const QJsonObject &command) {
    const auto operation = command["command"];
    if (operation == "annotation.delete") {
        eraseAnnotation(doc, id(command["annotation"]));
        return;
    }
    const bool updating = operation == "annotation.update";
    if (!updating && operation != "annotation.create")
        throw std::runtime_error("Unknown annotation command");
    AnnotationRecord record;
    if (updating) {
        const auto key = id(command["annotation"]);
        if (!doc.annotations().contains(key))
            throw std::runtime_error("Annotation does not exist");
        record = *doc.annotations().at(key);
        if (command.size() < 3)
            throw std::runtime_error("Annotation update requires a changed property");
    }
    const auto key = record.id;
    record.id = 1; // Strict decoder validates properties without narrowing persistent identities.
    if (command.contains("anchors")) {
        const auto value = command["anchors"];
        if (!value.isArray() || value.toArray().isEmpty() || value.toArray().size() > 2)
            throw std::runtime_error("Annotation requires one or two anchors");
        record.anchors.clear();
        for (const auto a : value.toArray())
            record.anchors.push_back(anchor(doc, a));
    }
    auto encoded =
        encodeAnnotations({{1, std::make_shared<AnnotationRecord>(record)}})[0].toObject();
    for (const auto *field : {"name", "kind", "offset", "text", "leader", "textSize", "color"})
        if (command.contains(field))
            encoded[field] = command[field];
    record = *decodeAnnotations(QJsonArray{encoded}, 2).at(1);
    if (updating) {
        record.id = key;
        updateAnnotation(doc, key, record);
    } else {
        createAnnotation(doc, record);
    }
}
QJsonObject annotationPropertiesSchema() {
    const QJsonObject id{{"type", "string"}, {"pattern", "^[1-9][0-9]*$"}, {"maxLength", 20}};
    const auto p = list(
        {{"type", "number"}, {"minimum", -coordinateLimit}, {"maximum", coordinateLimit}}, 3, 3);
    const QJsonObject world{{"const", "world"}};
    const QJsonObject space{{"type", "string"}, {"enum", QJsonArray{"local", "world"}}};
    const auto fixed =
        object({{"kind", QJsonObject{{"const", "point"}}}, {"space", world}, {"point", p}},
               {"kind", "space", "point"});
    const auto vertex =
        object({{"kind", QJsonObject{{"const", "vertex"}}}, {"body", id}, {"vertex", id}},
               {"kind", "body", "vertex"});
    const auto edge =
        object({{"kind", QJsonObject{{"const", "edge"}}},
                {"body", id},
                {"edge", id},
                {"fraction", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}},
               {"kind", "body", "edge", "fraction"});
    const auto face = object({{"kind", QJsonObject{{"const", "face"}}},
                              {"body", id},
                              {"face", id},
                              {"space", space},
                              {"point", p}},
                             {"kind", "body", "face", "space", "point"});
    auto anchors = list({{"oneOf", QJsonArray{fixed, vertex, edge, face}}}, 1, 2);
    anchors["description"] = "Exactly two anchors for distance or one for label. Providing anchors "
                             "explicitly rebinds; omission on update preserves association and "
                             "broken state. Edge fractions follow canonical a-to-b endpoints.";
    auto offset = p;
    offset["description"] = "World metre vector from anchor or midpoint to text; defaults to zero";
    return {
        {"name", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
        {"kind", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"distance", "label"}}}},
        {"anchors", anchors},
        {"offset", offset},
        {"text", QJsonObject{{"type", "string"},
                             {"maxLength", 4096},
                             {"description",
                              "Label content (required nonempty), or optional distance prefix"}}},
        {"leader", QJsonObject{{"type", "boolean"}}},
        {"textSize", QJsonObject{{"type", "number"}, {"minimum", 8}, {"maximum", 48}}},
        {"color", list({{"type", "number"}, {"minimum", 0}, {"maximum", 1}}, 3, 3)}};
}
QJsonObject annotationDescription(const Document &doc, Id annotation) {
    if (!doc.annotations().contains(annotation))
        throw std::runtime_error("Annotation does not exist");
    const auto &record = *doc.annotations().at(annotation);
    auto result = encodeAnnotations({{annotation, doc.annotations().at(annotation)}})[0].toObject();
    const auto m = measureAnnotation(doc, record);
    QJsonArray resolved;
    for (const auto &a : m.anchors)
        resolved.append(QJsonObject{{"point", QJsonArray{a.point.x, a.point.y, a.point.z}},
                                    {"state", state(a.state)}});
    result["resolvedAnchors"] = resolved;
    result["state"] = state(m.state);
    result["textPoint"] = QJsonArray{m.textPoint.x, m.textPoint.y, m.textPoint.z};
    result["distanceMetres"] = m.distance ? QJsonValue(*m.distance) : QJsonValue::Null;
    result["displayUnits"] = QString::fromUtf8(unitCode(doc.displayUnits()).data());
    return result;
}
} // namespace sketchy
