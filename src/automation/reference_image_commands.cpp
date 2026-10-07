#include "automation/reference_image_commands.hpp"
#include "core/groups.hpp"
#include "core/reference_images.hpp"
#include "io/reference_image_io.hpp"
#include "io/texture_image.hpp"
#include <QJsonArray>
#include <QStringList>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Id identity(const QJsonValue &value, bool zero = false) {
    bool valid{};
    const auto token = value.toString();
    const auto id = token.toULongLong(&valid);
    require(value.isString() && valid && (id || zero) && QString::number(id) == token,
            "Reference image needs a canonical decimal identity");
    return id;
}
QJsonArray coordinates(const QJsonValue &value, int size) {
    require(value.isArray() && value.toArray().size() == size,
            "Reference image coordinates have the wrong size");
    const auto values = value.toArray();
    for (const auto &number : values)
        require(number.isDouble() && std::isfinite(number.toDouble()),
                "Reference image coordinates must be finite numbers");
    return values;
}
std::string name(const QJsonValue &value) {
    const auto text = value.toString();
    require(value.isString() && !text.trimmed().isEmpty() && text.isValidUtf16() &&
                !text.contains(QChar::Null) && text.toUtf8().size() <= 1024,
            "Reference image name is empty or exceeds bounds");
    return text.toUtf8().toStdString();
}
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
} // namespace
bool isReferenceImageCommand(const QString &name) {
    return name == "reference_image.create" || name == "reference_image.update" ||
           name == "reference_image.calibrate";
}
ChangeReport executeReferenceImageCommand(Document &doc, const QJsonObject &command) {
    const auto operation = command["command"].toString();
    require(isReferenceImageCommand(operation), "Unknown reference image command");
    const bool creating = operation == "reference_image.create";
    const bool calibrating = operation == "reference_image.calibrate";
    QStringList required = creating ? QStringList{"command", "name", "asset", "width", "height"}
                                    : QStringList{"command", "body"};
    QStringList optional;
    if (calibrating)
        required += {"first", "second", "knownLength"};
    else if (creating)
        optional = {"opacity", "position", "parent"};
    else
        optional = {"name", "asset", "width", "height", "opacity"};
    for (const auto &key : required)
        require(command.contains(key), "Reference image command is missing a required field");
    for (auto it = command.begin(); it != command.end(); ++it)
        require(required.contains(it.key()) || optional.contains(it.key()),
                "Reference image command has an unknown field");
    BodyPtr before;
    if (!creating) {
        const auto id = identity(command["body"]);
        require(doc.bodies().contains(id) && doc.bodies().at(id)->referenceImage,
                "Reference image body does not exist");
        require(!persistentlyLocked(doc, id), "Reference image is locked");
        before = doc.bodies().at(id);
    }
    if (calibrating) {
        const auto first = coordinates(command["first"], 2),
                   second = coordinates(command["second"], 2);
        require(command["knownLength"].isDouble(), "Calibration length requires a number");
        return calibrateReferenceImage(doc, before->id, {first[0].toDouble(), first[1].toDouble()},
                                       {second[0].toDouble(), second[1].toDouble()},
                                       command["knownLength"].toDouble());
    }
    require(creating || command.size() > 2, "Reference image update has no properties");
    auto body = creating ? std::make_shared<Body>() : std::make_shared<Body>(*before);
    auto image = creating ? QJsonObject{{"asset", command["asset"]},
                                        {"width", command["width"]},
                                        {"height", command["height"]},
                                        {"opacity", 1}}
                          : encodeReferenceImage(*before->referenceImage);
    for (const auto &key : {"asset", "width", "height", "opacity"})
        if (command.contains(key))
            image[key] = command[key];
    body->referenceImage = decodeReferenceImage(image);
    if (creating) {
        body->id = doc.nextId();
        body->kind = BodyKind::ReferenceImage;
        if (command.contains("parent"))
            body->parent = identity(command["parent"], true);
        if (body->parent) {
            require(doc.bodies().contains(body->parent) &&
                        doc.bodies().at(body->parent)->kind == BodyKind::Group,
                    "Reference image parent must be a group");
            require(!persistentlyLocked(doc, body->parent), "Reference image parent is locked");
        }
        if (command.contains("position")) {
            const auto p = coordinates(command["position"], 3);
            body->transform =
                Transform::translation({p[0].toDouble(), p[1].toDouble(), p[2].toDouble()});
        }
    }
    if (command.contains("name"))
        body->name = name(command["name"]);
    if (before && *body == *before)
        return {};
    return doc.apply(
        {creating ? "Create reference image" : "Edit reference image", {{body->id, before, body}}},
        doc.revision());
}
QJsonObject referenceImageProperties() {
    return {
        {"name", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}},
        {"asset", QJsonObject{{"type", "string"}, {"maxLength", 20}, {"pattern", "^[1-9][0-9]*$"}}},
        {"width", QJsonObject{{"type", "number"}, {"minimum", 1e-6}, {"maximum", 1e6}}},
        {"height", QJsonObject{{"type", "number"}, {"minimum", 1e-6}, {"maximum", 1e6}}},
        {"opacity", QJsonObject{{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}};
}
QJsonObject referenceImageDescription(const Document &doc, Id id, bool decodePixels) {
    const auto &body = *doc.bodies().at(id);
    require(body.referenceImage.has_value(), "Entity is not a reference image");
    const auto &asset = *doc.assets().at(body.referenceImage->asset);
    QJsonArray corners;
    for (const auto p : referenceImageCorners(*body.referenceImage, doc.worldTransform(id)))
        corners.append(point(p));
    QJsonObject result{
        {"body", QString::number(id)},
        {"name", QString::fromStdString(body.name)},
        {"settings", encodeReferenceImage(*body.referenceImage)},
        {"lengthUnit", "m"},
        {"worldCorners", corners},
        {"coordinateConvention", "normalized top-left origin; +U right, +V down"},
        {"asset", QJsonObject{{"id", QString::number(asset.id)},
                              {"name", QString::fromStdString(asset.name)},
                              {"mediaType", QString::fromStdString(asset.mediaType)},
                              {"present", bool(asset.payload)},
                              {"bytes", QString::number(
                                            asset.payload ? asset.payload->bytes().size() : 0)}}}};
    if (decodePixels) {
        const auto decoded = decodeTextureImage(asset);
        const auto status = textureImageStatusName(decoded.status);
        result["pixels"] =
            QJsonObject{{"status", QString::fromLatin1(status.data(), status.size())},
                        {"width", decoded.image ? QJsonValue(decoded.image->width())
                                                : QJsonValue(QJsonValue::Null)},
                        {"height", decoded.image ? QJsonValue(decoded.image->height())
                                                 : QJsonValue(QJsonValue::Null)}};
    }
    return result;
}
} // namespace sketchy
