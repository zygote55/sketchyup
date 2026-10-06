#include "io/annotations_io.hpp"
#include <QJsonObject>
#include <QStringList>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    require(value.isObject(), "Annotation requires an object");
    const auto result = value.toObject();
    require(result.size() == fields.size(), "Annotation object has missing or unknown fields");
    for (const auto &field : fields)
        require(result.contains(field), "Missing annotation field");
    return result;
}
QJsonArray array(const QJsonValue &value, qsizetype size) {
    require(value.isArray() && value.toArray().size() == size, "Invalid annotation array shape");
    return value.toArray();
}
Id id(const QJsonValue &value, bool zero = false) {
    bool valid{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&valid);
    require(value.isString() && valid && (zero || result) && QString::number(result) == text,
            "Annotation ID requires canonical decimal string");
    return result;
}
double number(const QJsonValue &value) {
    require(value.isDouble() && std::isfinite(value.toDouble()),
            "Annotation requires finite number");
    return value.toDouble();
}
std::string text(const QJsonValue &value) {
    require(value.isString(), "Annotation requires text");
    return value.toString().toStdString();
}
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
Vec3 point(const QJsonValue &value) {
    const auto a = array(value, 3);
    return {number(a[0]), number(a[1]), number(a[2])};
}
const char *kind(AnchorKind kind) {
    switch (kind) {
    case AnchorKind::Point:
        return "point";
    case AnchorKind::Vertex:
        return "vertex";
    case AnchorKind::Edge:
        return "edge";
    case AnchorKind::Face:
        return "face";
    }
    throw std::runtime_error("Invalid anchor kind");
}
const char *state(AnchorState state) {
    switch (state) {
    case AnchorState::Resolved:
        return "resolved";
    case AnchorState::Missing:
        return "missing";
    case AnchorState::Ambiguous:
        return "ambiguous";
    }
    throw std::runtime_error("Invalid anchor state");
}
QJsonObject encode(const AnnotationAnchor &a) {
    return {{"kind", kind(a.kind)},
            {"body", QString::number(a.body)},
            {"entity", QString::number(a.entity)},
            {"parameter", a.parameter},
            {"vertices", QJsonArray{QString::number(a.vertices[0]), QString::number(a.vertices[1]),
                                    QString::number(a.vertices[2])}},
            {"weights", QJsonArray{a.weights[0], a.weights[1], a.weights[2]}},
            {"fallback", point(a.fallback)},
            {"state", state(a.state)}};
}
AnnotationAnchor decode(const QJsonValue &value) {
    const auto o = object(
        value, {"kind", "body", "entity", "parameter", "vertices", "weights", "fallback", "state"});
    AnnotationAnchor a;
    const auto k = o["kind"];
    if (k == "point")
        a.kind = AnchorKind::Point;
    else if (k == "vertex")
        a.kind = AnchorKind::Vertex;
    else if (k == "edge")
        a.kind = AnchorKind::Edge;
    else if (k == "face")
        a.kind = AnchorKind::Face;
    else
        throw std::runtime_error("Unknown annotation anchor kind");
    const auto s = o["state"];
    if (s == "resolved")
        a.state = AnchorState::Resolved;
    else if (s == "missing")
        a.state = AnchorState::Missing;
    else if (s == "ambiguous")
        a.state = AnchorState::Ambiguous;
    else
        throw std::runtime_error("Unknown annotation anchor state");
    a.body = id(o["body"], true);
    a.entity = id(o["entity"], true);
    a.parameter = number(o["parameter"]);
    a.fallback = point(o["fallback"]);
    const auto vertices = array(o["vertices"], 3), weights = array(o["weights"], 3);
    for (size_t i = 0; i < 3; ++i) {
        a.vertices[i] = id(vertices[qsizetype(i)], true);
        a.weights[i] = number(weights[qsizetype(i)]);
    }
    validateAnnotationAnchor(a);
    return a;
}
} // namespace
QJsonArray encodeAnnotations(const AnnotationRecords &records) {
    QJsonArray result;
    for (const auto &[id, record] : records) {
        QJsonArray anchors;
        for (const auto &anchor : record->anchors)
            anchors.append(encode(anchor));
        result.append(QJsonObject{
            {"id", QString::number(id)},
            {"name", QString::fromStdString(record->name)},
            {"kind", record->kind == AnnotationKind::Distance ? "distance" : "label"},
            {"anchors", anchors},
            {"offset", point(record->offset)},
            {"text", QString::fromStdString(record->text)},
            {"leader", record->leader},
            {"textSize", record->textSize},
            {"color", QJsonArray{record->color[0], record->color[1], record->color[2]}}});
    }
    return result;
}
AnnotationRecords decodeAnnotations(const QJsonValue &value, Id next) {
    require(value.isArray() && value.toArray().size() <= qsizetype(annotationRecordLimit),
            "Annotation table exceeds shape or count bounds");
    AnnotationRecords result;
    for (const auto entry : value.toArray()) {
        const auto o = object(entry, {"id", "name", "kind", "anchors", "offset", "text", "leader",
                                      "textSize", "color"});
        AnnotationRecord record;
        record.id = id(o["id"]);
        record.name = text(o["name"]);
        if (o["kind"] == "distance")
            record.kind = AnnotationKind::Distance;
        else if (o["kind"] == "label")
            record.kind = AnnotationKind::Label;
        else
            throw std::runtime_error("Unknown annotation kind");
        const auto anchors = array(o["anchors"], record.kind == AnnotationKind::Distance ? 2 : 1);
        for (const auto a : anchors)
            record.anchors.push_back(decode(a));
        record.offset = point(o["offset"]);
        record.text = text(o["text"]);
        record.textSize = number(o["textSize"]);
        require(o["leader"].isBool(), "Annotation leader requires boolean");
        record.leader = o["leader"].toBool();
        const auto color = array(o["color"], 3);
        for (size_t i = 0; i < 3; ++i) {
            const auto channel = number(color[qsizetype(i)]);
            require(channel >= 0 && channel <= 1, "Annotation color outside [0,1]");
            record.color[i] = float(channel);
        }
        require(result.emplace(record.id, std::make_shared<AnnotationRecord>(record)).second,
                "Duplicate annotation ID");
    }
    validateAnnotationRecords(result, next);
    return result;
}
} // namespace sketchy
