#include "automation/commands.hpp"
#include <QString>
#include <algorithm>
#include <set>
namespace sketchy {
namespace {
QJsonArray array(const QJsonValue &value) {
    if (!value.isArray())
        throw std::runtime_error("Expected array");
    return value.toArray();
}
double number(const QJsonValue &value) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
        throw std::runtime_error("Expected finite number");
    return value.toDouble();
}
Id id(const QJsonValue &value) {
    bool ok = false;
    auto n = value.toString().toULongLong(&ok);
    if (!value.isString() || !ok || !n || QString::number(n) != value.toString())
        throw std::runtime_error("Expected canonical string ID");
    return n;
}
Vec3 point(const QJsonValue &value) {
    auto p = array(value);
    if (p.size() != 3)
        throw std::runtime_error("Expected three coordinates");
    return {number(p[0]), number(p[1]), number(p[2])};
}
void fields(const QJsonObject &object, std::initializer_list<QString> allowed) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            throw std::runtime_error(("Unknown field: " + it.key()).toStdString());
}
} // namespace
QJsonObject capabilities() {
    return {
        {"apiVersion", 1},
        {"status", "experimental"},
        {"units", "m"},
        {"up", "Z"},
        {"commands", QJsonArray{"geometry.face", "geometry.extrude_isolated", "geometry.translate",
                                "geometry.delete", "material.color", "scene.transform"}},
        {"limits", QJsonObject{{"fileBytes", 32 * 1024 * 1024},
                               {"bodies", 10000},
                               {"vertices", 100000},
                               {"batchCommands", 100}}},
        {"limitations", QJsonArray{"No adjacent-face push/pull or automatic face merging",
                                   "No durable transaction outcomes or remote retry protocol",
                                   "No AI provider or Blender integration"}}};
}
QJsonObject describe(const Document &doc) {
    QJsonArray bodies;
    for (const auto &[id, b] : doc.bodies()) {
        QJsonArray faces;
        for (const auto &[fid, f] : b->surface.faces)
            faces.append(QJsonObject{{"id", QString::number(fid)},
                                     {"area", doc.worldArea(id, fid)},
                                     {"loops", int(f.loops.size())}});
        QJsonArray world;
        for (auto value : doc.worldTransform(id).m)
            world.append(value);
        bodies.append(QJsonObject{{"id", QString::number(id)},
                                  {"parent", QString::number(b->parent)},
                                  {"worldTransform", world},
                                  {"name", QString::fromStdString(b->name)},
                                  {"vertices", int(b->surface.vertices.size())},
                                  {"faces", faces}});
    }
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"bodies", bodies}};
}
QJsonObject executeBatch(Document &doc, const QJsonObject &request) {
    fields(request, {"apiVersion", "documentId", "expectedRevision", "commands"});
    if (!request["apiVersion"].isDouble() || request["apiVersion"].toDouble() != 1)
        throw std::runtime_error("Unsupported API version");
    if (!request["documentId"].isString() ||
        request["documentId"].toString().toStdString() != doc.identity())
        throw std::runtime_error("Wrong document identity");
    if (request["expectedRevision"] != QString::number(doc.revision()))
        throw std::runtime_error("STALE_REVISION");
    auto commands = array(request["commands"]);
    if (commands.empty() || commands.size() > 100)
        throw std::runtime_error("Batch must contain 1–100 commands");
    Document staged = doc;
    QJsonArray created;
    for (const auto &value : commands) {
        if (!value.isObject())
            throw std::runtime_error("Expected command object");
        auto command = value.toObject();
        auto name = command["command"].toString();
        if (name == "geometry.face") {
            fields(command, {"command", "loops", "name"});
            if (command.contains("name") && !command["name"].isString())
                throw std::runtime_error("Expected face name");
            std::vector<std::vector<Vec3>> loops;
            for (const auto &loop : array(command["loops"])) {
                loops.emplace_back();
                for (const auto &p : array(loop))
                    loops.back().push_back(point(p));
            }
            auto createdId = staged.addFace(loops, command["name"].toString("Face").toStdString());
            created.append(QString::number(createdId));
        } else if (name == "geometry.extrude_isolated") {
            fields(command, {"command", "body", "face", "distance"});
            staged.extrude(id(command["body"]), id(command["face"]), number(command["distance"]));
        } else if (name == "geometry.translate") {
            fields(command, {"command", "body", "delta"});
            staged.move(id(command["body"]), point(command["delta"]));
        } else if (name == "scene.transform") {
            fields(command, {"command", "body", "matrix", "parent"});
            auto matrix = array(command["matrix"]);
            if (matrix.size() != 16)
                throw std::runtime_error("Expected 16 matrix coefficients");
            Transform transform;
            for (int i = 0; i < 16; ++i)
                transform.m[i] = number(matrix[i]);
            const auto target = id(command["body"]);
            Id parent = staged.bodies().at(target)->parent;
            if (command.contains("parent"))
                parent = command["parent"] == "0" ? 0 : id(command["parent"]);
            staged.transform(target, transform, parent);
        } else if (name == "geometry.delete") {
            fields(command, {"command", "body"});
            staged.erase(id(command["body"]));
        } else if (name == "material.color") {
            fields(command, {"command", "body", "color"});
            auto c = point(command["color"]);
            staged.paint(id(command["body"]), {float(c.x), float(c.y), float(c.z)});
        } else
            throw std::runtime_error("Unavailable command");
    }
    Edit edit{"Command batch", {}};
    std::set<Id> all;
    for (const auto &[id, b] : doc.bodies())
        all.insert(id);
    for (const auto &[id, b] : staged.bodies())
        all.insert(id);
    for (Id id : all) {
        auto before = doc.bodies().contains(id) ? doc.bodies().at(id) : nullptr;
        auto after = staged.bodies().contains(id) ? staged.bodies().at(id) : nullptr;
        if (before != after)
            edit.changes.push_back({id, before, after});
    }
    if (edit.changes.empty())
        throw std::runtime_error("Batch has no committed changes");
    edit.nextIdFloor = staged.nextId();
    created = QJsonArray();
    for (const auto &change : edit.changes)
        if (!change.before && change.after)
            created.append(QString::number(change.id));
    doc.apply(std::move(edit), doc.revision());
    return {{"status", "committed"},
            {"revision", QString::number(doc.revision())},
            {"created", created},
            {"document", describe(doc)}};
}
} // namespace sketchy
