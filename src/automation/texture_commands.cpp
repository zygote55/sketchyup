#include "automation/texture_commands.hpp"
#include "core/face_textures.hpp"
#include <QJsonArray>
#include <QStringList>

namespace sketchy {
namespace {
Id identity(const QJsonValue &value) {
    bool ok = false;
    const auto id = value.toString().toULongLong(&ok);
    if (!value.isString() || !ok || !id || QString::number(id) != value.toString())
        throw std::runtime_error("Expected a canonical stable ID string");
    return id;
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Texture mapping requires finite numbers");
    return value.toDouble();
}
QJsonArray array(const QJsonValue &value, int size) {
    if (!value.isArray() || value.toArray().size() != size)
        throw std::runtime_error("Texture mapping coordinate array has the wrong size");
    return value.toArray();
}
Vec3 point(const QJsonValue &value) {
    const auto values = array(value, 3);
    return {number(values[0]), number(values[1]), number(values[2])};
}
TextureCoordinate uv(const QJsonValue &value) {
    const auto values = array(value, 2);
    return {number(values[0]), number(values[1])};
}
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
QJsonObject mapping(const TextureMapping &value) {
    return {{"origin", point(value.origin)},
            {"uGradient", point(value.uGradient)},
            {"vGradient", point(value.vGradient)},
            {"offset", QJsonArray{value.offset.u, value.offset.v}}};
}
void fields(const QJsonObject &object, const QStringList &required,
            const QStringList &optional = {}) {
    for (const auto &key : required)
        if (!object.contains(key))
            throw std::runtime_error(("Missing texture projection field: " + key).toStdString());
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!required.contains(it.key()) && !optional.contains(it.key()))
            throw std::runtime_error(
                ("Unknown texture projection field: " + it.key()).toStdString());
}
TextureMapping projection(const QJsonValue &value) {
    if (!value.isObject())
        throw std::runtime_error("Texture projection must be an object or explicit null");
    const auto object = value.toObject();
    if (!object["type"].isString())
        throw std::runtime_error("Texture projection requires a type");
    const auto type = object["type"].toString();
    if (type == "planar") {
        fields(object, {"type", "origin", "normal", "tangent", "width", "height"},
               {"rotationRadians", "offset"});
        return planarTextureMapping(
            point(object["origin"]), point(object["normal"]), point(object["tangent"]),
            number(object["width"]), number(object["height"]),
            object.contains("rotationRadians") ? number(object["rotationRadians"]) : 0,
            object.contains("offset") ? uv(object["offset"]) : TextureCoordinate{});
    }
    if (type == "pins") {
        fields(object, {"type", "points", "coordinates"});
        const auto points = array(object["points"], 3);
        const auto coordinates = array(object["coordinates"], 3);
        return pinnedTextureMapping({point(points[0]), point(points[1]), point(points[2])},
                                    {uv(coordinates[0]), uv(coordinates[1]), uv(coordinates[2])});
    }
    if (type == "affine") {
        fields(object, {"type", "origin", "uGradient", "vGradient", "offset"});
        TextureMapping result{point(object["origin"]), point(object["uGradient"]),
                              point(object["vGradient"]), uv(object["offset"])};
        result.validate();
        return result;
    }
    throw std::runtime_error("Choose planar, pins or affine texture projection");
}
} // namespace
QJsonObject faceTextureDescription(const Body &body, Id face) {
    const auto stored = faceTextureMappings(body, face);
    auto side = [&](bool back) {
        const auto &record = back ? stored.back : stored.front;
        return QJsonObject{
            {"stored", record ? QJsonValue(mapping(*record)) : QJsonValue(QJsonValue::Null)},
            {"effective", mapping(effectiveFaceTextureMapping(body, face, back))}};
    };
    return {{"space", "local"},
            {"lengthUnit", "m"},
            {"gradientUnit", "repeats/m"},
            {"originConvention", "top-left; +V down; unwrapped repeats"},
            {"front", side(false)},
            {"back", side(true)}};
}
ChangeReport executeTextureMappingCommand(Document &doc, const QJsonObject &command) {
    fields(command, {"command", "body", "face", "side", "space", "projection"});
    if (command["command"] != "material.map_texture")
        throw std::runtime_error("Unknown texture mapping command");
    const auto body = identity(command["body"]), face = identity(command["face"]);
    const auto side = command["side"].toString(), space = command["space"].toString();
    if (side != "front" && side != "back" && side != "both")
        throw std::runtime_error("Choose front, back or both texture mapping sides");
    if (space != "local" && space != "world")
        throw std::runtime_error("Choose local or world texture mapping coordinates");
    std::optional<TextureMapping> value;
    if (!command["projection"].isNull()) {
        value = projection(command["projection"]);
        if (space == "world")
            value = transformTextureMapping(*value, doc.worldTransform(body).inverse());
    }
    return assignTextureMapping(doc, body, face, value, side != "back", side != "front");
}
} // namespace sketchy
