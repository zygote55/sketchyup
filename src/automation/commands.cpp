#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/entity_info.hpp"
#include "automation/inspection.hpp"
#include "automation/inspection_session.hpp"
#include "automation/model_recipes.hpp"
#include "automation/transactions.hpp"
#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/copy_array.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "core/intersection_edit.hpp"
#include "core/materials.hpp"
#include "core/profile_sweep.hpp"
#include "core/selection.hpp"
#include "core/solid_boolean.hpp"
#include "core/tags.hpp"
#include "core/transform_selection.hpp"
#include "geometry/constraints.hpp"
#include "geometry/drawing.hpp"
#include "geometry/inference.hpp"
#include "geometry/intersection.hpp"
#include "io/assets.hpp"
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
ChangeReport decodedChanges(const QJsonObject &objects) {
    ChangeReport report;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        const auto object = it.value().toObject();
        auto &change = report[id(it.key())];
        auto decode = [&](const char *key, EntityChanges &entities) {
            const auto record = object[key].toObject();
            for (auto [name, values] :
                 {std::pair{"created", &entities.created}, std::pair{"deleted", &entities.deleted},
                  std::pair{"modified", &entities.modified}})
                for (auto value : record[name].toArray())
                    values->push_back(id(value));
            const auto mappings = record["descendants"].toObject();
            for (auto mapping = mappings.begin(); mapping != mappings.end(); ++mapping) {
                auto &values = entities.descendants[id(mapping.key())];
                for (auto value : mapping.value().toArray())
                    values.push_back(id(value));
            }
        };
        decode("faces", change.faces);
        decode("edges", change.edges);
        decode("vertices", change.vertices);
        decode("curves", change.curves);
        decode("guides", change.guides);
    }
    return report;
}
QJsonObject topologyDescription(const Document &doc, Id context) {
    const auto &body = *doc.bodies().at(context);
    const auto adjacency = body.topology.adjacency(body.surface);
    QJsonArray vertices, edges, faces, curves, guides;
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
    for (const auto &[id, guide] : body.guides)
        guides.append(encodeGuide(id, guide));
    QJsonArray world;
    for (auto value : doc.worldTransform(context).m)
        world.append(value);
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"worldTransform", world},
            {"context", QString::number(context)},
            {"revision", QString::number(doc.revision())},
            {"vertices", vertices},
            {"edges", edges},
            {"faces", faces},
            {"curves", curves},
            {"guides", guides},
            {"nextId", QString::number(body.surface.nextId)},
            {"nextEdgeId", QString::number(body.topology.nextId)}};
}
QJsonArray tagDescription(const Document &doc) {
    QJsonArray tags;
    for (const auto &[id, tag] : doc.tags())
        tags.append(QJsonObject{{"id", QString::number(id)},
                                {"parent", QString::number(tag->parent)},
                                {"name", QString::fromStdString(tag->name)},
                                {"folder", tag->folder},
                                {"visible", tag->visible},
                                {"effectiveVisible", tagVisible(doc.tags(), id)}});
    return tags;
}
} // namespace
QJsonObject capabilities() {
    return {
        {"apiVersion", 1},
        {"status", "experimental"},
        {"inspection", inspectionCapabilities()},
        {"inspectionSession", inspectionSessionCapabilities()},
        {"transactions", transactionCapabilities()},
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
        {"imports", QJsonArray{QJsonObject{{"format", "formline"},
                                           {"version", 1},
                                           {"sourceUnits", "m"},
                                           {"sourceUp", "Y"},
                                           {"primitives", QJsonArray{"box", "cylinder"}},
                                           {"objects", 1000},
                                           {"fileBytes", 32 * 1024 * 1024}}}},
        {"historyControl",
         QJsonObject{{"operation", "history.navigate"},
                     {"preconditions", QJsonArray{"apiVersion", "documentId", "expectedRevision"}},
                     {"position", "canonical decimal cursor from history.describe"},
                     {"maximumEntries", int(Document::historyEntryLimit)},
                     {"maximumPage", 1000},
                     {"batchMetadata", QJsonArray{"label", "taskId", "request", "assistant"}}}},
        {"queries",
         QJsonArray{"history.describe", "document.describe", "entity.inspect", "tags.describe",
                    "materials.describe", "material.sample", "assets.describe", "component.inspect",
                    "geometry.inspect", "geometry.infer", "geometry.measure_distance",
                    "geometry.measure_angle", "geometry.preview", "commands.describe",
                    "capabilities"}},
        {"transactionContract",
         QJsonObject{{"atomic", true},
                     {"history", "one undo item per batch"},
                     {"precondition", "document identity and expected content revision"},
                     {"idempotency", "Durable retries require the transaction dispatcher; legacy "
                                     "batch execution is local only"}}},
        {"limits", QJsonObject{{"fileBytes", 128 * 1024 * 1024},
                               {"nativeContainerBytes", 16 + 97 * 1024 * 1024},
                               {"assetBytes", int(AssetPayload::limit)},
                               {"totalAssetBytes", int(assetTotalLimit)},
                               {"assets", 1024},
                               {"documentBytes", 128 * 1024 * 1024},
                               {"packagedModelBytes", 32 * 1024 * 1024},
                               {"bodies", 10000},
                               {"componentDefinitions", 1024},
                               {"materials", 1024},
                               {"vertices", 100000},
                               {"guides", 10000},
                               {"guidesPerContext", 1024},
                               {"batchCommands", 100},
                               {"tagsAndFolders", 1024},
                               {"tagFolderDepth", 32}}},
        {"limitations",
         QJsonArray{
             "Push/pull supports prismatic cap edits and bounded face "
             "sweeps; solid booleans require validated material solids, with disconnected "
             "results returned separately",
             "Local JSON-lines sessions are available; remote MCP transport is not yet implemented",
             "Component geometry is materialized per instance; instanced "
             "rendering and component libraries are not yet implemented",
             "External provider and render work is outside document command batches"}}};
}
QJsonObject describe(const Document &doc) {
    QJsonArray bodies, definitions, instances;
    const auto tags = tagDescription(doc);
    Selection presentation;
    std::map<Id, size_t> uses;
    for (const auto &[root, instance] : doc.instances()) {
        ++uses[instance->definition];
        QJsonObject members;
        for (auto [member, target] : instance->members)
            members[QString::number(member)] = QString::number(target);
        instances.append(QJsonObject{{"root", QString::number(root)},
                                     {"definition", QString::number(instance->definition)},
                                     {"members", members}});
    }
    for (const auto &[id, definition] : doc.definitions())
        definitions.append(QJsonObject{{"id", QString::number(id)},
                                       {"name", QString::fromStdString(definition->name)},
                                       {"root", QString::number(definition->root)},
                                       {"members", qint64(definition->members.size())},
                                       {"instances", qint64(uses[id])}});
    for (const auto &[id, b] : doc.bodies()) {
        QJsonArray faces;
        for (const auto &[fid, f] : b->surface.faces)
            faces.append(QJsonObject{{"id", QString::number(fid)},
                                     {"area", doc.worldArea(id, fid)},
                                     {"loops", int(f.loops.size())}});
        QJsonArray world;
        for (auto value : doc.worldTransform(id).m)
            world.append(value);
        bodies.append(
            QJsonObject{{"id", QString::number(id)},
                        {"parent", QString::number(b->parent)},
                        {"kind", b->kind == BodyKind::Group ? "group" : "geometry"},
                        {"hidden", b->hidden},
                        {"effectiveHidden", presentation.hidden(doc, {id, SelectionKind::Body, 0})},
                        {"locked", b->locked},
                        {"tag", QString::number(b->tag)},
                        {"worldTransform", world},
                        {"name", QString::fromStdString(b->name)},
                        {"vertices", int(b->surface.vertices.size())},
                        {"guides", int(b->guides.size())},
                        {"faces", faces}});
    }
    return {{"documentId", QString::fromStdString(doc.identity())},
            {"revision", QString::number(doc.revision())},
            {"units", "m"},
            {"displayUnits", QString::fromLatin1(unitCode(doc.displayUnits()).data())},
            {"bodies", bodies},
            {"definitions", definitions},
            {"instances", instances},
            {"tags", tags},
            {"nextTagId", QString::number(doc.nextTagId())}};
}
QJsonObject executeQuery(const Document &doc, const QJsonObject &request) {
    const auto name = request["query"].toString();
    if (name == "history.describe") {
        fields(request, {"query", "offset", "limit"});
        const auto offset =
            request.contains("offset") ? (request["offset"] == "0" ? 0 : id(request["offset"])) : 0;
        const auto limit = request.contains("limit") ? id(request["limit"]) : 200;
        return describeHistory(doc, offset, limit);
    }
    if (name == "entity.inspect") {
        fields(request, {"query", "body", "kind", "entity"});
        const auto kind =
            request.contains("kind") ? request["kind"].toString() : QString("context");
        if (kind != "context" && kind != "face" && kind != "edge" && kind != "guide")
            throw std::runtime_error("Unknown entity measurement kind");
        const auto entity =
            request.contains("entity") && request["entity"] != "0" ? id(request["entity"]) : Id{};
        if ((kind == "context") != (entity == 0))
            throw std::runtime_error("Subentity measurements require a nonzero identity");
        return entityDescription(doc, {id(request["body"]),
                                       kind == "context" ? SelectionKind::Body
                                       : kind == "face"  ? SelectionKind::Face
                                       : kind == "edge"  ? SelectionKind::Edge
                                                         : SelectionKind::Guide,
                                       entity});
    }
    if (name == "assets.describe") {
        fields(request, {"query"});
        return {{"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"assets", assetManifest(doc)},
                {"nextAssetId", QString::number(doc.nextAssetId())}};
    }
    if (name == "materials.describe") {
        fields(request, {"query"});
        QJsonArray records;
        for (const auto &[id, material] : doc.materials())
            records.append(QJsonObject{
                {"id", QString::number(id)},
                {"name", QString::fromStdString(material->name)},
                {"color", QJsonArray{material->color[0], material->color[1], material->color[2]}},
                {"opacity", material->opacity},
                {"asset", QString::number(material->asset)},
                {"assetStatus", !material->asset                            ? "none"
                                : doc.assets().at(material->asset)->payload ? "present"
                                                                            : "missing"}});
        return {{"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"materials", records},
                {"nextMaterialId", QString::number(doc.nextMaterialId())}};
    }
    if (name == "material.sample") {
        fields(request, {"query", "body", "face"});
        const auto body = doc.bodies().at(id(request["body"]));
        const auto face = request.contains("face") ? id(request["face"]) : Id{};
        if (face && !body->surface.faces.contains(face))
            throw std::runtime_error("Unknown face");
        auto appearance = [&](bool back) {
            const auto result = surfaceAppearance(doc.materials(), *body, face, back);
            const auto asset = result.material ? doc.materials().at(result.material)->asset : Id{};
            return QJsonObject{
                {"material", QString::number(result.material)},
                {"color", QJsonArray{result.color[0], result.color[1], result.color[2]}},
                {"opacity", result.opacity},
                {"asset", QString::number(asset)},
                {"assetStatus", !asset                            ? "none"
                                : doc.assets().at(asset)->payload ? "present"
                                                                  : "missing"}};
        };
        return {{"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"front", appearance(false)},
                {"back", appearance(true)}};
    }
    if (name == "tags.describe") {
        fields(request, {"query"});
        return {{"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"tags", tagDescription(doc)},
                {"nextTagId", QString::number(doc.nextTagId())}};
    }
    if (name == "component.inspect") {
        fields(request, {"query", "definition"});
        const auto definition = doc.definitions().at(id(request["definition"]));
        QJsonObject references;
        for (auto [member, target] : definition->references)
            references[QString::number(member)] = QString::number(target);
        QJsonArray instances;
        for (const auto &[root, instance] : doc.instances())
            if (instance->definition == definition->id)
                instances.append(QString::number(root));
        return {{"scope", "definition"},
                {"definition", QString::number(definition->id)},
                {"name", QString::fromStdString(definition->name)},
                {"root", QString::number(definition->root)},
                {"nextMemberId", QString::number(definition->nextMemberId)},
                {"members", encodeBodies(definition->members)},
                {"references", references},
                {"instances", instances},
                {"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())}};
    }
    if (name == "geometry.measure_distance") {
        fields(request, {"query", "start", "end"});
        return {{"distance", measureDistance(point(request["start"]), point(request["end"]))},
                {"units", "m"}};
    }
    if (name == "geometry.measure_angle") {
        fields(request, {"query", "origin", "first", "second", "normal"});
        return {{"angle", measureAngle(point(request["origin"]), point(request["first"]),
                                       point(request["second"]), point(request["normal"]))},
                {"units", "rad"}};
    }
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
                         "plane", "body", "anchor", "reference", "fromPoint", "includeGuides"});
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
        if (request.contains("includeGuides")) {
            if (!request["includeGuides"].isBool())
                throw std::runtime_error("includeGuides must be a boolean");
            query.includeGuides = request["includeGuides"].toBool();
        }
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
                {"otherEntityType", inferenceEntityLabel(candidate.otherEntityType)},
                {"pixels", candidate.pixels},
                {"depth", candidate.depth}});
        QJsonArray directions;
        if (!request.contains("anchor") &&
            (request.contains("reference") || request.contains("fromPoint")))
            throw std::runtime_error("Directional references require an anchor");
        if (request.contains("anchor")) {
            const auto anchor = point(request["anchor"]);
            const auto plane = query.plane.value_or(DrawingPlane{});
            std::vector<DirectionConstraint> references;
            if (request.contains("reference")) {
                if (!request["reference"].isObject())
                    throw std::runtime_error("Inference reference requires body and edge");
                const auto ref = request["reference"].toObject();
                fields(ref, {"body", "edge", "guide"});
                if (ref.contains("edge") == ref.contains("guide"))
                    throw std::runtime_error("Reference requires exactly one edge or guide");
                InferenceCandidate source;
                source.body = id(ref["body"]);
                source.entity = id(ref[ref.contains("guide") ? "guide" : "edge"]);
                source.entityType =
                    ref.contains("guide") ? InferenceEntity::Guide : InferenceEntity::Edge;
                if (!doc.bodies().contains(source.body) ||
                    (source.entityType == InferenceEntity::Guide
                         ? !query.includeGuides ||
                               !doc.bodies().at(source.body)->guides.contains(source.entity)
                         : !doc.bodies().at(source.body)->topology.edges.contains(source.entity)))
                    throw std::runtime_error("Inference reference does not exist or is hidden");
                references = edgeDirections(doc, source, anchor, plane);
            }
            const auto from = request.contains("fromPoint")
                                  ? std::optional<Vec3>{point(request["fromPoint"])}
                                  : std::nullopt;
            for (const auto &candidate :
                 directionCandidates(query.camera, query.x, query.y, anchor, plane, references,
                                     from, query.radius)) {
                const auto &c = candidate.constraint;
                directions.append(QJsonObject{
                    {"kind", directionLabel(c.kind)},
                    {"origin", QJsonArray{c.origin.x, c.origin.y, c.origin.z}},
                    {"direction", QJsonArray{c.direction.x, c.direction.y, c.direction.z}},
                    {"point", QJsonArray{candidate.point.x, candidate.point.y, candidate.point.z}},
                    {"body", QString::number(c.body)},
                    {"entity", QString::number(c.entity)},
                    {"entityType",
                     c.entityType ? QJsonValue(inferenceEntityLabel(*c.entityType)) : QJsonValue()},
                    {"pixels", candidate.pixels}});
            }
        }
        return {{"documentId", QString::fromStdString(doc.identity())},
                {"revision", QString::number(doc.revision())},
                {"candidates", candidates},
                {"directions", directions},
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
static QJsonObject executeBatchWithReferences(Document &doc, const QJsonObject &request,
                                              BatchResponse response,
                                              const Document *outerScene = nullptr,
                                              Id excludedInstance = 0) {
    fields(request, {"apiVersion", "documentId", "expectedRevision", "commands", "history"});
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
    HistoryMetadata historyMetadata;
    std::string historyLabel =
        commands.size() == 1
            ? commandDescription(commands[0].toObject()["command"].toString())["label"]
                  .toString()
                  .toStdString()
            : "Command batch (" + std::to_string(commands.size()) + " operations)";
    if (request.contains("history")) {
        if (!request["history"].isObject())
            throw std::runtime_error("History metadata must be an object");
        const auto metadata = request["history"].toObject();
        fields(metadata, {"label", "taskId", "request", "assistant"});
        auto text = [&](const char *key, size_t limit) {
            const auto value = metadata.value(key);
            const auto string = value.toString();
            const auto bytes = string.toUtf8();
            if (!value.isString() || size_t(bytes.size()) > limit || string.contains(QChar('\0')) ||
                QString::fromUtf8(bytes) != string)
                throw std::runtime_error("Invalid history text");
            return bytes.toStdString();
        };
        if (metadata.contains("label"))
            historyLabel = text("label", 512);
        if (metadata.contains("taskId"))
            historyMetadata.taskId = text("taskId", 128);
        if (metadata.contains("request"))
            historyMetadata.request = text("request", 4096);
        if (metadata.contains("assistant")) {
            if (!metadata["assistant"].isBool())
                throw std::runtime_error("History assistant flag must be boolean");
            historyMetadata.assistant = metadata["assistant"].toBool();
        }
        if (historyLabel.empty() ||
            (historyMetadata.assistant &&
             (historyMetadata.taskId.empty() || historyMetadata.request.empty())))
            throw std::runtime_error("History task metadata is incomplete");
    }
    Document staged = doc.readSnapshot();
    QJsonArray created, copies, transfers, componentOperations, recipeOperations, sweeps, booleans,
        solidOperations;
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
    auto mergeContext = [&](Id context, std::optional<std::set<Id>> members = {}) {
        for (const auto &partition : consolidationGroups(staged, context, members)) {
            const auto result = consolidateContext(staged, context, partition);
            compose(result.changes);
            for (const auto &[source, mapping] : result.transfers) {
                auto encode = [](const auto &map) {
                    QJsonObject object;
                    for (auto [from, to] : map)
                        object[QString::number(from)] = QString::number(to);
                    return object;
                };
                transfers.append(QJsonObject{{"sourceBody", QString::number(source)},
                                             {"body", QString::number(result.destination)},
                                             {"vertices", encode(mapping.vertices)},
                                             {"edges", encode(mapping.edges)},
                                             {"faces", encode(mapping.faces)},
                                             {"curves", encode(mapping.curves)},
                                             {"guides", encode(mapping.guides)}});
            }
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
        if (name.startsWith("assembly.")) {
            const auto recipe = executeModelRecipe(staged, command);
            for (const auto &step : recipe.steps)
                compose(decodedChanges(step.toObject()["changes"].toObject()));
            recipeOperations.append(recipe.report);
        } else if (name == "document.units") {
            if (!command["units"].isString())
                throw std::runtime_error("Document units must be a string");
            staged.setDisplayUnits(parseDisplayUnit(command["units"].toString().toStdString()));
        } else if (name.startsWith("asset.")) {
            if ((command.contains("name") && !command["name"].isString()) ||
                (command.contains("mediaType") && !command["mediaType"].isString()))
                throw std::runtime_error("Asset name and media type must be strings");
            auto data = [&] {
                if (!command["data"].isString())
                    throw std::runtime_error("Asset data must be base64");
                return decodeAssetPayload(command["data"].toString());
            };
            if (name == "asset.import" || name == "asset.missing")
                createAsset(staged, command["name"].toString().toStdString(),
                            command["mediaType"].toString().toStdString(),
                            name == "asset.import" ? data() : AssetPayloadPtr{});
            else if (name == "asset.replace")
                replaceAsset(staged, id(command["asset"]),
                             command["data"].isNull() ? AssetPayloadPtr{} : data(),
                             command.contains("mediaType")
                                 ? std::optional(command["mediaType"].toString().toStdString())
                                 : std::nullopt);
            else
                eraseAsset(staged, id(command["asset"]));
        } else if (name == "material.create" || name == "material.edit" ||
                   name == "material.delete" || name == "material.assign") {
            if (command.contains("name") && !command["name"].isString())
                throw std::runtime_error("Material name must be a string");
            auto color = [&] {
                const auto value = point(command["color"]);
                for (auto component : {value.x, value.y, value.z})
                    if (component < 0 || component > 1)
                        throw std::runtime_error("Invalid material color");
                return std::array<float, 3>{float(value.x), float(value.y), float(value.z)};
            };
            auto opacity = [&] {
                const auto value = number(command["opacity"]);
                if (value < 0 || value > 1)
                    throw std::runtime_error("Invalid opacity");
                return float(value);
            };
            if (name == "material.create")
                createMaterial(staged, command["name"].toString().toStdString(), color(),
                               command.contains("opacity") ? opacity() : 1,
                               !command.contains("asset") || command["asset"] == "0"
                                   ? Id{}
                                   : id(command["asset"]));
            else if (name == "material.edit")
                editMaterial(
                    staged, id(command["material"]),
                    command.contains("name")
                        ? std::optional(command["name"].toString().toStdString())
                        : std::nullopt,
                    command.contains("color") ? std::optional(color()) : std::nullopt,
                    command.contains("opacity") ? std::optional(opacity()) : std::nullopt,
                    command.contains("asset")
                        ? std::optional(command["asset"] == "0" ? Id{} : id(command["asset"]))
                        : std::nullopt);
            else if (name == "material.delete")
                eraseMaterial(staged, id(command["material"]));
            else {
                const auto side =
                    command.contains("side") ? command["side"].toString() : QString("both");
                if (side != "front" && side != "back" && side != "both")
                    throw std::runtime_error("Unknown material side");
                compose(assignMaterial(staged, id(command["body"]),
                                       command.contains("face") ? std::optional(id(command["face"]))
                                                                : std::nullopt,
                                       command["material"] == "0" ? Id{} : id(command["material"]),
                                       side != "back", side != "front"));
            }
        } else if (name == "entity.position" || name == "entity.dimensions") {
            const auto frame =
                command.contains("frame") ? command["frame"].toString() : QString("world");
            if (frame != "world" && frame != "parent")
                throw std::runtime_error("Unknown entity edit frame");
            if (name == "entity.position")
                compose(positionEntity(staged, id(command["body"]), point(command["position"]),
                                       frame == "world"));
            else
                compose(dimensionEntity(staged, id(command["body"]), point(command["dimensions"]),
                                        frame == "world"));
        } else if (name == "entity.properties") {
            if (!command["values"].isObject())
                throw std::runtime_error("Entity properties must be an object");
            EntityProperties properties;
            const auto values = command["values"].toObject();
            for (auto it = values.begin(); it != values.end(); ++it) {
                const auto key = it.key().toStdString();
                if (it.value().isBool())
                    properties[key] = it.value().toBool();
                else if (it.value().isDouble())
                    properties[key] = number(it.value());
                else if (it.value().isString())
                    properties[key] = it.value().toString().toStdString();
                else
                    throw std::runtime_error(
                        "Property values must be booleans, finite numbers or strings");
            }
            compose(setEntityProperties(staged, id(command["body"]), std::move(properties)));
        } else if (name.startsWith("tag.")) {
            auto parent = [&] { return command["parent"] == "0" ? Id{0} : id(command["parent"]); };
            if (command.contains("name") && !command["name"].isString())
                throw std::runtime_error("Tag name must be a string");
            if (name == "tag.create") {
                if (command.contains("folder") && !command["folder"].isBool())
                    throw std::runtime_error("Tag folder flag must be boolean");
                createTag(staged, command["name"].toString().toStdString(),
                          command.contains("parent") ? parent() : 0, command["folder"].toBool());
            } else if (name == "tag.edit") {
                if (command.contains("visible") && !command["visible"].isBool())
                    throw std::runtime_error("Tag visibility must be boolean");
                editTag(staged, id(command["tag"]),
                        command.contains("name")
                            ? std::optional{command["name"].toString().toStdString()}
                            : std::nullopt,
                        command.contains("parent") ? std::optional{parent()} : std::nullopt,
                        command.contains("visible") ? std::optional{command["visible"].toBool()}
                                                    : std::nullopt);
            } else if (name == "tag.delete")
                eraseTag(staged, id(command["tag"]));
            else
                compose(assignTag(staged, id(command["body"]),
                                  command["tag"] == "0" ? 0 : id(command["tag"])));
        } else if (name == "scene.rename") {
            if (!command["name"].isString())
                throw std::runtime_error("Entity name must be a string");
            compose(renameEntity(staged, id(command["body"]),
                                 command["name"].toString().toStdString()));
        } else if (name == "geometry.erase_selection" || name == "group.selection" ||
                   name == "component.selection") {
            const auto records = array(command["entities"]);
            if (records.empty() || records.size() > 10000)
                throw std::runtime_error("Selection must contain 1–10000 entities");
            Selection selection;
            if (command.contains("context"))
                selection.enter(staged, command["context"] == "0" ? 0 : id(command["context"]));
            if (command.contains("showHidden")) {
                if (!command["showHidden"].isBool())
                    throw std::runtime_error("showHidden must be boolean");
                selection.showHidden(staged, command["showHidden"].toBool());
            }
            SelectionSet entities;
            for (const auto &record : records) {
                if (!record.isObject())
                    throw std::runtime_error("Expected selected entity object");
                const auto object = record.toObject();
                fields(object, {"body", "kind", "entity"});
                const auto kind = object["kind"];
                SelectedEntity entity{id(object["body"]), SelectionKind::Body, 0};
                if (kind == "context") {
                    if (object["entity"] != "0")
                        throw std::runtime_error("Context selection requires entity zero");
                } else {
                    entity.entity = id(object["entity"]);
                    if (kind == "face")
                        entity.kind = SelectionKind::Face;
                    else if (kind == "edge")
                        entity.kind = SelectionKind::Edge;
                    else if (kind == "guide")
                        entity.kind = SelectionKind::Guide;
                    else
                        throw std::runtime_error("Unknown selection kind");
                }
                if (!selection.exists(staged, entity) || !entities.insert(entity).second)
                    throw std::runtime_error("Missing or duplicate selected entity");
            }
            for (auto entity : entities)
                if (!selection.selectable(staged, entity))
                    throw std::runtime_error(
                        "Selected entity is outside the editable context or locked/hidden");
            selection.apply(staged, entities, SelectionMode::Replace);
            if (name == "group.selection" || name == "component.selection") {
                if (command.contains("name") && !command["name"].isString())
                    throw std::runtime_error("Group name must be a string");
                const auto result = groupSelected(
                    staged, selection,
                    command.value("name")
                        .toString(name == "component.selection" ? "Component" : "Group")
                        .toStdString());
                compose(result.changes);
                for (const auto &[source, target] : result.movedGeometry) {
                    const auto &body = *staged.bodies().at(target);
                    auto identities = [](const auto &records) {
                        QJsonObject mapping;
                        for (const auto &[id, record] : records)
                            mapping[QString::number(id)] = QString::number(id);
                        return mapping;
                    };
                    transfers.append(QJsonObject{{"sourceBody", QString::number(source)},
                                                 {"body", QString::number(target)},
                                                 {"vertices", identities(body.surface.vertices)},
                                                 {"edges", identities(body.topology.edges)},
                                                 {"faces", identities(body.surface.faces)},
                                                 {"curves", identities(body.curves)},
                                                 {"guides", identities(body.guides)}});
                }
                if (name == "component.selection") {
                    const auto component =
                        createComponent(staged, result.group,
                                        command.value("name").toString("Component").toStdString());
                    compose(component.changes);
                    componentOperations.append(
                        QJsonObject{{"definition", QString::number(component.definition)},
                                    {"instance", QString::number(component.instance)}});
                }
            } else
                compose(eraseSelected(staged, selection));
        } else if (name == "guide.erase") {
            compose(staged.eraseGuide(id(command["body"]), id(command["guide"])));
        } else if (name == "guide.clear") {
            compose(staged.clearGuides(command["body"] == "0" ? 0 : id(command["body"])));
        } else if (name.startsWith("guide.")) {
            const auto context = command["body"] == "0" ? Id{0} : id(command["body"]);
            if (command.contains("space") && command["space"] != "local" &&
                command["space"] != "world")
                throw std::runtime_error("Guide space must be local or world");
            const bool worldSpace = command["space"] == "world";
            Guide guide;
            if (name == "guide.point")
                guide = guidePoint(point(command["origin"]));
            else if (name == "guide.line")
                guide = guideLine(point(command["origin"]), point(command["direction"]));
            else if (name == "guide.angle")
                guide = angledGuide(DrawingPlane::make(point(command["origin"]),
                                                       point(command["normal"]),
                                                       point(command["xAxis"])),
                                    number(command["angle"]));
            else if (name == "guide.offset") {
                const auto source = staged.bodies().at(context)->guides.at(id(command["guide"]));
                if (source.kind != GuideKind::Line)
                    throw std::runtime_error("Only guide lines can be offset");
                const auto world = staged.worldTransform(context);
                guide = worldSpace
                            ? guideLine(world.point(source.origin), world.vector(source.direction))
                            : source;
                guide = offsetGuide(guide, point(command["normal"]), number(command["distance"]));
            } else
                throw std::runtime_error("Unavailable guide command");
            if (worldSpace && context) {
                const auto inverse = staged.worldTransform(context).inverse();
                guide = guide.kind == GuideKind::Point ? guidePoint(inverse.point(guide.origin))
                                                       : guideLine(inverse.point(guide.origin),
                                                                   inverse.vector(guide.direction));
            }
            compose(staged.addGuide(context, guide));
        } else if (name == "geometry.face") {
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
        } else if (name == "geometry.offset") {
            if (command.contains("space") && command["space"] != "local" &&
                command["space"] != "world")
                throw std::runtime_error("Offset space must be local or world");
            compose(staged.offsetFace(id(command["body"]), id(command["face"]),
                                      number(command["distance"]), command["space"] == "world"));
        } else if (name == "geometry.reverse_faces") {
            const auto entities = array(command["entities"]);
            if (entities.empty() || entities.size() > 4096)
                throw OrientationError("ORIENTATION_SELECTION", "Choose 1–4096 editable faces");
            SelectionSet selected;
            for (auto value : entities) {
                if (!value.isObject())
                    throw std::runtime_error("Expected selected face object");
                const auto entity = value.toObject();
                fields(entity, {"body", "face"});
                if (!selected.insert({id(entity["body"]), SelectionKind::Face, id(entity["face"])})
                         .second)
                    throw OrientationError("ORIENTATION_SELECTION", "Duplicate selected face");
            }
            compose(reverseSelectedFaces(staged, selected,
                                         command["context"] == "0" ? 0 : id(command["context"])));
        } else if (name == "geometry.orient_faces") {
            compose(orientConnectedFaces(staged, id(command["body"]), id(command["face"]),
                                         command["context"] == "0" ? 0 : id(command["context"])));
        } else if (name == "geometry.boolean" || name == "geometry.trim" ||
                   name == "geometry.split" || name == "geometry.outer_shell") {
            const bool legacy = name == "geometry.boolean";
            const auto operation = legacy ? command["operation"].toString() : name.mid(9);
            if (legacy && operation != "union" && operation != "subtract" &&
                operation != "intersection")
                throw BooleanError("BOOLEAN_OPERATION", "Choose union, subtract or intersection");
            const auto retentionKey = operation == "trim" ? "keepTarget" : "keepOperands";
            if (!command[retentionKey].isBool())
                throw BooleanError("BOOLEAN_OPERANDS", "Explicitly choose operand retention");
            const bool keep = command[retentionKey].toBool();
            const auto target = id(command["body"]), tool = id(command["tool"]);
            const auto result =
                solidBodies(staged, target, tool,
                            operation == "union"          ? SolidAction::Union
                            : operation == "subtract"     ? SolidAction::Subtract
                            : operation == "intersection" ? SolidAction::Intersect
                            : operation == "trim"         ? SolidAction::Trim
                            : operation == "split"        ? SolidAction::Split
                                                          : SolidAction::OuterShell,
                            command["context"] == "0" ? 0 : id(command["context"]), keep);
            compose(result.changes);
            QJsonArray parts;
            for (const auto &part : result.parts) {
                QJsonArray faces;
                for (const auto &[face, source] : part.sources)
                    faces.append(
                        QJsonObject{{"face", QString::number(face)},
                                    {"sourceBody", QString::number(source.operand ? tool : target)},
                                    {"sourceFace", QString::number(source.face)},
                                    {"reversed", source.reversed}});
                QJsonObject record{{"body", QString::number(part.body)},
                                   {"generatedVolume", part.generatedVolume},
                                   {"faces", faces}};
                if (!legacy)
                    record["portion"] = QString::fromStdString(part.portion);
                parts.append(record);
            }
            QJsonObject record{{"sourceBody", command["body"]},
                               {"toolBody", command["tool"]},
                               {"operation", operation},
                               {"parts", parts}};
            if (legacy) {
                record["keepOperands"] = keep;
                booleans.append(record);
            } else {
                record["keepTarget"] = keep;
                record["keepTool"] = keep || operation == "trim";
                solidOperations.append(record);
            }
        } else if (name == "geometry.intersect") {
            const auto entities = array(command["entities"]);
            if (entities.empty() || entities.size() > 128)
                throw IntersectionError("INTERSECTION_LIMIT", "Select 1–128 target faces");
            SelectionSet targets;
            for (auto value : entities) {
                if (!value.isObject())
                    throw std::runtime_error("Expected intersection target object");
                const auto entity = value.toObject();
                fields(entity, {"body", "face"});
                targets.insert({id(entity["body"]), SelectionKind::Face, id(entity["face"])});
            }
            const auto mode = command["mode"].toString();
            if (mode != "selected" && mode != "context" && mode != "model")
                throw IntersectionError("INTERSECTION_MODE", "Choose selected, context or model");
            compose(intersectSelected(staged, targets,
                                      mode == "selected"  ? IntersectionMode::Selected
                                      : mode == "context" ? IntersectionMode::Context
                                                          : IntersectionMode::Model,
                                      command["context"] == "0" ? 0 : id(command["context"]),
                                      outerScene, excludedInstance));
        } else if (name == "geometry.sweep") {
            if (command.contains("space") && command["space"] != "local" &&
                command["space"] != "world")
                throw std::runtime_error("Sweep space must be local or world");
            if (command.contains("closed") && !command["closed"].isBool())
                throw std::runtime_error("closed must be boolean");
            const auto stations = array(command["path"]);
            if (stations.size() > 129)
                throw SweepError("SWEEP_LIMIT", "Sweep accepts at most 128 path stations");
            std::vector<Vec3> path;
            for (auto station : stations)
                path.push_back(point(station));
            const auto result = sweepFace(staged, id(command["body"]), id(command["face"]), path,
                                          command["closed"].toBool(), command["space"] == "world");
            compose(result.changes);
            QJsonArray sides, segments;
            for (const auto &[edge, faces] : result.sides)
                sides.append(QJsonObject{{"vertices", ids(std::vector<Id>{edge[0], edge[1]})},
                                         {"faces", ids(faces)}});
            for (const auto &faces : result.segments)
                segments.append(ids(faces));
            sweeps.append(QJsonObject{{"sourceBody", command["body"]},
                                      {"sourceFace", command["face"]},
                                      {"body", QString::number(result.body)},
                                      {"caps", ids(result.caps)},
                                      {"sides", sides},
                                      {"segments", segments}});
        } else if (name == "geometry.push_pull") {
            if (command.contains("newFace") && !command["newFace"].isBool())
                throw std::runtime_error("newFace must be boolean");
            compose(staged.pushPull(id(command["body"]), id(command["face"]),
                                    number(command["distance"]), command["newFace"].toBool()));
        } else if (name == "geometry.extrude_isolated") {
            fields(command, {"command", "body", "face", "distance"});
            staged.extrude(id(command["body"]), id(command["face"]), number(command["distance"]));
        } else if (name == "geometry.transform_selection" || name == "geometry.array_selection") {
            const auto records = array(command["entities"]);
            if (records.empty() || records.size() > 10000)
                throw std::runtime_error("Transform requires 1–10000 targets");
            TransformTargets targets;
            for (const auto &record : records) {
                if (!record.isObject())
                    throw std::runtime_error("Expected transform target object");
                const auto object = record.toObject();
                fields(object, {"body", "kind", "entity"});
                const auto kind = object["kind"];
                TransformTarget target{id(object["body"]), TransformKind::Context, 0};
                if (kind == "context") {
                    if (object["entity"] != "0")
                        throw std::runtime_error("Context target requires entity zero");
                } else {
                    target.entity = id(object["entity"]);
                    if (kind == "face")
                        target.kind = TransformKind::Face;
                    else if (kind == "edge")
                        target.kind = TransformKind::Edge;
                    else if (kind == "vertex")
                        target.kind = TransformKind::Vertex;
                    else if (kind == "guide")
                        target.kind = TransformKind::Guide;
                    else
                        throw std::runtime_error("Unknown transform target kind");
                }
                if (!targets.insert(target).second)
                    throw std::runtime_error("Duplicate transform target");
            }
            if (command.contains("space") && command["space"] != "local" &&
                command["space"] != "world")
                throw std::runtime_error("Transform space must be local or world");
            const auto pivot = command.contains("pivot") ? point(command["pivot"]) : Vec3{};
            const auto space =
                command["space"] == "local" ? TransformSpace::Local : TransformSpace::World;
            std::vector<TransformResult> instances;
            const auto isArray = name == "geometry.array_selection";
            if (isArray) {
                CopyArray spec;
                const auto count = number(command["count"]);
                if (count < 1 || count > maxArrayCopies || count != std::floor(count))
                    throw std::runtime_error("Array count must be an integer from 1 to 100");
                spec.copies = unsigned(count);
                if (command.contains("divide") && !command["divide"].isBool())
                    throw std::runtime_error("divide must be boolean");
                spec.divide = command["divide"].toBool();
                if (command["mode"] == "linear") {
                    if (command.contains("axis") || command.contains("angle"))
                        throw std::runtime_error("Linear array accepts delta, not axis or angle");
                    spec.delta = point(command["delta"]);
                } else if (command["mode"] == "radial") {
                    if (command.contains("delta"))
                        throw std::runtime_error("Radial array accepts axis and angle, not delta");
                    spec.mode = ArrayMode::Radial;
                    spec.axis = point(command["axis"]);
                    spec.angle = number(command["angle"]);
                } else
                    throw std::runtime_error("Array mode must be linear or radial");
                auto result = copyArraySelected(staged, targets, spec, pivot, space);
                compose(result.changes);
                instances = std::move(result.instances);
            } else {
                const auto values = array(command["matrix"]);
                if (values.size() != 16)
                    throw std::runtime_error("Transform matrix requires sixteen numbers");
                Transform matrix;
                for (int i = 0; i < 16; ++i)
                    matrix.m[i] = number(values[i]);
                if (command.contains("copy") && !command["copy"].isBool())
                    throw std::runtime_error("copy must be boolean");
                instances.push_back(transformSelected(staged, targets, matrix, pivot, space,
                                                      command["copy"].toBool()));
                compose(instances.back().changes);
            }
            unsigned index = 0;
            for (const auto &result : instances) {
                const auto first = copies.size();
                ++index;
                for (const auto &[source, copied] : result.copies)
                    copies.append(QJsonObject{{"sourceBody", QString::number(source)},
                                              {"body", QString::number(copied)}});
                for (const auto &[body, copied] : result.geometryCopies) {
                    auto ids = [](const auto &mapping) {
                        QJsonObject result;
                        for (const auto &[source, target] : mapping)
                            result[QString::number(source)] = QString::number(target);
                        return result;
                    };
                    copies.append(QJsonObject{{"sourceBody", QString::number(body)},
                                              {"body", QString::number(body)},
                                              {"vertices", ids(copied.vertices)},
                                              {"edges", ids(copied.edges)},
                                              {"faces", ids(copied.faces)},
                                              {"curves", ids(copied.curves)},
                                              {"guides", ids(copied.guides)}});
                }
                if (isArray)
                    for (auto i = first; i < copies.size(); ++i) {
                        auto record = copies[i].toObject();
                        record["instance"] = int(index);
                        copies[i] = record;
                    }
            }
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
        } else if (name.startsWith("component.")) {
            if (command.contains("name") && !command["name"].isString())
                throw std::runtime_error("Component name must be a string");
            auto matrix = [&] {
                const auto values = array(command["matrix"]);
                if (values.size() != 16)
                    throw std::runtime_error("Expected 16 matrix coefficients");
                Transform result;
                for (int i = 0; i < 16; ++i)
                    result.m[i] = number(values[i]);
                result.validate();
                return result;
            };
            ComponentResult result;
            Id scopeInstance = 0;
            InstancePtr scopeBefore;
            QJsonObject innerResult;
            std::map<Id, Id> scopeMembers;
            std::optional<Document> scopeDraft;
            if (name == "component.create")
                result = createComponent(staged, id(command["body"]),
                                         command.value("name").toString("Component").toStdString());
            else if (name == "component.instance")
                result = placeComponent(staged, id(command["definition"]), matrix(),
                                        command.contains("parent") && command["parent"] != "0"
                                            ? id(command["parent"])
                                            : 0,
                                        command.value("name").toString().toStdString());
            else if (name == "component.make_unique")
                result = makeComponentUnique(staged, id(command["body"]));
            else if (name == "component.replace")
                result = replaceComponent(staged, id(command["body"]), id(command["definition"]));
            else if (name == "component.axes")
                result = setComponentAxes(staged, id(command["definition"]), matrix());
            else if (name == "component.edit" || name == "component.edit_instance") {
                auto commands = array(command["commands"]);
                if (name == "component.edit_instance") {
                    // This command never grants shared-definition or global-resource edits.
                    for (auto value : commands) {
                        const auto nested = value.toObject();
                        const auto operation = nested.value("command").toString();
                        if (nested.contains("commands") ||
                            !(operation.startsWith("geometry.") || operation == "entity.position" ||
                              operation == "entity.dimensions" ||
                              operation == "entity.properties" || operation == "material.assign" ||
                              operation == "material.color"))
                            throw std::runtime_error("Instance edit requires ordinary scoped "
                                                     "geometry or appearance commands");
                    }
                    const auto unique = makeComponentUnique(staged, id(command["body"]));
                    compose(unique.changes);
                    command["instance"] = QString::number(unique.instance);
                    command["definition"] = QString::number(unique.definition);
                }
                Transform frame;
                if (command.contains("instance")) {
                    scopeInstance = id(command["instance"]);
                    scopeBefore = staged.instances().at(scopeInstance);
                    if (scopeBefore->definition != id(command["definition"]))
                        throw std::runtime_error(
                            "Instance no longer belongs to the requested definition");
                    frame = staged.worldTransform(scopeInstance);
                }
                for (auto value : commands) {
                    const auto nested = value.toObject().value("command");
                    if (!scopeInstance && nested == "geometry.intersect" &&
                        value.toObject().value("mode") == "model")
                        throw IntersectionError("INTERSECTION_SCOPE",
                                                "Whole-model intersection inside a component "
                                                "requires an explicit instance scope");
                    if (nested.toString().startsWith("assembly.") || nested == "component.edit" ||
                        nested == "component.edit_instance" || nested == "component.axes" ||
                        (nested.toString().startsWith("tag.") && nested != "tag.assign"))
                        throw std::runtime_error(
                            "Use a separate explicit scope for shared definition or axis edits");
                }
                result = editComponentDefinition(
                    staged, id(command["definition"]),
                    [&](Document &draft) {
                        if (scopeInstance)
                            scopeMembers = componentScopeMembers(staged, draft, scopeInstance);
                        const auto scopedCommands =
                            scopeInstance
                                ? canonicalComponentCommands(staged, draft, scopeInstance, commands)
                                : commands;
                        innerResult = executeBatchWithReferences(
                            draft,
                            {{"apiVersion", 1},
                             {"documentId", QString::fromStdString(draft.identity())},
                             {"expectedRevision", QString::number(draft.revision())},
                             {"commands", scopedCommands}},
                            response == BatchResponse::Full ? BatchResponse::Full
                                                            : BatchResponse::Changes,
                            scopeInstance ? &staged : nullptr, scopeInstance);
                        if (scopeInstance)
                            scopeDraft = draft;
                        return decodedChanges(innerResult["changes"].toObject());
                    },
                    frame);
                result.instance = scopeInstance;
            } else
                throw std::runtime_error("Unavailable component command");
            compose(result.changes);
            QJsonObject operation{{"definition", QString::number(result.definition)},
                                  {"instance", QString::number(result.instance)}};
            if (name == "component.edit" || name == "component.edit_instance") {
                QJsonObject normalized;
                for (auto [source, target] : result.movedGeometry)
                    normalized[QString::number(source)] = QString::number(target);
                operation["normalizedMembers"] = normalized;
                // Definition-only callers receive canonical member IDs here;
                // instance-scoped callers also get scene-resolved top-level maps.
                operation["sweeps"] = innerResult["sweeps"].toArray();
                operation["booleans"] = innerResult["booleans"].toArray();
                operation["solidOperations"] = innerResult["solidOperations"].toArray();
                if (scopeInstance) {
                    const auto after = componentScopeMembers(staged, *scopeDraft, scopeInstance);
                    QJsonArray scopedCreated;
                    for (auto value : innerResult["created"].toArray()) {
                        const auto member = id(value);
                        if (after.contains(member))
                            scopedCreated.append(QString::number(after.at(member)));
                    }
                    for (auto [source, member] : result.movedGeometry)
                        scopedCreated.append(QString::number(after.at(member)));
                    operation["created"] = scopedCreated;
                    auto resolve = [&](const char *key, QJsonArray &output) {
                        for (auto value : innerResult[key].toArray()) {
                            auto record = value.toObject();
                            const auto source = id(record["sourceBody"]);
                            auto target = id(record["body"]);
                            if (result.movedGeometry.contains(target))
                                target = result.movedGeometry.at(target);
                            if (!scopeMembers.contains(source) || !after.contains(target))
                                continue;
                            record["sourceBody"] = QString::number(scopeMembers.at(source));
                            record["body"] = QString::number(after.at(target));
                            output.append(record);
                        }
                    };
                    resolve("copies", copies);
                    resolve("transfers", transfers);
                    resolve("sweeps", sweeps);
                    auto resolveSolids = [&](const char *key, QJsonArray &output) {
                        for (auto value : innerResult[key].toArray()) {
                            auto record = value.toObject();
                            for (auto key : {"sourceBody", "toolBody"})
                                record[key] = QString::number(scopeMembers.at(id(record[key])));
                            QJsonArray parts;
                            for (auto partValue : record["parts"].toArray()) {
                                auto part = partValue.toObject();
                                const auto member = id(part["body"]);
                                if (!after.contains(member))
                                    continue;
                                part["body"] = QString::number(after.at(member));
                                QJsonArray faces;
                                for (auto faceValue : part["faces"].toArray()) {
                                    auto face = faceValue.toObject();
                                    face["sourceBody"] =
                                        QString::number(scopeMembers.at(id(face["sourceBody"])));
                                    faces.append(face);
                                }
                                part["faces"] = faces;
                                parts.append(part);
                            }
                            record["parts"] = parts;
                            output.append(record);
                        }
                    };
                    resolveSolids("booleans", booleans);
                    resolveSolids("solidOperations", solidOperations);
                }
            } else if (name == "component.create")
                for (auto [source, target] : result.movedGeometry) {
                    const auto &body = *staged.bodies().at(target);
                    auto identities = [](const auto &records) {
                        QJsonObject mapping;
                        for (const auto &[id, record] : records)
                            mapping[QString::number(id)] = QString::number(id);
                        return mapping;
                    };
                    transfers.append(QJsonObject{{"sourceBody", QString::number(source)},
                                                 {"body", QString::number(target)},
                                                 {"vertices", identities(body.surface.vertices)},
                                                 {"edges", identities(body.topology.edges)},
                                                 {"faces", identities(body.surface.faces)},
                                                 {"curves", identities(body.curves)},
                                                 {"guides", identities(body.guides)}});
                }
            componentOperations.append(operation);
        } else if (name == "group.create") {
            std::set<Id> members;
            for (const auto &value : array(command["members"]))
                if (!members.insert(id(value)).second)
                    throw std::runtime_error("Duplicate group member");
            if (command.contains("name") && !command["name"].isString())
                throw std::runtime_error("Group name must be a string");
            createGroup(staged, members, command.value("name").toString("Group").toStdString());
        } else if (name == "geometry.merge_context") {
            std::optional<std::set<Id>> members;
            if (command.contains("members")) {
                members.emplace();
                const auto records = array(command["members"]);
                if (records.size() > 10000)
                    throw std::runtime_error("Too many merge members");
                for (const auto &record : records)
                    if (!members->insert(id(record)).second)
                        throw std::runtime_error("Duplicate merge member");
            }
            mergeContext(command["context"] == "0" ? 0 : id(command["context"]), members);
        } else if (name == "group.explode") {
            const auto body = id(command["body"]);
            const auto context = enclosingGroup(staged, body);
            if (command.contains("merge") && !command["merge"].isBool())
                throw std::runtime_error("Explode merge must be boolean");
            compose(explodeGroup(staged, body));
            if (command.value("merge").toBool(true))
                mergeContext(context);
        } else if (name == "scene.reparent") {
            compose(reparentPreservingWorld(staged, id(command["body"]),
                                            command["parent"] == "0" ? 0 : id(command["parent"])));
        } else if (name == "scene.state") {
            auto flag = [&](const char *key) -> std::optional<bool> {
                if (!command.contains(key))
                    return {};
                if (!command[key].isBool())
                    throw std::runtime_error("Entity state flags must be boolean");
                return command[key].toBool();
            };
            compose(setEntityState(staged, command["body"] == "0" ? 0 : id(command["body"]),
                                   flag("hidden"), flag("locked")));
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
    Edit edit{historyLabel, {}};
    edit.metadata = historyMetadata;
    appendSceneMetadataChanges(edit, doc, staged);
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
                                    std::move(mapping.vertices), std::move(mapping.edges), true});
        }
    }
    if (edit.changes.empty() && edit.definitions.empty() && edit.instances.empty() &&
        edit.tags.empty() && edit.materials.empty() && edit.assets.empty() && !edit.displayUnits)
        throw std::runtime_error("Batch has no committed changes");
    edit.nextIdFloor = staged.nextId();
    created = QJsonArray();
    for (const auto &change : edit.changes)
        if (!change.before && change.after)
            created.append(QString::number(change.id));
    QJsonArray createdDefinitions;
    for (const auto &change : edit.definitions)
        if (!change.before && change.after)
            createdDefinitions.append(QString::number(change.id));
    QJsonArray createdAssets;
    for (const auto &change : edit.assets)
        if (!change.before && change.after)
            createdAssets.append(QString::number(change.id));
    QJsonArray createdMaterials;
    for (const auto &change : edit.materials)
        if (!change.before && change.after)
            createdMaterials.append(QString::number(change.id));
    QJsonArray createdTags;
    for (const auto &change : edit.tags)
        if (!change.before && change.after)
            createdTags.append(QString::number(change.id));
    // Generated mappings name only faces that survive subsequent commands in
    // this batch. Preserve segment/edge slots even when their face list is empty.
    QJsonArray survivingSweeps;
    for (auto value : sweeps) {
        auto record = value.toObject();
        const auto body = id(record["body"]);
        if (!staged.bodies().contains(body))
            continue;
        const auto &faces = staged.bodies().at(body)->surface.faces;
        auto prune = [&](QJsonArray values) {
            QJsonArray kept;
            for (auto face : values)
                if (faces.contains(id(face)))
                    kept.append(face);
            return kept;
        };
        record["caps"] = prune(record["caps"].toArray());
        QJsonArray sides, segments;
        for (auto value : record["sides"].toArray()) {
            auto side = value.toObject();
            side["faces"] = prune(side["faces"].toArray());
            sides.append(side);
        }
        for (auto value : record["segments"].toArray())
            segments.append(prune(value.toArray()));
        record["sides"] = sides;
        record["segments"] = segments;
        survivingSweeps.append(record);
    }
    auto pruneSolids = [&](const QJsonArray &records) {
        QJsonArray surviving;
        for (auto value : records) {
            auto record = value.toObject();
            QJsonArray parts;
            for (auto partValue : record["parts"].toArray()) {
                auto part = partValue.toObject();
                const auto body = id(part["body"]);
                if (!staged.bodies().contains(body))
                    continue;
                QJsonArray faces;
                for (auto faceValue : part["faces"].toArray())
                    if (staged.bodies().at(body)->surface.faces.contains(
                            id(faceValue.toObject()["face"])))
                        faces.append(faceValue);
                part["faces"] = faces;
                parts.append(part);
            }
            record["parts"] = parts;
            surviving.append(record);
        }
        return surviving;
    };
    const auto survivingBooleans = pruneSolids(booleans);
    const auto survivingSolidOperations = pruneSolids(solidOperations);
    const auto report = doc.apply(std::move(edit), doc.revision());
    if (response == BatchResponse::CreatedIds)
        return {{"status", "committed"},
                {"revision", QString::number(doc.revision())},
                {"created", created},
                {"createdDefinitions", createdDefinitions},
                {"createdTags", createdTags},
                {"createdMaterials", createdMaterials},
                {"createdAssets", createdAssets},
                {"recipeOperations", recipeOperations},
                {"sweeps", survivingSweeps},
                {"booleans", survivingBooleans},
                {"solidOperations", survivingSolidOperations}};
    for (qsizetype i = 0; i < componentOperations.size(); ++i) {
        auto operation = componentOperations[i].toObject();
        if (!doc.instances().contains(operation["instance"].toString().toULongLong()))
            operation["instance"] = "0";
        componentOperations[i] = operation;
    }
    QJsonObject changes;
    for (const auto &[context, change] : report)
        changes[QString::number(context)] =
            QJsonObject{{"vertices", entityChanges(change.vertices)},
                        {"edges", entityChanges(change.edges)},
                        {"faces", entityChanges(change.faces)},
                        {"curves", entityChanges(change.curves)},
                        {"guides", entityChanges(change.guides)}};
    auto surviving = [&](const QJsonArray &records) {
        QJsonArray survivors;
        for (const auto &copy : records) {
            auto record = copy.toObject();
            const auto context = id(record["body"]);
            if (!doc.bodies().contains(context))
                continue;
            const auto &body = *doc.bodies().at(context);
            auto prune = [&](const QString &key, const auto &entities) {
                if (!record.contains(key))
                    return;
                auto mapping = record[key].toObject();
                for (auto it = mapping.begin(); it != mapping.end();) {
                    if (!entities.contains(id(it.value())))
                        it = mapping.erase(it);
                    else
                        ++it;
                }
                record[key] = mapping;
            };
            prune("vertices", body.surface.vertices);
            prune("edges", body.topology.edges);
            prune("faces", body.surface.faces);
            prune("curves", body.curves);
            prune("guides", body.guides);
            survivors.append(record);
        }
        return survivors;
    };
    QJsonObject result{{"status", "committed"},
                       {"revision", QString::number(doc.revision())},
                       {"created", created},
                       {"createdDefinitions", createdDefinitions},
                       {"createdTags", createdTags},
                       {"createdMaterials", createdMaterials},
                       {"createdAssets", createdAssets},
                       {"componentOperations", componentOperations},
                       {"recipeOperations", recipeOperations},
                       {"sweeps", survivingSweeps},
                       {"booleans", survivingBooleans},
                       {"solidOperations", survivingSolidOperations},
                       {"copies", surviving(copies)},
                       {"transfers", surviving(transfers)},
                       {"changes", changes}};
    if (response == BatchResponse::Full)
        result["document"] = describe(doc);
    return result;
}
QJsonObject executeBatch(Document &doc, const QJsonObject &request, BatchResponse response) {
    return executeBatchWithReferences(doc, request, response);
}
QJsonObject executeAmend(Document &doc, const Document::AmendStamp &stamp,
                         const QJsonObject &request) {
    QJsonObject result;
    auto commands = request["commands"].toArray();
    if (commands.size() == 1 && commands[0].toObject()["command"] == "component.edit")
        commands = commands[0].toObject()["commands"].toArray();
    const auto policy =
        commands.size() == 1 && commands[0].toObject()["command"] == "geometry.array_selection"
            ? Document::AmendPolicy::CopyArray
            : Document::AmendPolicy::FixedContextCount;
    const auto report = doc.amendLast(
        stamp, [&](Document &candidate) { result = executeBatch(candidate, request); }, policy);
    QJsonObject changes;
    for (const auto &[context, change] : report)
        changes[QString::number(context)] =
            QJsonObject{{"vertices", entityChanges(change.vertices)},
                        {"edges", entityChanges(change.edges)},
                        {"faces", entityChanges(change.faces)},
                        {"curves", entityChanges(change.curves)},
                        {"guides", entityChanges(change.guides)}};
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
        if (!doc.bodies().contains(id) || doc.bodies().at(id) != body ||
            doc.worldTransform(id) != candidate.worldTransform(id))
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
        if (!doc.bodies().contains(id) || doc.bodies().at(id) != body ||
            doc.worldTransform(id) != candidate.worldTransform(id))
            geometry[QString::number(id)] = topologyDescription(candidate, id);
    result["geometry"] = geometry;
    return result;
}
} // namespace sketchy
