#include "io/formline.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <numbers>
namespace sketchy {
namespace {
constexpr qsizetype limit = 32 * 1024 * 1024;
QString string(const QJsonObject &object, const char *key, qsizetype maximum,
               bool nonempty = false) {
    const auto value = object.value(key);
    const auto text = value.toString();
    if (!value.isString() || text.size() > maximum || (nonempty && text.isEmpty()) ||
        QString::fromUtf8(text.toUtf8()) != text)
        throw std::runtime_error(std::string("Invalid Formline ") + key);
    return text;
}
double number(const QJsonObject &object, const char *key, bool dimension = false) {
    const auto value = object.value(key);
    const auto result = value.toDouble();
    if (!value.isDouble() || !std::isfinite(result) || std::abs(result) > 10000 ||
        (dimension && result < .01))
        throw std::runtime_error(std::string("Invalid Formline ") + key);
    return result;
}
qsizetype extraFields(const QJsonObject &object, const QStringList &known) {
    qsizetype count = 0;
    for (auto it = object.begin(); it != object.end(); ++it)
        count += !known.contains(it.key());
    return count;
}
} // namespace
FormlineImport importFormline(const QByteArray &bytes) {
    if (bytes.size() > limit)
        throw std::runtime_error("Formline source exceeds 32 MiB");
    QJsonParseError error;
    const auto parsed = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !parsed.isObject())
        throw std::runtime_error("Invalid Formline JSON");
    const auto input = parsed.object();
    if (input.value("format") != "formline" || !input.value("version").isDouble() ||
        input.value("version").toDouble() != 1 || !input.value("objects").isArray() ||
        input.value("objects").toArray().size() > 1000)
        throw std::runtime_error("Not a supported Formline v1 model");
    const auto modelName = string(input, "name", 200);
    FormlineImport result;
    Edit edit{"Import Formline v1", {}};
    auto root = std::make_shared<Body>();
    root->id = 1;
    root->kind = BodyKind::Group;
    root->name = modelName.toStdString();
    root->properties = {{"formline.version", 1.0}, {"formline.units", std::string("m")}};
    edit.changes.push_back({1, nullptr, root});
    Id next = 2, nextMaterial = 1;
    std::set<QString> identities;
    std::map<QString, Id> colors;
    QJsonArray mapping, warnings;
    qsizetype ignored = extraFields(input, {"format", "version", "name", "objects"});
    int boxes = 0, cylinders = 0, normalizedVisibility = 0;
    static const QRegularExpression colorPattern("^#[0-9a-fA-F]{6}$");
    for (const auto value : input.value("objects").toArray()) {
        if (!value.isObject())
            throw std::runtime_error("Invalid Formline object");
        const auto object = value.toObject();
        const auto id = string(object, "id", 100, true);
        const auto name = string(object, "name", 200);
        const auto type = string(object, "type", 16);
        const auto color = string(object, "color", 7).toLower();
        if (!identities.insert(id).second || (type != "box" && type != "cylinder") ||
            !colorPattern.match(color).hasMatch())
            throw std::runtime_error("Invalid Formline identity, type or color");
        const auto x = number(object, "x"), y = number(object, "y"), z = number(object, "z");
        const auto w = number(object, "w", true), h = number(object, "h", true),
                   d = number(object, "d", true);
        const auto rotation = number(object, "rotation");
        auto body = std::make_shared<Body>();
        body->id = next++;
        body->parent = 1;
        body->name = name.toStdString();
        // The prototype used visible !== false, with missing visibility defaulting to true.
        body->hidden = object.value("visible").isBool() && !object.value("visible").toBool();
        normalizedVisibility += object.contains("visible") && !object.value("visible").isBool();
        const auto hex = color.mid(1).toUInt(nullptr, 16);
        body->color = {float((hex >> 16) & 255) / 255, float((hex >> 8) & 255) / 255,
                       float(hex & 255) / 255};
        if (!colors.contains(color)) {
            colors[color] = nextMaterial;
            auto material = std::make_shared<MaterialRecord>();
            material->id = nextMaterial++;
            material->name = "Imported " + color.toStdString();
            material->color = body->color;
            edit.materials.push_back({material->id, nullptr, material});
        }
        body->materials = {colors.at(color), colors.at(color)};
        body->properties = {{"formline.sourceId", id.toStdString()},
                            {"formline.type", type.toStdString()}};
        // C(x,y,z)=(x,-z,y) is a proper rotation, and C*Ry(theta)*C^-1=Rz(theta).
        // The original position used y+h/2 for centered primitives, so y is the base height.
        body->transform = Transform::translation({x, -z, y}) *
                          Transform::rotation({0, 0, 1}, rotation * std::numbers::pi / 180);
        std::vector<Vec3> loop;
        if (type == "box") {
            ++boxes;
            loop = {{-w / 2, -d / 2, 0}, {w / 2, -d / 2, 0}, {w / 2, d / 2, 0}, {-w / 2, d / 2, 0}};
        } else {
            ++cylinders;
            for (unsigned i = 0; i < 48; ++i) {
                const auto angle = 2 * std::numbers::pi * i / 48;
                loop.push_back({w / 2 * std::sin(angle), -w / 2 * std::cos(angle), 0});
            }
            body->properties["formline.sourceDepth"] = d;
            if (d != w)
                warnings.append(QJsonObject{
                    {"code", "cylinder_depth_ignored"},
                    {"sourceId", id},
                    {"diameter", w},
                    {"unusedDepth", d},
                    {"message", "The prototype used width as cylinder diameter and ignored depth; "
                                "imported geometry preserves that shape."}});
        }
        const auto base = body->surface.addFace({loop});
        body->surface.extrude(base, h);
        body->topology = Topology::rebuild(body->surface, {});
        edit.changes.push_back({body->id, nullptr, body});
        mapping.append(QJsonObject{{"sourceId", id}, {"body", QString::number(body->id)}});
        ignored += extraFields(object, {"id", "type", "name", "color", "visible", "x", "y", "z",
                                        "w", "h", "d", "rotation"});
    }
    if (ignored)
        warnings.append(
            QJsonObject{{"code", "extra_fields_ignored"},
                        {"count", ignored},
                        {"message", "Extra fields were ignored, matching the prototype loader."}});
    if (normalizedVisibility)
        warnings.append(QJsonObject{{"code", "visibility_normalized"},
                                    {"count", normalizedVisibility},
                                    {"message", "Non-boolean visibility values were treated as "
                                                "visible, matching the prototype loader."}});
    result.document.apply(std::move(edit), result.document.revision());
    // Reject expanded models outside native save limits before presenting an unsaved import.
    (void)encodeDocument(result.document, AssetStorage::External);
    result.report = {{"format", "formline"},
                     {"version", 1},
                     {"name", modelName},
                     {"objects", mapping.size()},
                     {"boxes", boxes},
                     {"cylinders", cylinders},
                     {"cylinderSegments", 48},
                     {"units", "m"},
                     {"axisConversion", "(x, y, z) -> (x, -z, y)"},
                     {"root", "1"},
                     {"mapping", mapping},
                     {"warnings", warnings}};
    return result;
}
FormlineImport loadFormline(const QString &path) {
    QFile file(path);
    if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly) || file.isSequential() ||
        file.size() > limit)
        throw std::runtime_error("Cannot read Formline source or it exceeds 32 MiB");
    const auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > limit)
        throw std::runtime_error("Cannot read bounded Formline source");
    return importFormline(bytes);
}
} // namespace sketchy
