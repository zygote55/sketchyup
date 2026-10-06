#include "io/scenes_io.hpp"
#include "io/model_style_io.hpp"
#include <QStringList>
namespace sketchy {
namespace {
QJsonObject object(const QJsonValue &value, const QStringList &fields) {
    if (!value.isObject())
        throw std::runtime_error("Scene requires an object");
    const auto result = value.toObject();
    if (result.size() != fields.size())
        throw std::runtime_error("Scene object has missing or unknown fields");
    for (const auto &field : fields)
        if (!result.contains(field))
            throw std::runtime_error("Missing scene field");
    return result;
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Scene requires finite numeric values");
    return value.toDouble();
}
bool flag(const QJsonValue &value) {
    if (!value.isBool())
        throw std::runtime_error("Scene requires boolean flags");
    return value.toBool();
}
Id id(const QJsonValue &value) {
    bool ok{};
    const auto text = value.toString();
    const auto result = text.toULongLong(&ok);
    if (!value.isString() || !ok || !result || QString::number(result) != text)
        throw std::runtime_error("Scene identity requires a canonical nonzero decimal string");
    return result;
}
QJsonArray array(const QJsonValue &value, qsizetype maximum) {
    if (!value.isArray() || value.toArray().size() > maximum)
        throw std::runtime_error("Scene array exceeds its shape or reference budget");
    return value.toArray();
}
QString kindCode(SceneEntityKind kind) {
    switch (kind) {
    case SceneEntityKind::Body:
        return "body";
    case SceneEntityKind::Face:
        return "face";
    case SceneEntityKind::Edge:
        return "edge";
    case SceneEntityKind::Guide:
        return "guide";
    }
    throw std::runtime_error("Unknown scene entity kind");
}
SceneEntityKind kindValue(const QJsonValue &value) {
    if (value == "body")
        return SceneEntityKind::Body;
    if (value == "face")
        return SceneEntityKind::Face;
    if (value == "edge")
        return SceneEntityKind::Edge;
    if (value == "guide")
        return SceneEntityKind::Guide;
    throw std::runtime_error("Unknown scene entity kind");
}
QJsonObject encodeEntity(const SceneEntity &entity) {
    return {{"body", QString::number(entity.body)},
            {"kind", kindCode(entity.kind)},
            {"entity", entity.kind == SceneEntityKind::Body
                           ? QJsonValue(QJsonValue::Null)
                           : QJsonValue(QString::number(entity.entity))}};
}
QJsonArray encodeFlags(const std::map<Id, bool> &flags) {
    QJsonArray result;
    for (const auto &[key, visible] : flags)
        result.append(QJsonObject{{"id", QString::number(key)}, {"visible", visible}});
    return result;
}
std::map<Id, bool> decodeFlags(const QJsonValue &value, qsizetype maximum) {
    std::map<Id, bool> result;
    for (const auto &entry : array(value, maximum)) {
        const auto record = object(entry, {"id", "visible"});
        if (!result.emplace(id(record["id"]), flag(record["visible"])).second)
            throw std::runtime_error("Repeated scene visibility identity");
    }
    return result;
}
} // namespace
QJsonObject encodeSceneSnapshot(const SceneSnapshot &snapshot) {
    snapshot.validate();
    QJsonObject result;
    if (snapshot.camera) {
        const auto &c = *snapshot.camera;
        result["camera"] = QJsonObject{{"target", QJsonArray{c.target.x, c.target.y, c.target.z}},
                                       {"yaw", c.yaw},
                                       {"pitch", c.pitch},
                                       {"distance", c.distance},
                                       {"fieldOfView", c.fieldOfView},
                                       {"orthographic", c.orthographic}};
    }
    if (snapshot.visibility) {
        const auto &v = *snapshot.visibility;
        QJsonArray hidden;
        for (const auto &entity : v.hiddenEntities)
            hidden.append(encodeEntity(entity));
        result["visibility"] = QJsonObject{{"bodies", encodeFlags(v.bodyVisible)},
                                           {"tags", encodeFlags(v.tagVisible)},
                                           {"hidden", hidden},
                                           {"showHidden", v.showHidden}};
    }
    if (snapshot.style)
        result["style"] = encodeModelStyle(*snapshot.style);
    if (snapshot.section) {
        QJsonValue plane(QJsonValue::Null);
        if (snapshot.section->plane) {
            const auto &p = *snapshot.section->plane;
            plane = QJsonArray{p[0], p[1], p[2], p[3]};
        }
        result["section"] = QJsonObject{{"plane", plane}};
    }
    return result;
}
SceneSnapshot decodeSceneSnapshot(const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Scene snapshot requires an object");
    const auto fields = value.toObject();
    const QStringList allowed{"camera", "visibility", "style", "section"};
    for (auto it = fields.begin(); it != fields.end(); ++it)
        if (!allowed.contains(it.key()))
            throw std::runtime_error("Unknown scene snapshot field");
    SceneSnapshot result;
    if (fields.contains("camera")) {
        const auto c = object(fields["camera"], {"target", "yaw", "pitch", "distance",
                                                 "fieldOfView", "orthographic"});
        const auto target = array(c["target"], 3);
        if (target.size() != 3)
            throw std::runtime_error("Scene camera target requires three coordinates");
        result.camera = SceneCamera{{number(target[0]), number(target[1]), number(target[2])},
                                    number(c["yaw"]),
                                    number(c["pitch"]),
                                    number(c["distance"]),
                                    number(c["fieldOfView"]),
                                    flag(c["orthographic"])};
    }
    if (fields.contains("visibility")) {
        const auto v = object(fields["visibility"], {"bodies", "tags", "hidden", "showHidden"});
        SceneVisibility visibility{decodeFlags(v["bodies"], 10000),
                                   decodeFlags(v["tags"], 1024),
                                   {},
                                   flag(v["showHidden"])};
        for (const auto &entry : array(v["hidden"], 50000)) {
            const auto e = object(entry, {"body", "kind", "entity"});
            const auto kind = kindValue(e["kind"]);
            if (kind == SceneEntityKind::Body && !e["entity"].isNull())
                throw std::runtime_error("Body scene references require a null entity");
            SceneEntity entity{id(e["body"]), kind,
                               kind == SceneEntityKind::Body ? 0 : id(e["entity"])};
            entity.validate();
            if (!visibility.hiddenEntities.insert(entity).second)
                throw std::runtime_error("Repeated hidden scene entity");
        }
        result.visibility = std::move(visibility);
    }
    if (fields.contains("style"))
        result.style = decodeModelStyle(fields["style"]);
    if (fields.contains("section")) {
        const auto section = object(fields["section"], {"plane"});
        SceneSection s;
        if (!section["plane"].isNull()) {
            const auto p = array(section["plane"], 4);
            if (p.size() != 4)
                throw std::runtime_error("Scene section plane requires four coefficients");
            s.plane = std::array<double, 4>{number(p[0]), number(p[1]), number(p[2]), number(p[3])};
        }
        result.section = s;
    }
    result.validate();
    return result;
}
QJsonArray encodeScenes(const SceneRecords &scenes) {
    QJsonArray result;
    for (const auto &[id, scene] : scenes)
        result.append(QJsonObject{{"id", QString::number(id)},
                                  {"name", QString::fromStdString(scene->name)},
                                  {"position", int(scene->position)},
                                  {"snapshot", encodeSceneSnapshot(scene->snapshot)}});
    return result;
}
SceneRecords decodeScenes(const QJsonValue &value, Id nextSceneId) {
    SceneRecords result;
    size_t bytes = 0;
    for (const auto &entry : array(value, sceneCountLimit)) {
        const auto record = object(entry, {"id", "name", "position", "snapshot"});
        if (!record["name"].isString())
            throw std::runtime_error("Scene name requires text");
        const auto position = number(record["position"]);
        if (position < 0 || position >= sceneCountLimit || position != std::floor(position))
            throw std::runtime_error("Invalid scene order position");
        auto scene = std::make_shared<SceneRecord>(SceneRecord{
            id(record["id"]), record["name"].toString().toStdString(),
            static_cast<std::uint32_t>(position), decodeSceneSnapshot(record["snapshot"])});
        bytes += sceneBytes(scene);
        if (bytes > sceneBytesLimit)
            throw std::runtime_error("Scenes exceed eight MiB record budget");
        if (!result.emplace(scene->id, scene).second)
            throw std::runtime_error("Repeated scene identity");
    }
    validateSceneRecords(result, nextSceneId);
    return result;
}
QJsonObject encodeMissingSceneReferences(const MissingSceneReferences &missing) {
    QJsonArray bodies, tags, entities;
    for (auto id : missing.bodies)
        bodies.append(QString::number(id));
    for (auto id : missing.tags)
        tags.append(QString::number(id));
    for (const auto &entity : missing.entities)
        entities.append(encodeEntity(entity));
    return {{"bodies", bodies}, {"tags", tags}, {"entities", entities}};
}
} // namespace sketchy
