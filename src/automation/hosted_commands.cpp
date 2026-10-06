#include "automation/hosted_commands.hpp"
#include "core/components.hpp"
#include <QJsonArray>
#include <set>
namespace sketchy {
namespace {
QString sid(Id id) { return QString::number(id); }
Id identity(const QJsonValue &value) {
    bool ok = false;
    const auto text = value.toString();
    const auto id = text.toULongLong(&ok);
    if (!value.isString() || !ok || !id || sid(id) != text)
        throw std::runtime_error("Expected a canonical stable ID string");
    return id;
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Expected a finite number");
    return value.toDouble();
}
Vec3 point(const QJsonValue &value) {
    if (!value.isArray() || value.toArray().size() != 3)
        throw std::runtime_error("Expected three coordinates");
    const auto values = value.toArray();
    return {number(values[0]), number(values[1]), number(values[2])};
}
QJsonArray point(Vec3 p) { return {p.x, p.y, p.z}; }
template <class Records> QJsonArray changed(const Records &before, const Records &after) {
    std::set<Id> ids;
    for (const auto &[id, value] : before)
        if (!after.contains(id) || (value != after.at(id) && *value != *after.at(id)))
            ids.insert(id);
    for (const auto &[id, value] : after)
        if (!before.contains(id))
            ids.insert(id);
    QJsonArray result;
    for (auto id : ids)
        result.append(sid(id));
    return result;
}
} // namespace
QJsonValue componentGlueDescription(const ComponentDefinition &definition) {
    if (!definition.glue)
        return QJsonValue::Null;
    const auto &glue = *definition.glue;
    return QJsonObject{{"member", sid(glue.member)},
                       {"face", sid(glue.face)},
                       {"anchor", point(glue.anchor)},
                       {"tangent", point(glue.tangent)},
                       {"cutsOpening", glue.cutsOpening}};
}
QJsonValue attachmentDescription(const Document &doc, Id root) {
    const auto &records = doc.hostedComponents();
    if (!records.attachments.contains(root))
        return QJsonValue::Null;
    const auto &attachment = *records.attachments.at(root);
    const auto &openings = records.hosts.at(attachment.host)->openings;
    QJsonArray frame;
    for (auto value : attachment.frame.m)
        frame.append(value);
    QJsonValue opening = QJsonValue::Null;
    if (openings.contains(root)) {
        const auto &cut = openings.at(root);
        QJsonArray reveals;
        for (const auto &[edge, face] : cut.jambs)
            reveals.append(sid(face));
        opening = QJsonObject{
            {"entry", sid(cut.profile.face)}, {"exit", sid(cut.exit)}, {"reveals", reveals}};
    }
    return QJsonObject{{"host", sid(attachment.host)},
                       {"face", sid(attachment.face)},
                       {"frame", frame},
                       {"inset", attachment.inset},
                       {"cutsOpening", openings.contains(root)},
                       {"opening", opening}};
}
bool isHostedCommand(const QString &name) {
    return name == "component.glue" || name == "component.attach" || name == "component.bind" ||
           name == "component.detach" || name == "component.bake_host";
}
HostedCommandResult executeHostedCommand(Document &doc, const QJsonObject &command) {
    const auto name = command["command"].toString();
    if (!isHostedCommand(name))
        throw std::runtime_error("Unknown hosted component command");
    const auto before = doc.hostedRecords();
    HostedCommandResult result;
    result.operation["command"] = name;
    if (name == "component.glue") {
        const auto definition = identity(command["definition"]);
        const auto value = command["glue"];
        std::optional<ComponentGlue> glue;
        if (!value.isNull()) {
            if (!value.isObject())
                throw std::runtime_error("Glue must be an explicit canonical face record or null");
            const auto record = value.toObject();
            const QStringList fields{"member", "face", "anchor", "tangent", "cutsOpening"};
            if (record.size() != fields.size())
                throw std::runtime_error(
                    "Glue requires exactly member, face, anchor, tangent and cutsOpening");
            for (const auto &field : fields)
                if (!record.contains(field))
                    throw std::runtime_error("Missing canonical glue field");
            if (!record["cutsOpening"].isBool())
                throw std::runtime_error("cutsOpening must be boolean");
            glue = ComponentGlue{identity(record["member"]), identity(record["face"]),
                                 point(record["anchor"]), point(record["tangent"]),
                                 record["cutsOpening"].toBool()};
        }
        result.changes = setComponentGlue(doc, definition, glue).changes;
        result.operation["definition"] = sid(definition);
        result.operation["glue"] = componentGlueDescription(*doc.definitions().at(definition));
    } else if (name == "component.bake_host") {
        const auto host = identity(command["body"]);
        if (!doc.bodies().contains(host) || doc.bodies().at(host)->kind != BodyKind::Geometry)
            throw std::runtime_error("Bake requires an existing ordinary geometry host");
        result.changes = bakeHostedComponents(doc, host);
        result.operation["host"] = sid(host);
    } else {
        const auto root = identity(command["body"]);
        if (!doc.instances().contains(root))
            throw std::runtime_error("Choose an existing component instance");
        result.operation["instance"] = sid(root);
        result.operation["definition"] = sid(doc.instances().at(root)->definition);
        if (name == "component.detach")
            result.changes = detachComponent(doc, root);
        else {
            const auto host = identity(command["host"]), face = identity(command["face"]);
            if (name == "component.bind")
                result.changes =
                    bindComponentAtCurrentPose(doc, root, host, face, number(command["inset"]));
            else {
                FacePlacementOptions placement;
                placement.anchor = point(command["anchor"]);
                if (command.contains("tangent"))
                    placement.tangent = point(command["tangent"]);
                if (command.contains("angle"))
                    placement.rotationRadians = number(command["angle"]);
                if (command.contains("scale"))
                    placement.scale = point(command["scale"]);
                result.changes =
                    attachComponent(doc, root, host, face, placement,
                                    command.contains("inset") ? number(command["inset"]) : 0);
            }
            result.operation["host"] = sid(host);
        }
        result.operation["attachment"] = attachmentDescription(doc, root);
    }
    const auto &after = doc.hostedComponents();
    result.operation["affectedHosts"] = changed(before->hosts, after.hosts);
    result.operation["affectedAttachments"] = changed(before->attachments, after.attachments);
    return result;
}
} // namespace sketchy
