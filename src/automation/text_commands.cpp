#include "automation/text_commands.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include "io/text_source_io.hpp"
#include "text/text_worker.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <set>
namespace sketchy {
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Id identity(const QJsonValue &value, bool zero = false) {
    bool valid{};
    const auto s = value.toString();
    const auto id = s.toULongLong(&valid);
    require(value.isString() && valid && (id || zero) && QString::number(id) == s,
            "Text requires a canonical decimal identity");
    return id;
}
std::string name(const QJsonValue &value) {
    require(value.isString(), "Text object name requires a string");
    const auto s = value.toString();
    require(!s.trimmed().isEmpty() && s.isValidUtf16() && s.toUtf8().size() <= 1024 &&
                !s.contains(QChar::Null),
            "Text object name is empty or exceeds bounds");
    return s.toUtf8().toStdString();
}
TextGeometrySettings sourceSettings(const TextSource &s) {
    return {QString::fromStdString(s.text),
            QString::fromStdString(s.family),
            QString::fromStdString(s.style),
            s.height,
            s.depth,
            s.lineSpacing,
            s.allowSubstitution};
}
Surface combine(const TextGeometry &geometry, Id first) {
    Surface result;
    result.nextId = first;
    auto allocate = [&] {
        require(result.nextId && result.nextId < UINT64_MAX,
                "Text geometry identity space exhausted");
        return result.nextId++;
    };
    for (const auto &region : geometry.regions) {
        std::map<Id, Id> vertices;
        for (const auto &[id, point] : region.vertices) {
            const auto target = allocate();
            vertices[id] = target;
            result.vertices[target] = point;
        }
        for (const auto &[_, old] : region.faces) {
            Face face;
            face.id = allocate();
            for (const auto &loop : old.loops) {
                auto &output = face.loops.emplace_back();
                for (const auto vertex : loop)
                    output.push_back(vertices.at(vertex));
            }
            result.faces[face.id] = std::move(face);
        }
    }
    result.validate();
    return result;
}
TextSource sourceRecord(const TextGeometrySettings &s, const TextGeometry &geometry,
                        const Body &body) {
    TextSource result;
    result.text = s.text.toUtf8().toStdString();
    result.family = s.family.toUtf8().toStdString();
    result.style = s.style.toUtf8().toStdString();
    result.height = s.height;
    result.depth = s.depth;
    result.lineSpacing = s.lineSpacing;
    result.allowSubstitution = s.allowSubstitution;
    result.actualFamily = geometry.actualFamily.toUtf8().toStdString();
    result.actualStyle = geometry.actualStyle.toUtf8().toStdString();
    result.substituted = geometry.substituted;
    result.fallback = geometry.fallback;
    for (const auto &font : geometry.fonts)
        result.fonts.push_back({font.family.toUtf8().toStdString(),
                                font.style.toUtf8().toStdString(), font.fingerprint.toStdString(),
                                font.glyphs});
    result.geometryDigest = textGeometryDigest(body);
    result.regions = geometry.regions.size();
    validateTextSource(result);
    return result;
}
} // namespace
std::string textGeometryDigest(const Body &body) {
    auto copy = std::make_shared<Body>(body);
    copy->textSource.reset();
    const auto encoded = encodeBodies({{copy->id, copy}})[0].toObject();
    QJsonObject shape;
    for (const auto *key : {"vertices", "faces", "wires", "curves", "guides"})
        shape[key] = encoded[key];
    return QCryptographicHash::hash(QJsonDocument(shape).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}
bool isTextCommand(const QString &name) {
    return name == "text.create" || name == "text.update" || name == "text.bake";
}
ChangeReport executeTextCommand(Document &doc, const QJsonObject &command) {
    const auto operation = command["command"].toString();
    require(isTextCommand(operation), "Unknown editable text command");
    const bool creating = operation == "text.create", baking = operation == "text.bake";
    BodyPtr before;
    if (!creating) {
        const auto id = identity(command["body"]);
        require(doc.bodies().contains(id), "Text body does not exist");
        before = doc.bodies().at(id);
        require(before->textSource.has_value(), "Body does not carry editable text source");
        require(!persistentlyLocked(doc, id), "Text body is locked");
    }
    if (baking) {
        require(command.size() == 2, "Text bake has unknown parameters");
        auto body = std::make_shared<Body>(*before);
        body->textSource.reset();
        return doc.apply({"Bake text geometry", {{body->id, before, body}}}, doc.revision());
    }
    auto settings =
        encodeTextSettings(before ? sourceSettings(*before->textSource) : TextGeometrySettings{});
    bool changedSetting = false;
    for (auto it = command.begin(); it != command.end(); ++it) {
        if (settings.contains(it.key())) {
            settings[it.key()] = it.value();
            changedSetting = true;
        } else
            require(it.key() == "command" || it.key() == "name" ||
                        (creating && (it.key() == "parent" || it.key() == "position")) ||
                        (!creating && (it.key() == "body" || it.key() == "acceptFontChange" ||
                                       it.key() == "regenerate")),
                    "Unknown editable text parameter");
    }
    if (command.contains("regenerate"))
        require(command["regenerate"].isBool(), "Regeneration requires a boolean");
    const bool regenerate = command["regenerate"].toBool();
    require(creating || changedSetting || command.contains("name") || regenerate,
            "Text update has no properties");
    if (command.contains("acceptFontChange"))
        require(command["acceptFontChange"].isBool(), "Font change consent requires a boolean");
    const auto requested = decodeTextSettings(settings);
    auto body = before ? std::make_shared<Body>(*before) : std::make_shared<Body>();
    if (creating) {
        require(command.contains("text") && command.contains("family") &&
                    command.contains("height") && command.contains("name"),
                "Text creation requires name, text, family and height");
        body->id = doc.nextId();
        body->kind = BodyKind::Group;
        if (command.contains("parent"))
            body->parent = identity(command["parent"], true);
        if (body->parent) {
            require(doc.bodies().contains(body->parent) &&
                        doc.bodies().at(body->parent)->kind == BodyKind::Group,
                    "Text parent must be an existing group");
            require(!persistentlyLocked(doc, body->parent), "Text parent is locked");
        }
        if (command.contains("position")) {
            require(command["position"].isArray(), "Text position requires three coordinates");
            const auto p = command["position"].toArray();
            require(p.size() == 3, "Text position requires three coordinates");
            for (const auto n : p)
                require(n.isDouble() && std::isfinite(n.toDouble()),
                        "Text position requires finite numbers");
            Vec3 position{p[0].toDouble(), p[1].toDouble(), p[2].toDouble()};
            checkPoint(position);
            body->transform = Transform::translation(position);
        }
    }
    if (command.contains("name"))
        body->name = name(command["name"]);
    if (!creating && !regenerate &&
        settings == encodeTextSettings(sourceSettings(*before->textSource))) {
        if (*body == *before)
            return {};
        return doc.apply({"Rename text", {{body->id, before, body}}}, doc.revision());
    }
    if (before) {
        require(textGeometryDigest(*before) == before->textSource->geometryDigest,
                "Text geometry was edited independently; restore it or bake its source before "
                "replacing it");
        require(before->edgeAppearances.empty() && before->faceColors.empty() &&
                    before->faceMaterials.empty() && before->faceTextureMappings.empty(),
                "Text has per-face or per-edge appearance; bake or clear it before regeneration");
    }
    const auto geometry = runTextWorker(requested);
    if (before && !command["acceptFontChange"].toBool()) {
        for (const auto &old : before->textSource->fonts)
            for (const auto &now : geometry.fonts)
                require(old.family != now.family.toStdString() ||
                            old.style != now.style.toStdString() ||
                            old.fingerprint == now.fingerprint.toStdString(),
                        "A previously used font changed; explicitly accept the font change before "
                        "regeneration");
    }
    body->surface = combine(geometry, before ? before->surface.nextId : 1);
    body->topology = {};
    if (before)
        body->topology.nextId = before->topology.nextId;
    body->textSource = sourceRecord(requested, geometry, *body);
    Change change{body->id, before, body};
    if (before) {
        // Regeneration retires every old glyph identity. Coincident new edges
        // have no semantic lineage to the old text and must not be guessed.
        for (const auto &[id, _] : before->surface.vertices)
            change.vertexDescendants[id] = {};
        for (const auto &[id, _] : before->surface.faces)
            change.faceDescendants[id] = {};
        for (const auto &[id, _] : before->topology.edges)
            change.edgeDescendants[id] = {};
    }
    return doc.apply({creating ? "Create 3D text" : "Edit 3D text", {std::move(change)}},
                     doc.revision());
}
QJsonObject textSettingsProperties() {
    return {{"text", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 4096}}},
            {"family", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
            {"style", QJsonObject{{"type", "string"}, {"maxLength", 256}}},
            {"height", QJsonObject{{"type", "number"}, {"minimum", .001}, {"maximum", 1000}}},
            {"depth", QJsonObject{{"oneOf", QJsonArray{QJsonObject{{"const", 0}},
                                                       QJsonObject{{"type", "number"},
                                                                   {"minimum", 1e-6},
                                                                   {"maximum", 1000}}}}}},
            {"lineSpacing", QJsonObject{{"type", "number"}, {"minimum", .5}, {"maximum", 10}}},
            {"allowSubstitution", QJsonObject{{"type", "boolean"}}},
            {"name", QJsonObject{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}}};
}
QJsonObject textDescription(const Document &doc, Id id) {
    require(doc.bodies().contains(id) && doc.bodies().at(id)->textSource.has_value(),
            "Editable text body does not exist");
    const auto &body = *doc.bodies().at(id);
    return {{"body", QString::number(id)},
            {"name", QString::fromStdString(body.name)},
            {"source", encodeTextSource(*body.textSource)},
            {"geometryMatchesSource", textGeometryDigest(body) == body.textSource->geometryDigest},
            {"fontAvailability", "unchecked; cached geometry does not require local fonts"},
            {"regenerationRequiresLocalFonts", true}};
}
} // namespace sketchy
