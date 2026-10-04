#include "automation/commands.hpp"
#include "geometry/drawing.hpp"
#include "geometry/inference.hpp"
#include "io/document_io.hpp"
#include <QString>
#include <algorithm>
#include <numbers>
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
    QJsonArray vertices, edges, faces, curves;
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
    for (const auto &[id, curve] : body.curves)
        curves.append(encodeCurve(id, curve));
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"context", QString::number(context)},
            {"revision", QString::number(doc.revision())},
            {"vertices", vertices},
            {"edges", edges},
            {"faces", faces},
            {"curves", curves},
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
            {"queries", QJsonArray{"document.describe", "geometry.inspect", "geometry.infer",
                                   "geometry.preview", "commands.describe", "capabilities"}},
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
            {"limitations", QJsonArray{"Push/pull supports prismatic cap edits and bounded face "
                                       "sweeps; general solid booleans are unavailable",
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
    if (name == "geometry.preview") {
        fields(request, {"query", "batch"});
        if (!request["batch"].isObject())
            throw std::runtime_error("Preview requires a batch object");
        return previewBatch(doc, request["batch"].toObject());
    }
    if (name == "commands.describe") {
        fields(request, {"query", "name"});
        return commandDescription(request["name"].toString());
    }
    if (name == "geometry.infer") {
        fields(request, {"query", "clipFromWorld", "worldFromClip", "viewport", "pointer", "radius",
                         "plane", "body"});
        InferenceQuery query;
        auto matrix = [&](const QJsonValue &value, auto &output) {
            const auto values = array(value);
            if (values.size() != 16)
                throw std::runtime_error("Inference camera matrix requires 16 values");
            for (int i = 0; i < 16; ++i)
                output[i] = number(values[i]);
        };
        matrix(request["clipFromWorld"], query.camera.clipFromWorld);
        matrix(request["worldFromClip"], query.camera.worldFromClip);
        const auto viewport = array(request["viewport"]), pointer = array(request["pointer"]);
        if (viewport.size() != 2 || pointer.size() != 2)
            throw std::runtime_error("Viewport and pointer require two values");
        query.camera.width = number(viewport[0]);
        query.camera.height = number(viewport[1]);
        query.x = number(pointer[0]);
        query.y = number(pointer[1]);
        if (request.contains("radius"))
            query.radius = number(request["radius"]);
        if (request.contains("body"))
            query.context = request["body"] == "0" ? 0 : id(request["body"]);
        if (query.context && !doc.bodies().contains(query.context))
            throw std::runtime_error("Inference context does not exist");
        if (request.contains("plane")) {
            if (!request["plane"].isObject())
                throw std::runtime_error("Inference plane must be an object");
            const auto p = request["plane"].toObject();
            fields(p, {"origin", "normal", "xAxis"});
            query.plane =
                DrawingPlane::make(point(p["origin"]), point(p["normal"]), point(p["xAxis"]));
        }
        InferenceIndex index;
        index.sync(doc);
        const auto result = index.query(query);
        QJsonArray candidates;
        for (const auto &candidate : result.candidates)
            candidates.append(QJsonObject{
                {"kind", inferenceLabel(candidate.kind)},
                {"entityType", inferenceEntityLabel(candidate.entityType)},
                {"point", QJsonArray{candidate.point.x, candidate.point.y, candidate.point.z}},
                {"body", QString::number(candidate.body)},
                {"entity", QString::number(candidate.entity)},
                {"otherBody", QString::number(candidate.otherBody)},
                {"otherEntity", QString::number(candidate.otherEntity)},
                {"pixels", candidate.pixels},
                {"depth", candidate.depth}});
        return {{"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"candidates", candidates},
                {"truncated", result.truncated},
                {"visitedPrimitives", qint64(result.visitedPrimitives)}};
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
    struct Lineage {
        std::map<Id, std::vector<Id>> faces, vertices, edges;
    };
    std::map<Id, Lineage> lineages;
    auto compose = [&](const ChangeReport &report) {
        for (const auto &[context, changes] : report) {
            if (!doc.bodies().contains(context))
                continue;
            if (!lineages.contains(context)) {
                auto &initial = lineages[context];
                const auto &body = *doc.bodies().at(context);
                for (const auto &[id, record] : body.surface.faces)
                    initial.faces[id] = {id};
                for (const auto &[id, record] : body.surface.vertices)
                    initial.vertices[id] = {id};
                for (const auto &[id, record] : body.topology.edges)
                    initial.edges[id] = {id};
            }
            auto update = [](auto &mapping, const EntityChanges &step) {
                for (auto &[source, descendants] : mapping) {
                    std::vector<Id> next;
                    for (auto id : descendants) {
                        const auto found = step.descendants.find(id);
                        if (found == step.descendants.end())
                            next.push_back(id);
                        else
                            next.insert(next.end(), found->second.begin(), found->second.end());
                    }
                    std::sort(next.begin(), next.end());
                    next.erase(std::unique(next.begin(), next.end()), next.end());
                    descendants = std::move(next);
                }
            };
            auto &mapping = lineages.at(context);
            update(mapping.faces, changes.faces);
            update(mapping.vertices, changes.vertices);
            update(mapping.edges, changes.edges);
        }
    };
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
            compose(staged.insertEdges(context, point(command["origin"]), point(command["normal"]),
                                       edges));
        } else if (name == "geometry.rectangle" || name == "geometry.polygon" ||
                   name == "geometry.polyline") {
            const auto context = command["body"] == "0" ? Id{0} : id(command["body"]);
            const auto origin = point(command["origin"]), normal = point(command["normal"]);
            std::vector<Vec3> points;
            bool closed = true;
            std::string label;
            if (name == "geometry.polyline") {
                if (!command["closed"].isBool())
                    throw std::runtime_error("Polyline closed flag must be boolean");
                closed = command["closed"].toBool();
                for (const auto &value : array(command["points"]))
                    points.push_back(point(value));
                label = "Polyline";
            } else {
                const auto plane = DrawingPlane::make(origin, normal, point(command["xAxis"]));
                if (name == "geometry.rectangle") {
                    points = rectangleOutline(plane, number(command["width"]),
                                              number(command["height"]));
                    label = "Rectangle";
                } else {
                    const auto sides = number(command["sides"]);
                    if (sides < 3 || sides > 256 || sides != std::floor(sides))
                        throw std::runtime_error("Polygon sides must be an integer from 3 to 256");
                    points = polygonOutline(plane, number(command["radius"]), unsigned(sides));
                    label = "Polygon";
                }
            }
            auto localOrigin = origin, localNormal = normal;
            if (command.contains("space") && command["space"] != "local" &&
                command["space"] != "world")
                throw std::runtime_error("Drawing space must be local or world");
            if (command["space"] == "world" && context) {
                const auto inverse = staged.worldTransform(context).inverse();
                const auto n = normalized(normal);
                const auto frame = DrawingPlane::make(
                    origin, n, std::abs(n.x) < .8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
                localOrigin = inverse.point(origin);
                localNormal = cross(inverse.vector(frame.xAxis), inverse.vector(frame.yAxis));
                const auto magnitude = length(localNormal);
                if (!std::isfinite(magnitude) || magnitude == 0)
                    throw std::runtime_error("Drawing plane is singular in this context");
                localNormal = localNormal * (1 / magnitude);
                for (auto &p : points)
                    p = inverse.point(p);
            }
            compose(staged.insertEdges(context, localOrigin, localNormal,
                                       polylineEdges(points, closed), label));
        } else if (name == "geometry.circle" || name == "geometry.arc_center" ||
                   name == "geometry.arc_two_points" || name == "geometry.arc_three_points" ||
                   name == "geometry.pie") {
            const auto context = command["body"] == "0" ? Id{0} : id(command["body"]);
            const auto count = number(command["segments"]);
            if (count < 1 || count > 256 || count != std::floor(count))
                throw std::runtime_error("Curve segments must be an integer from 1 to 256");
            Curve curve;
            if (name == "geometry.arc_two_points")
                curve = twoPointArc(point(command["start"]), point(command["end"]),
                                    point(command["normal"]), number(command["bulge"]),
                                    unsigned(count));
            else if (name == "geometry.arc_three_points")
                curve = threePointArc(point(command["start"]), point(command["through"]),
                                      point(command["end"]), unsigned(count));
            else {
                const auto kind = name == "geometry.circle" ? CurveKind::Circle
                                  : name == "geometry.pie"  ? CurveKind::Pie
                                                            : CurveKind::Arc;
                curve = centerCurve(kind,
                                    DrawingPlane::make(point(command["center"]),
                                                       point(command["normal"]),
                                                       point(command["xAxis"])),
                                    number(command["radius"]),
                                    kind == CurveKind::Circle ? 0 : number(command["startAngle"]),
                                    kind == CurveKind::Circle ? 2 * std::numbers::pi
                                                              : number(command["sweepAngle"]),
                                    unsigned(count));
            }
            if (command.contains("space") && command["space"] != "local" &&
                command["space"] != "world")
                throw std::runtime_error("Drawing space must be local or world");
            if (command["space"] == "world" && context) {
                const auto inverse = staged.worldTransform(context).inverse();
                curve.center = inverse.point(curve.center);
                curve.xAxis = inverse.vector(curve.xAxis);
                curve.yAxis = inverse.vector(curve.yAxis);
            }
            compose(staged.addCurve(context, std::move(curve)));
        } else if (name == "geometry.wire") {
            const auto context = command["body"] == "0" ? Id{0} : id(command["body"]);
            staged.addWire(context, point(command["start"]), point(command["end"]));
        } else if (name == "geometry.split_edge") {
            compose(staged.splitEdge(id(command["body"]), id(command["edge"]),
                                     number(command["fraction"])));
        } else if (name == "geometry.erase_face") {
            compose(staged.eraseFace(id(command["body"]), id(command["face"])));
        } else if (name == "geometry.erase_edge") {
            compose(staged.eraseEdge(id(command["body"]), id(command["edge"])));
        } else if (name == "geometry.heal_face") {
            compose(staged.healFace(id(command["body"]), id(command["edge"]),
                                    point(command["origin"]), point(command["normal"])));
        } else if (name == "geometry.cleanup") {
            compose(staged.cleanup(id(command["body"])));
        } else if (name == "geometry.push_pull") {
            compose(staged.pushPull(id(command["body"]), id(command["face"]),
                                    number(command["distance"])));
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
            auto mapping = lineages.contains(id) ? lineages.at(id) : Lineage{};
            auto prune = [](auto &map, const auto &records) {
                for (auto &[source, targets] : map)
                    std::erase_if(targets, [&](Id target) { return !records.contains(target); });
            };
            const Body empty;
            const auto &body = after ? *after : empty;
            prune(mapping.faces, body.surface.faces);
            prune(mapping.vertices, body.surface.vertices);
            prune(mapping.edges, body.topology.edges);
            edit.changes.push_back({id, before, after, std::move(mapping.faces),
                                    std::move(mapping.vertices), std::move(mapping.edges)});
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
                        {"faces", entityChanges(change.faces)},
                        {"curves", entityChanges(change.curves)}};
    return {{"status", "committed"},
            {"revision", QString::number(doc.revision())},
            {"created", created},
            {"changes", changes},
            {"document", describe(doc)}};
}
QJsonObject executeAmend(Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request) {
    QJsonObject result;
    const auto report = doc.amendLast(
        stamp, [&](Document &candidate) { result = executeBatch(candidate, request); });
    QJsonObject changes;
    for (const auto &[context, change] : report)
        changes[QString::number(context)] =
            QJsonObject{{"vertices", entityChanges(change.vertices)},
                        {"edges", entityChanges(change.edges)},
                        {"faces", entityChanges(change.faces)},
                        {"curves", entityChanges(change.curves)}};
    result["changes"] = changes;
    result["amended"] = true;
    return result;
}
QJsonObject previewAmend(const Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request) {
    Document candidate = doc;
    auto result = executeAmend(candidate, stamp, request);
    result["status"] = "preview";
    result["baseRevision"] = QString::number(doc.revision());
    QJsonObject geometry;
    for (const auto &[id, body] : candidate.bodies())
        if (!doc.bodies().contains(id) || doc.bodies().at(id) != body)
            geometry[QString::number(id)] = topologyDescription(candidate, id);
    result["geometry"] = geometry;
    return result;
}
QJsonObject previewBatch(const Document &doc, const QJsonObject &request) {
    Document candidate = doc;
    auto result = executeBatch(candidate, request);
    result["status"] = "preview";
    result["baseRevision"] = QString::number(doc.revision());
    QJsonObject geometry;
    for (const auto &[id, body] : candidate.bodies())
        if (!doc.bodies().contains(id) || doc.bodies().at(id) != body)
            geometry[QString::number(id)] = topologyDescription(candidate, id);
    result["geometry"] = geometry;
    return result;
}
} // namespace sketchy
