#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
#include <set>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F fn) {
    bool rejected = false;
    try {
        fn();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Invalid command must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document source;
        source.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto beforeQueries = encodeDocument(source);
        auto topology = executeQuery(source, {{"query", "geometry.inspect"}, {"body", "1"}});
        check(topology["edges"].toArray().size() == 4 && topology["context"] == "1",
              "Topology query exposes context-scoped edge records");
        check(executeQuery(source, {{"query", "document.describe"}}).contains("bodies"),
              "Document query registered");
        check(executeQuery(source, {{"query", "capabilities"}}).contains("commandSchemas"),
              "Capabilities query registered");
        check(executeQuery(source, {{"query", "commands.describe"}, {"name", "geometry.translate"}})
                  .contains("parameters"),
              "Command schema query registered");
        check(encodeDocument(source) == beforeQueries, "Queries never mutate model or revision");
        rejects(
            [&] { executeQuery(source, {{"query", "document.describe"}, {"mutation", true}}); });
        const QJsonObject sweep{
            {"apiVersion", 1},
            {"documentId", QString::fromStdString(source.identity())},
            {"expectedRevision", QString::number(source.revision())},
            {"commands", QJsonArray{QJsonObject{{"command", "geometry.push_pull"},
                                                {"body", "1"},
                                                {"face", "5"},
                                                {"distance", 2}}}}};
        const auto historyBefore = source.historyBytes();
        auto preview = executeQuery(source, {{"query", "geometry.preview"}, {"batch", sweep}});
        check(preview["status"] == "preview" &&
                  preview["geometry"].toObject()["1"].toObject()["faces"].toArray().size() == 6,
              "Preview includes prospective topology");
        check(encodeDocument(source) == beforeQueries && source.historyBytes() == historyBefore,
              "Preview never changes source records, revision or history");
        auto committed = source;
        auto applied = executeBatch(committed, sweep);
        check(applied["changes"] == preview["changes"] &&
                  applied["document"] == preview["document"],
              "Preview and commit agree at the same revision");
        auto previewReopened = decodeContainer(encodeContainer(committed));
        check(encodeDocument(previewReopened) == encodeDocument(committed),
              "Push/pull container round trip");
        committed.undo();
        rejects([&] { previewBatch(committed, sweep); });
        Document transformedDrawing;
        const auto context =
            transformedDrawing.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        transformedDrawing.transform(context, Transform::translation({5, 6, 7}) *
                                                  Transform::scaling({2, 3, 1}));
        const QJsonObject worldRectangle{{"command", "geometry.rectangle"},
                                         {"body", QString::number(context)},
                                         {"space", "world"},
                                         {"origin", QJsonArray{6, 7, 7}},
                                         {"normal", QJsonArray{0, 0, 1}},
                                         {"xAxis", QJsonArray{1, 1, 0}},
                                         {"width", 2},
                                         {"height", 1}};
        auto worldRequest = [&](QJsonObject command) {
            return QJsonObject{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(transformedDrawing.identity())},
                {"expectedRevision", QString::number(transformedDrawing.revision())},
                {"commands", QJsonArray{command}}};
        };
        executeBatch(transformedDrawing, worldRequest(worldRectangle));
        bool foundWorldArea = false;
        for (const auto &[id, face] : transformedDrawing.bodies().at(context)->surface.faces)
            foundWorldArea |= std::abs(transformedDrawing.worldArea(context, id) - 2) < 1e-6;
        check(foundWorldArea,
              "World-space rotated rectangle retains dimensions in nonuniform context");
        auto drawingReopened = decodeContainer(encodeContainer(transformedDrawing));
        check(encodeDocument(drawingReopened) == encodeDocument(transformedDrawing),
              "Plane drawing topology and transform persist");
        auto malformedRectangle = worldRectangle;
        malformedRectangle["space"] = "screen";
        const auto drawingBefore = encodeDocument(transformedDrawing);
        rejects([&] { executeBatch(transformedDrawing, worldRequest(malformedRectangle)); });
        check(encodeDocument(transformedDrawing) == drawingBefore,
              "Invalid drawing space rejects atomically");
        QJsonArray matrix;
        for (auto value : Transform::translation({2, 0, 0}).m)
            matrix.append(value);
        const QJsonArray cases{
            QJsonObject{{"command", "geometry.face"},
                        {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{1, 0, 0},
                                                        QJsonArray{0, 1, 0}}}}},
            QJsonObject{
                {"command", "geometry.insert_edges"},
                {"body", "1"},
                {"origin", QJsonArray{0, 0, 0}},
                {"normal", QJsonArray{0, 0, 1}},
                {"edges", QJsonArray{QJsonArray{QJsonArray{0, .5, 0}, QJsonArray{1, .5, 0}}}}},
            QJsonObject{{"command", "geometry.rectangle"},
                        {"body", "0"},
                        {"origin", QJsonArray{0, 0, 1}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"xAxis", QJsonArray{1, 0, 0}},
                        {"width", 2},
                        {"height", 1}},
            QJsonObject{{"command", "geometry.polygon"},
                        {"body", "0"},
                        {"origin", QJsonArray{0, 0, 1}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"xAxis", QJsonArray{1, 0, 0}},
                        {"radius", 2},
                        {"sides", 6}},
            QJsonObject{{"command", "geometry.polyline"},
                        {"body", "0"},
                        {"origin", QJsonArray{0, 0, 1}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"points",
                         QJsonArray{QJsonArray{0, 0, 1}, QJsonArray{2, 0, 1}, QJsonArray{2, 1, 1}}},
                        {"closed", false}},
            QJsonObject{{"command", "geometry.wire"},
                        {"body", "0"},
                        {"start", QJsonArray{0, 0, 0}},
                        {"end", QJsonArray{1, 1, 1}}},
            QJsonObject{
                {"command", "geometry.split_edge"}, {"body", "1"}, {"edge", "1"}, {"fraction", .5}},
            QJsonObject{{"command", "geometry.erase_face"}, {"body", "1"}, {"face", "5"}},
            QJsonObject{{"command", "geometry.erase_edge"}, {"body", "1"}, {"edge", "1"}},
            QJsonObject{{"command", "geometry.heal_face"},
                        {"body", "1"},
                        {"edge", "1"},
                        {"origin", QJsonArray{0, 0, 0}},
                        {"normal", QJsonArray{0, 0, 1}}},
            QJsonObject{{"command", "geometry.cleanup"}, {"body", "1"}},
            QJsonObject{
                {"command", "geometry.push_pull"}, {"body", "1"}, {"face", "5"}, {"distance", 2}},
            QJsonObject{{"command", "geometry.extrude_isolated"},
                        {"body", "1"},
                        {"face", "5"},
                        {"distance", 2}},
            QJsonObject{
                {"command", "geometry.translate"}, {"body", "1"}, {"delta", QJsonArray{1, 0, 0}}},
            QJsonObject{{"command", "geometry.delete"}, {"body", "1"}},
            QJsonObject{
                {"command", "material.color"}, {"body", "1"}, {"color", QJsonArray{.1, .2, .3}}},
            QJsonObject{{"command", "scene.transform"}, {"body", "1"}, {"matrix", matrix}}};
        check(commandCatalog().size() == cases.size(),
              "All published commands have executable cases");
        for (auto value : cases) {
            auto command = value.toObject();
            auto descriptor = commandDescription(command["command"].toString());
            check(!descriptor["label"].toString().isEmpty(), "Human command label exists");
            auto schema = descriptor["parameters"].toObject();
            auto request = [&](const Document &doc, QJsonObject item) {
                return QJsonObject{{"apiVersion", 1},
                                   {"documentId", QString::fromStdString(doc.identity())},
                                   {"expectedRevision", QString::number(doc.revision())},
                                   {"commands", QJsonArray{item}}};
            };
            Document doc = source;
            if (command["command"] == "geometry.heal_face")
                doc.eraseFace(1, 5);
            if (command["command"] == "geometry.cleanup") {
                auto before = doc.bodies().at(1);
                auto body = std::make_shared<Body>(*before);
                const auto duplicate = body->surface.nextId++;
                body->surface.vertices[duplicate] = body->surface.vertices.begin()->second;
                doc.apply({"Duplicate fixture", {{1, before, body}}}, doc.revision());
            }
            const auto baseline = doc;
            const auto original = encodeDocument(doc);
            for (auto key : schema["required"].toArray()) {
                auto missing = command;
                missing.remove(key.toString());
                rejects([&] { executeBatch(doc, request(doc, missing)); });
                check(encodeDocument(doc) == original, "Missing field never mutates document");
            }
            auto unknown = command;
            unknown["unrecognized"] = true;
            rejects([&] { executeBatch(doc, request(doc, unknown)); });
            check(encodeDocument(doc) == original, "Unknown field never mutates document");
            const auto revision = doc.revision();
            executeBatch(doc, request(doc, command));
            check(doc.revision() == revision + 1, "Registered command commits once");
            doc.undo();
            auto expectedBody = *baseline.bodies().at(1);
            check(doc.bodies().at(1)->surface.nextId >= expectedBody.surface.nextId,
                  "Undo retains surface allocator high-water mark");
            expectedBody.surface.nextId = doc.bodies().at(1)->surface.nextId;
            expectedBody.topology.nextId = doc.bodies().at(1)->topology.nextId;
            check(doc.bodies().size() == baseline.bodies().size() &&
                      *doc.bodies().at(1) == expectedBody,
                  "Registered command undo restores source geometry and metadata");
        }
        Document subdivided = source;
        const auto originalBody = *subdivided.bodies().at(1);
        auto insertion = [](QJsonArray first, QJsonArray last) {
            return QJsonObject{{"command", "geometry.insert_edges"},
                               {"body", "1"},
                               {"origin", QJsonArray{0, 0, 0}},
                               {"normal", QJsonArray{0, 0, 1}},
                               {"edges", QJsonArray{QJsonArray{first, last}}}};
        };
        auto request = [&](QJsonArray commands) {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(subdivided.identity())},
                               {"expectedRevision", QString::number(subdivided.revision())},
                               {"commands", commands}};
        };
        const auto initialRevision = subdivided.revision();
        auto outcome = executeBatch(subdivided, request({insertion({0, .5, 0}, {1, .5, 0}),
                                                         insertion({.5, 0, 0}, {.5, 1, 0})}));
        check(subdivided.revision() == initialRevision + 1 &&
                  subdivided.bodies().at(1)->surface.faces.size() == 4,
              "Two staged arrangements commit once");
        const auto mappings = outcome["changes"]
                                  .toObject()["1"]
                                  .toObject()["faces"]
                                  .toObject()["descendants"]
                                  .toObject();
        check(mappings["5"].toArray().size() == 4,
              "Face lineage composes through a multi-operation batch");
        const auto encoded = encodeContainer(subdivided);
        const auto reopened = decodeContainer(encoded);
        check(*reopened.bodies().at(1) == *subdivided.bodies().at(1),
              "Arrangement persists exact topology");
        subdivided.undo();
        check(subdivided.bodies().at(1)->surface.faces == originalBody.surface.faces &&
                  subdivided.bodies().at(1)->topology.edges == originalBody.topology.edges,
              "Arrangement batch undo restores original connectivity");
        subdivided.redo();
        check(subdivided.bodies().at(1)->surface == reopened.bodies().at(1)->surface,
              "Arrangement batch redo restores result");
        const auto beforeFailure = encodeDocument(subdivided);
        rejects([&] {
            executeBatch(subdivided, request({insertion({0, .25, 0}, {1, .25, 0}),
                                              insertion({0, 0, 0}, {1, 1, .1})}));
        });
        check(encodeDocument(subdivided) == beforeFailure,
              "Invalid later insertion rolls back the whole batch");
        Document seam;
        auto body = std::make_shared<Body>();
        body->id = 1;
        body->surface.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto adjacent = body->surface.addFace({{{2, 0, 0}, {4, 0, 0}, {4, 2, 0}, {2, 2, 0}}});
        std::set<Id> duplicated;
        for (auto &vertex : body->surface.faces.at(adjacent).loops[0])
            if (body->surface.vertices.at(vertex).x == 2) {
                const auto copy = body->surface.nextId++;
                body->surface.vertices[copy] = body->surface.vertices.at(vertex);
                duplicated.insert(copy);
                vertex = copy;
            }
        seam.apply({"Seam fixture", {{1, nullptr, body}}}, seam.revision());
        auto seamBefore = seam.bodies().at(1);
        Id keptEdge = 0, duplicateEdge = 0;
        for (const auto &[id, edge] : seamBefore->topology.edges)
            if (seamBefore->surface.vertices.at(edge.a).x == 2 &&
                seamBefore->surface.vertices.at(edge.b).x == 2) {
                if (duplicated.contains(edge.a))
                    duplicateEdge = id;
                else
                    keptEdge = id;
            }
        check(keptEdge && duplicateEdge, "Seam edge fixture");
        const auto seamResult = executeBatch(
            seam,
            {{"apiVersion", 1},
             {"documentId", QString::fromStdString(seam.identity())},
             {"expectedRevision", QString::number(seam.revision())},
             {"commands", QJsonArray{QJsonObject{{"command", "geometry.cleanup"}, {"body", "1"}},
                                     QJsonObject{{"command", "geometry.split_edge"},
                                                 {"body", "1"},
                                                 {"edge", QString::number(keptEdge)},
                                                 {"fraction", .5}}}}});
        const auto descendants = seamResult["changes"]
                                     .toObject()["1"]
                                     .toObject()["edges"]
                                     .toObject()["descendants"]
                                     .toObject();
        check(descendants[QString::number(duplicateEdge)].toArray().size() == 2 &&
                  descendants[QString::number(duplicateEdge)] ==
                      descendants[QString::number(keptEdge)],
              "Cleanup-to-retained-edge lineage composes with subsequent split");
        const auto persisted = decodeContainer(encodeContainer(seam));
        check(*persisted.bodies().at(1) == *seam.bodies().at(1),
              "Cleanup/split batch persists exact merged topology");
        seam.undo();
        check(seam.bodies().at(1)->topology.edges == seamBefore->topology.edges &&
                  seam.bodies().at(1)->surface.vertices == seamBefore->surface.vertices,
              "Cleanup/split batch is one reversible operation");
        Document color = source;
        rejects([&] {
            executeBatch(
                color,
                {{"apiVersion", 1},
                 {"documentId", QString::fromStdString(color.identity())},
                 {"expectedRevision", QString::number(color.revision())},
                 {"commands", QJsonArray{QJsonObject{{"command", "material.color"},
                                                     {"body", "1"},
                                                     {"color", QJsonArray{1.00000001, 0, 0}}}}}});
        });
        check(color.revision() == source.revision(),
              "Out-of-range double color cannot round into range");
        rejects([] { commandDescription("internal.commit"); });
        std::cout << "Command catalog, required/unknown fields, handlers, single-step undo and "
                     "color bounds passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
