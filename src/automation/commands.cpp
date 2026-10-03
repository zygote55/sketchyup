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
QJsonArray ids(const std::vector<Id> &values) {
    QJsonArray result;
    for (auto value : values)
        result.append(QString::number(value));
    return result;
}
QJsonObject entityChanges(const EntityChanges &changes) {
    QJsonObject mappings;
    for (const auto &[id, targets] : changes.descendants)
        mappings[QString::number(id)] = ids(targets);
    return {{"created", ids(changes.created)},
            {"deleted", ids(changes.deleted)},
            {"modified", ids(changes.modified)},
            {"descendants", mappings}};
}
QJsonObject topologyDescription(const Document &doc, Id context) {
    const auto &body = *doc.bodies().at(context);
    const auto adjacency = body.topology.adjacency(body.surface);
    QJsonArray vertices, edges, faces;
    for (const auto &[id, point] : body.surface.vertices)
        vertices.append(QJsonObject{{"id", QString::number(id)},
                                    {"point", QJsonArray{point.x, point.y, point.z}},
                                    {"edges", ids(adjacency.vertexEdges.at(id))}});
    for (const auto &[id, edge] : body.topology.edges) {
        QJsonArray incidence;
        for (const auto &item : adjacency.edgeFaces.at(id))
            incidence.append(QJsonObject{{"face", QString::number(item.face)},
                                         {"loop", qint64(item.loop)},
                                         {"position", qint64(item.position)},
                                         {"reversed", item.reversed}});
        edges.append(
            QJsonObject{{"id", QString::number(id)},
                        {"vertices", QJsonArray{QString::number(edge.a), QString::number(edge.b)}},
                        {"wire", edge.wire},
                        {"incidence", incidence}});
    }
    for (const auto &[id, face] : body.surface.faces) {
        QJsonArray loops;
        for (const auto &loop : adjacency.faceLoops.at(id)) {
            QJsonArray oriented;
            for (const auto &edge : loop)
                oriented.append(
                    QJsonObject{{"edge", QString::number(edge.edge)}, {"reversed", edge.reversed}});
            loops.append(oriented);
        }
        faces.append(QJsonObject{{"id", QString::number(id)}, {"loops", loops}});
    }
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"context", QString::number(context)},
            {"revision", QString::number(doc.revision())},
            {"vertices", vertices},
            {"edges", edges},
            {"faces", faces},
            {"nextId", QString::number(body.surface.nextId)},
            {"nextEdgeId", QString::number(body.topology.nextId)}};
}
} // namespace
QJsonObject capabilities() {
    return {{"apiVersion", 1},
            {"status", "experimental"},
            {"units", "m"},
            {"up", "Z"},
            {"commands",
             [] {
                 QJsonArray names;
                 for (const auto &item : commandCatalog())
                     names.append(item.toObject()["name"]);
                 return names;
             }()},
            {"commandSchemas", commandCatalog()},
            {"queries", QJsonArray{"document.describe", "geometry.inspect", "commands.describe",
                                   "capabilities"}},
            {"transactionContract",
             QJsonObject{{"atomic", true},
                         {"history", "one undo item per batch"},
                         {"precondition", "document identity and expected content revision"},
                         {"idempotency", "reserved; unavailable until durable outcome ledger"}}},
            {"limits", QJsonObject{{"fileBytes", 16 + 33 * 1024 * 1024},
                                   {"documentBytes", 32 * 1024 * 1024},
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
QJsonObject executeQuery(const Document &doc, const QJsonObject &request) {
    const auto name = request["query"].toString();
    if (name == "commands.describe") {
        fields(request, {"query", "name"});
        return commandDescription(request["name"].toString());
    }
    if (name == "geometry.inspect") {
        fields(request, {"query", "body"});
        return topologyDescription(doc, id(request["body"]));
    }
    fields(request, {"query"});
    if (name == "document.describe")
        return describe(doc);
    if (name == "capabilities")
        return capabilities();
    throw std::runtime_error("Unavailable query");
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
    std::map<Id, std::map<Id, std::vector<Id>>> faceLineage;
    for (const auto &[context, body] : doc.bodies())
        for (const auto &[face, record] : body->surface.faces)
            faceLineage[context][face] = {face};
    for (const auto &value : commands) {
        if (!value.isObject())
            throw std::runtime_error("Expected command object");
        auto command = value.toObject();
        auto name = command["command"].toString();
        const auto schema = commandDescription(name)["parameters"].toObject();
        const auto allowed = schema["properties"].toObject();
        for (auto it = command.begin(); it != command.end(); ++it)
            if (!allowed.contains(it.key()))
                throw std::runtime_error("Unknown command parameter");
        for (const auto &required : schema["required"].toArray())
            if (!command.contains(required.toString()))
                throw std::runtime_error("Missing command parameter");
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
        } else if (name == "geometry.insert_edges") {
            std::vector<std::array<Vec3, 2>> edges;
            const auto edgeValues = array(command["edges"]);
            if (edgeValues.empty() || edgeValues.size() > 1024)
                throw PlanarError("ARRANGEMENT_LIMIT", "Insert between 1 and 1024 finite edges");
            for (const auto &value : edgeValues) {
                const auto edge = array(value);
                if (edge.size() != 2)
                    throw PlanarError("INVALID_EDGE",
                                      "Each inserted edge needs exactly two endpoints");
                edges.push_back({point(edge[0]), point(edge[1])});
            }
            const auto context = command["body"] == "0" ? Id{0} : id(command["body"]);
            const auto report = staged.insertEdges(context, point(command["origin"]),
                                                   point(command["normal"]), edges);
            for (const auto &[body, changes] : report)
                for (auto &[source, descendants] : faceLineage[body]) {
                    std::vector<Id> next;
                    for (auto face : descendants) {
                        auto found = changes.faces.descendants.find(face);
                        if (found == changes.faces.descendants.end())
                            next.push_back(face);
                        else
                            next.insert(next.end(), found->second.begin(), found->second.end());
                    }
                    std::sort(next.begin(), next.end());
                    next.erase(std::unique(next.begin(), next.end()), next.end());
                    descendants = std::move(next);
                }
        } else if (name == "geometry.wire") {
            const auto context = command["body"] == "0" ? Id{0} : id(command["body"]);
            staged.addWire(context, point(command["start"]), point(command["end"]));
        } else if (name == "geometry.split_edge") {
            staged.splitEdge(id(command["body"]), id(command["edge"]), number(command["fraction"]));
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
            if (c.x < 0 || c.x > 1 || c.y < 0 || c.y > 1 || c.z < 0 || c.z > 1)
                throw std::runtime_error("Color components must be between zero and one");
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
        if (before != after) {
            auto lineage = faceLineage[id];
            for (auto &[source, targets] : lineage)
                std::erase_if(targets, [&](Id target) {
                    return !after || !after->surface.faces.contains(target);
                });
            edit.changes.push_back({id, before, after, std::move(lineage)});
        }
    }
    if (edit.changes.empty())
        throw std::runtime_error("Batch has no committed changes");
    edit.nextIdFloor = staged.nextId();
    created = QJsonArray();
    for (const auto &change : edit.changes)
        if (!change.before && change.after)
            created.append(QString::number(change.id));
    const auto report = doc.apply(std::move(edit), doc.revision());
    QJsonObject changes;
    for (const auto &[context, change] : report)
        changes[QString::number(context)] =
            QJsonObject{{"vertices", entityChanges(change.vertices)},
                        {"edges", entityChanges(change.edges)},
                        {"faces", entityChanges(change.faces)}};
    return {{"status", "committed"},
            {"revision", QString::number(doc.revision())},
            {"created", created},
            {"changes", changes},
            {"document", describe(doc)}};
}
} // namespace sketchy
