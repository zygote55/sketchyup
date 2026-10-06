#include "automation/commands.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/face_orientation.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
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
        const QJsonObject inferenceQuery{
            {"query", "geometry.infer"},
            {"clipFromWorld", QJsonArray{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -.1, 0, 0, 0, 0, 1}},
            {"worldFromClip", QJsonArray{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -10, 0, 0, 0, 0, 1}},
            {"viewport", QJsonArray{1000, 800}},
            {"pointer", QJsonArray{503, 403}}};
        const auto inferred = executeQuery(source, inferenceQuery);
        check(!inferred["candidates"].toArray().empty() &&
                  inferred["candidates"].toArray()[0].toObject()["kind"] == "Endpoint",
              "Inference query publishes ranked logical-pixel candidates");
        check(encodeDocument(source) == beforeQueries, "Inference query is read only");
        auto directionalQuery = inferenceQuery;
        directionalQuery["anchor"] = QJsonArray{0, 0, 0};
        directionalQuery["pointer"] = QJsonArray{700, 403};
        directionalQuery["reference"] = QJsonObject{{"body", "1"}, {"edge", "1"}};
        auto directions = executeQuery(source, directionalQuery)["directions"].toArray();
        check(directions.size() >= 2, "Directional query includes axis and edge reference");
        check(encodeDocument(source) == beforeQueries, "Directional query remains read only");
        directionalQuery["reference"] = QJsonObject{{"body", "1"}, {"edge", "9999"}};
        rejects([&] { executeQuery(source, directionalQuery); });
        directionalQuery.remove("anchor");
        rejects([&] { executeQuery(source, directionalQuery); });
        auto invalidInference = inferenceQuery;
        invalidInference["radius"] = 100;
        rejects([&] { executeQuery(source, invalidInference); });
        rejects(
            [&] { executeQuery(source, {{"query", "document.describe"}, {"mutation", true}}); });
        Document guideInference;
        guideInference.addGuide(0, guideLine({}, {1, 0, 0}));
        auto guideQuery = inferenceQuery;
        guideQuery["anchor"] = QJsonArray{0, 0, 0};
        guideQuery["pointer"] = QJsonArray{700, 403};
        guideQuery["reference"] = QJsonObject{{"body", "1"}, {"guide", "1"}};
        auto guideResults = executeQuery(guideInference, guideQuery);
        check(guideResults["candidates"].toArray()[0].toObject()["entityType"] == "guide" &&
                  guideResults["directions"].toArray().size() == 2,
              "Guide query exposes acquisition and typed parallel direction");
        guideQuery["includeGuides"] = false;
        rejects([&] { executeQuery(guideInference, guideQuery); });
        guideQuery.remove("reference");
        check(executeQuery(guideInference, guideQuery)["candidates"].toArray().empty(),
              "CLI hidden guides do not acquire");
        guideQuery["includeGuides"] = "false";
        rejects([&] { executeQuery(guideInference, guideQuery); });
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
        QJsonObject circle{{"command", "geometry.circle"},
                           {"body", "1"},
                           {"space", "world"},
                           {"center", QJsonArray{9, 11, 7}},
                           {"normal", QJsonArray{0, 0, 1}},
                           {"xAxis", QJsonArray{1, 0, 0}},
                           {"radius", 1},
                           {"segments", 24}};
        const auto circleRequest = worldRequest(circle);
        const auto circlePreview = previewBatch(transformedDrawing, circleRequest);
        const auto circleResult = executeBatch(transformedDrawing, circleRequest);
        check(circlePreview["changes"] == circleResult["changes"],
              "Curve preview and commit agree");
        auto curveBody = transformedDrawing.bodies().at(1);
        check(curveBody->curves.size() == 1, "World circle creates one analytic record");
        const auto &curve = curveBody->curves.begin()->second;
        const auto transform = transformedDrawing.worldTransform(1);
        for (unsigned i = 0; i <= 24; ++i)
            check(std::abs(length(transform.point(curve.point(i * 2 * std::acos(-1) / 24)) -
                                  Vec3{9, 11, 7}) -
                           1) < tolerance,
                  "Affine local frame retains world circle radius under nonuniform transform");
        const auto queried =
            executeQuery(transformedDrawing, {{"query", "geometry.inspect"}, {"body", "1"}});
        check(queried["curves"].toArray().size() == 1,
              "Inspection publishes curve parameters and edges");
        const auto oldCurveId = curveBody->curves.begin()->first;
        circle["radius"] = 1.5;
        const auto amended = executeAmend(transformedDrawing, transformedDrawing.amendmentStamp(),
                                          worldRequest(circle));
        check(transformedDrawing.bodies().at(1)->curves.size() == 1 &&
                  transformedDrawing.bodies().at(1)->curves.begin()->first > oldCurveId,
              "Curve amendment keeps one record and reserves retired IDs");
        check(amended["changes"]
                  .toObject()["1"]
                  .toObject()["curves"]
                  .toObject()["deleted"]
                  .toArray()
                  .contains(QString::number(oldCurveId)),
              "Amendment reports retired curve identity");
        check(encodeDocument(decodeContainer(encodeContainer(transformedDrawing))) ==
                  encodeDocument(transformedDrawing),
              "Transformed analytic curve persists exactly");
        transformedDrawing.undo();
        check(transformedDrawing.bodies().at(1)->curves.empty(),
              "Amended curve remains one undo item");
        Document guided;
        guided.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        guided.transform(1, Transform::translation({2, 3, 4}) * Transform::scaling({-2, 3, 1}));
        auto guideRequest = [&](QJsonArray commands) {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(guided.identity())},
                               {"expectedRevision", QString::number(guided.revision())},
                               {"commands", commands}};
        };
        QJsonObject worldGuide{{"command", "guide.line"},
                               {"body", "1"},
                               {"space", "world"},
                               {"origin", QJsonArray{2, 3, 4}},
                               {"direction", QJsonArray{1, 0, 0}}};
        const auto beforeGuide = encodeDocument(guided);
        auto guidePreview = previewBatch(guided, guideRequest({worldGuide}));
        check(encodeDocument(guided) == beforeGuide, "Guide preview is read only");
        auto guideResult = executeBatch(guided, guideRequest({worldGuide}));
        check(guidePreview["changes"] == guideResult["changes"], "Guide preview and commit agree");
        const auto guideId = guided.bodies().at(1)->guides.begin()->first;
        QJsonObject offset{{"command", "guide.offset"},
                           {"body", "1"},
                           {"space", "world"},
                           {"guide", QString::number(guideId)},
                           {"normal", QJsonArray{0, 0, 1}},
                           {"distance", .9}};
        executeBatch(guided, guideRequest({offset}));
        const auto shifted = guided.bodies().at(1)->guides.rbegin()->second;
        check(length(guided.worldTransform(1).point(shifted.origin) - Vec3{2, 3.9, 4}) < tolerance,
              "World offset remains 0.9 m under mirrored nonuniform context");
        const auto inspection =
            executeQuery(guided, {{"query", "geometry.inspect"}, {"body", "1"}});
        check(inspection["guides"].toArray().size() == 2 &&
                  guided.bodies().at(1)->surface.faces.size() == 1,
              "Guide query remains distinct from faces");
        const auto measureBaseline = encodeDocument(guided);
        const auto distance = executeQuery(guided, {{"query", "geometry.measure_distance"},
                                                    {"start", QJsonArray{2, 3, 4}},
                                                    {"end", QJsonArray{2, 3.9, 4}}});
        check(std::abs(distance["distance"].toDouble() - .9) < tolerance &&
                  distance["units"] == "m",
              "Read-only tape measurement query");
        const auto angle = executeQuery(guided, {{"query", "geometry.measure_angle"},
                                                 {"origin", QJsonArray{0, 0, 0}},
                                                 {"first", QJsonArray{1, 0, 0}},
                                                 {"second", QJsonArray{0, -1, 0}},
                                                 {"normal", QJsonArray{0, 0, 1}}});
        check(std::abs(angle["angle"].toDouble() + std::acos(-1) * .5) < tolerance &&
                  encodeDocument(guided) == measureBaseline,
              "Signed protractor query never mutates");
        auto badGuide = worldGuide;
        badGuide["direction"] = QJsonArray{0, 0, 0};
        rejects([&] { executeBatch(guided, guideRequest({worldGuide, badGuide})); });
        check(encodeDocument(guided) == measureBaseline,
              "Invalid guide batch rolls back atomically");
        badGuide = worldGuide;
        badGuide["space"] = "screen";
        rejects([&] { executeBatch(guided, guideRequest({badGuide})); });
        Document newFaceDoc = source;
        newFaceDoc.extrude(1, 5, 2);
        Id newFaceCap = 0;
        for (const auto &[id, face] : newFaceDoc.bodies().at(1)->surface.faces)
            if (newFaceDoc.bodies().at(1)->surface.normal(id).z > .99)
                newFaceCap = id;
        const auto newFaceBaseline = encodeDocument(newFaceDoc);
        QJsonObject newFaceCommand{{"command", "geometry.push_pull"},
                                   {"body", "1"},
                                   {"face", QString::number(newFaceCap)},
                                   {"distance", 1},
                                   {"newFace", 1}};
        auto newFaceRequest = [&] {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(newFaceDoc.identity())},
                               {"expectedRevision", QString::number(newFaceDoc.revision())},
                               {"commands", QJsonArray{newFaceCommand}}};
        };
        rejects([&] { executeBatch(newFaceDoc, newFaceRequest()); });
        check(encodeDocument(newFaceDoc) == newFaceBaseline, "New-face flag requires a boolean");
        newFaceCommand["newFace"] = true;
        const auto oldCapLoop = newFaceDoc.bodies().at(1)->surface.faces.at(newFaceCap);
        executeBatch(newFaceDoc, newFaceRequest());
        check(newFaceDoc.bodies().at(1)->surface.faces.at(newFaceCap) == oldCapLoop,
              "Public push/pull new-face mode retains source cap identity");
        Document selected = source;
        selected.addGuide(1, guidePoint({0, 0, 1}));
        const auto selectedGuide = selected.bodies().at(1)->guides.rbegin()->first;
        auto selectedRequest = [&](QJsonArray entities) {
            return QJsonObject{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(selected.identity())},
                {"expectedRevision", QString::number(selected.revision())},
                {"commands", QJsonArray{QJsonObject{{"command", "geometry.erase_selection"},
                                                    {"entities", entities}}}}};
        };
        const QJsonObject selectedFace{{"body", "1"}, {"kind", "face"}, {"entity", "5"}};
        const QJsonObject selectedPoint{
            {"body", "1"}, {"kind", "guide"}, {"entity", QString::number(selectedGuide)}};
        const auto selectionBaseline = encodeDocument(selected);
        for (const auto &bad :
             QJsonArray{QJsonObject{{"body", "1"}, {"kind", "edge"}, {"entity", "01"}},
                        QJsonObject{{"body", "1"}, {"kind", "face"}, {"entity", "999"}},
                        QJsonObject{{"body", "1"}, {"kind", "context"}, {"entity", 0}},
                        QJsonObject{{"body", "1"}, {"kind", "vertex"}, {"entity", "1"}},
                        QJsonObject{
                            {"body", "1"}, {"kind", "face"}, {"entity", "5"}, {"unknown", true}}}) {
            rejects([&] { executeBatch(selected, selectedRequest({selectedFace, bad})); });
            check(encodeDocument(selected) == selectionBaseline,
                  "Invalid nested selection is rejected atomically");
        }
        rejects([&] { executeBatch(selected, selectedRequest({selectedFace, selectedFace})); });
        const auto selectionRevision = selected.revision();
        executeBatch(selected, selectedRequest({selectedFace, selectedPoint}));
        check(selected.revision() == selectionRevision + 1 &&
                  selected.bodies().at(1)->surface.faces.empty() &&
                  selected.bodies().at(1)->guides.empty(),
              "Public typed deletion commits face and guide together");
        selected.undo();
        check(selected.bodies().at(1)->surface.faces.contains(5) &&
                  selected.bodies().at(1)->guides.contains(selectedGuide),
              "One undo restores a public mixed deletion");
        QJsonArray matrix;
        for (auto value : Transform::translation({2, 0, 0}).m)
            matrix.append(value);
        Document transformedSelection = source;
        const auto transformBaseline = encodeDocument(transformedSelection);
        QJsonObject transformCommand{
            {"command", "geometry.transform_selection"},
            {"matrix", matrix},
            {"entities", QJsonArray{QJsonObject{{"body", "1"}, {"kind", "face"}, {"entity", "5"}}}},
            {"copy", true}};
        auto transformRequest = [&] {
            return QJsonObject{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(transformedSelection.identity())},
                {"expectedRevision", QString::number(transformedSelection.revision())},
                {"commands", QJsonArray{transformCommand}}};
        };
        const auto copyPreview = previewBatch(transformedSelection, transformRequest());
        check(encodeDocument(transformedSelection) == transformBaseline &&
                  copyPreview["copies"].toArray().size() == 1,
              "Scoped copy preview reports new identities without editing the source");
        const auto goodTransform = transformCommand;
        for (auto bad :
             QJsonArray{QJsonObject{{"copy", 1}}, QJsonObject{{"space", "screen"}},
                        QJsonObject{{"matrix", QJsonArray{1, 2, 3}}},
                        QJsonObject{{"entities",
                                     QJsonArray{QJsonObject{
                                         {"body", "1"}, {"kind", "vertex"}, {"entity", "01"}}}}},
                        QJsonObject{{"entities", QJsonArray{QJsonObject{{"body", "1"},
                                                                        {"kind", "face"},
                                                                        {"entity", "5"},
                                                                        {"unknown", 0}}}}}}) {
            transformCommand = goodTransform;
            const auto fields = bad.toObject();
            for (auto it = fields.begin(); it != fields.end(); ++it)
                transformCommand[it.key()] = it.value();
            rejects([&] { executeBatch(transformedSelection, transformRequest()); });
            check(encodeDocument(transformedSelection) == transformBaseline,
                  "Malformed transform rejects atomically");
        }
        transformCommand = goodTransform;
        const auto copyResult = executeBatch(transformedSelection, transformRequest());
        const auto copyIds = copyResult["copies"].toArray()[0].toObject();
        const auto copiedFace = copyIds["faces"].toObject()["5"].toString().toULongLong();
        check(copyIds["sourceBody"] == "1" && copyIds["body"] == "1" && copiedFace > 5 &&
                  transformedSelection.bodies().at(1)->surface.faces.contains(copiedFace) &&
                  transformedSelection.bodies().size() == 1,
              "Public raw copy returns new subentity mappings inside the same context");
        transformedSelection.undo();
        check(transformedSelection.bodies().at(1)->surface.faces.size() == 1,
              "One undo removes a scoped copy");
        transformedSelection.addWire(0, {0, 0, 0}, {1, 0, 0});
        const auto child = transformedSelection.bodies().rbegin()->first;
        transformedSelection.transform(child, Transform::translation({0, 3, 0}), 1);
        transformCommand["copy"] = false;
        transformCommand["entities"] =
            QJsonArray{QJsonObject{{"body", "1"}, {"kind", "context"}, {"entity", "0"}}};
        const auto hierarchyPreview = previewBatch(transformedSelection, transformRequest());
        const auto childGeometry =
            hierarchyPreview["geometry"].toObject()[QString::number(child)].toObject();
        check(!childGeometry.isEmpty() && childGeometry["worldTransform"].toArray()[12] == 2 &&
                  childGeometry["worldTransform"].toArray()[13] == 3,
              "Parent-transform preview includes unchanged child meshes at their new world frame");
        Document arrayDoc = source;
        QJsonObject arrayCommand{{"command", "geometry.array_selection"},
                                 {"entities", goodTransform["entities"]},
                                 {"mode", "linear"},
                                 {"delta", QJsonArray{2, 0, 0}},
                                 {"count", 3}};
        auto arrayRequest = [&] {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(arrayDoc.identity())},
                               {"expectedRevision", QString::number(arrayDoc.revision())},
                               {"commands", QJsonArray{arrayCommand}}};
        };
        const auto arrayBaseline = encodeDocument(arrayDoc);
        const auto arrayPreview = previewBatch(arrayDoc, arrayRequest());
        check(encodeDocument(arrayDoc) == arrayBaseline &&
                  arrayPreview["copies"].toArray().size() == 3 &&
                  arrayPreview["copies"].toArray()[2].toObject()["instance"] == 3,
              "Array preview is private and labels ordered copy instances");
        const auto validArray = arrayCommand;
        for (auto patch : QJsonArray{QJsonObject{{"count", 0}}, QJsonObject{{"count", 101}},
                                     QJsonObject{{"count", 1.5}}, QJsonObject{{"count", "3"}},
                                     QJsonObject{{"divide", 1}}, QJsonObject{{"mode", "unknown"}},
                                     QJsonObject{{"axis", QJsonArray{0, 0, 1}}}}) {
            arrayCommand = validArray;
            const auto object = patch.toObject();
            for (auto it = object.begin(); it != object.end(); ++it)
                arrayCommand[it.key()] = it.value();
            rejects([&] { executeBatch(arrayDoc, arrayRequest()); });
            check(encodeDocument(arrayDoc) == arrayBaseline,
                  "Malformed array preserves document and IDs");
        }
        arrayCommand = validArray;
        executeBatch(arrayDoc, arrayRequest());
        check(arrayDoc.bodies().at(1)->surface.faces.size() == 4,
              "Array command creates exact copy count");
        arrayDoc.undo();
        check(arrayDoc.bodies().at(1)->surface.faces.size() == 1, "Public array is one undo item");
        Document exploded = source;
        exploded.addFace({{{1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}}});
        exploded.paint(2, {.1f, .2f, .9f});
        createGroup(exploded, {2});
        auto explodeRequest = [&](QJsonObject command) {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(exploded.identity())},
                               {"expectedRevision", QString::number(exploded.revision())},
                               {"commands", QJsonArray{command}}};
        };
        const auto intact = encodeDocument(exploded);
        rejects([&] {
            executeBatch(
                exploded,
                explodeRequest({{"command", "group.explode"}, {"body", "3"}, {"merge", 1}}));
        });
        rejects([&] {
            executeBatch(exploded, explodeRequest({{"command", "geometry.merge_context"},
                                                   {"context", "0"},
                                                   {"members", QJsonArray{"1", "2"}}}));
        });
        check(encodeDocument(exploded) == intact, "Invalid merge requests preserve the document");
        const auto explosion =
            executeBatch(exploded, explodeRequest({{"command", "group.explode"}, {"body", "3"}}));
        check(exploded.bodies().size() == 1 &&
                  exploded.bodies().at(1)->surface.vertices.size() == 6 &&
                  exploded.bodies().at(1)->faceColors.size() == 1 &&
                  explosion["transfers"].toArray().size() == 2,
              "Public explode welds colored geometry and returns typed transfers");
        exploded.undo();
        check(exploded.bodies().size() == 3 && exploded.bodies().at(2)->parent == 3,
              "Public explode plus consolidation is one undo");
        executeBatch(
            exploded,
            explodeRequest({{"command", "group.explode"}, {"body", "3"}, {"merge", false}}));
        check(exploded.bodies().size() == 2 && exploded.bodies().at(1)->surface.faces.size() == 1,
              "Boundary-only explode is explicitly available");
        Document componentCopy = source;
        createGroup(componentCopy, {1});
        auto definition = std::make_shared<ComponentDefinition>();
        definition->id = 1;
        definition->root = 2;
        definition->nextMemberId = 3;
        definition->members = componentCopy.bodies();
        auto binding = std::make_shared<ComponentInstance>();
        binding->definition = 1;
        binding->members = {{1, 1}, {2, 2}};
        Edit bind{"Bind public component fixture", {}};
        bind.definitions.push_back({1, nullptr, definition});
        bind.instances.push_back({2, nullptr, binding});
        componentCopy.apply(bind, componentCopy.revision());
        const auto description = executeQuery(componentCopy, {{"query", "document.describe"}});
        check(description["definitions"].toArray().size() == 1 &&
                  description["instances"].toArray()[0].toObject()["definition"] == "1",
              "Document query exposes canonical definitions and instance bindings");
        const QJsonObject componentArray{
            {"apiVersion", 1},
            {"documentId", QString::fromStdString(componentCopy.identity())},
            {"expectedRevision", QString::number(componentCopy.revision())},
            {"commands",
             QJsonArray{QJsonObject{
                 {"command", "geometry.array_selection"},
                 {"mode", "linear"},
                 {"count", 2},
                 {"delta", QJsonArray{2, 0, 0}},
                 {"entities",
                  QJsonArray{QJsonObject{{"body", "2"}, {"kind", "context"}, {"entity", "0"}}}}}}}};
        const auto componentPreview = previewBatch(componentCopy, componentArray);
        check(componentCopy.instances().size() == 1 &&
                  componentPreview["document"].toObject()["instances"].toArray().size() == 3,
              "Component array preview includes bindings without publishing");
        executeBatch(componentCopy, componentArray);
        check(componentCopy.instances().size() == 3 && componentCopy.definitions().size() == 1,
              "Public array publication retains shared component definitions");
        componentCopy.undo();
        check(componentCopy.instances().size() == 1,
              "Public component array undoes bindings and scene records together");
        Document componentWorkflow = source;
        auto componentCommand = [&](const QJsonObject &command) {
            return executeBatch(
                componentWorkflow,
                {{"apiVersion", 1},
                 {"documentId", QString::fromStdString(componentWorkflow.identity())},
                 {"expectedRevision", QString::number(componentWorkflow.revision())},
                 {"commands", QJsonArray{command}}});
        };
        const auto componentCreated =
            componentCommand({{"command", "component.create"}, {"body", "1"}});
        check(componentCreated["createdDefinitions"].toArray() == QJsonArray{"1"} &&
                  componentCreated["componentOperations"].toArray()[0].toObject()["instance"] ==
                      "1",
              "Component creation reports separate definition and placement identities");
        componentCommand(
            {{"command", "component.instance"}, {"definition", "1"}, {"matrix", matrix}});
        auto shared = QJsonObject{
            {"command", "component.edit"},
            {"definition", "1"},
            {"commands",
             QJsonArray{QJsonObject{
                 {"command", "geometry.insert_edges"},
                 {"body", "2"},
                 {"origin", QJsonArray{0, 0, 0}},
                 {"normal", QJsonArray{0, 0, 1}},
                 {"edges", QJsonArray{QJsonArray{QJsonArray{.5, 0, 0}, QJsonArray{.5, 1, 0}}}}}}}};
        const auto beforeShared = encodeDocument(componentWorkflow);
        const QJsonObject sharedBatch{
            {"apiVersion", 1},
            {"documentId", QString::fromStdString(componentWorkflow.identity())},
            {"expectedRevision", QString::number(componentWorkflow.revision())},
            {"commands", QJsonArray{shared}}};
        const auto sharedPreview = previewBatch(componentWorkflow, sharedBatch);
        check(encodeDocument(componentWorkflow) == beforeShared,
              "Shared component preview preserves document, identity and revision");
        const auto sharedResult = componentCommand(shared);
        check(sharedResult["document"] == sharedPreview["document"] &&
                  sharedResult["changes"] == sharedPreview["changes"],
              "Shared component preview and commit have identical instances and lineage");
        check(componentWorkflow.bodies().at(2)->surface.faces.size() == 2 &&
                  componentWorkflow.bodies().at(4)->surface.faces.size() == 2 &&
                  sharedResult["changes"]
                          .toObject()["4"]
                          .toObject()["faces"]
                          .toObject()["descendants"]
                          .toObject()["5"]
                          .toArray()
                          .size() == 2,
              "Explicit shared command scope updates every instance with typed lineage");
        const auto sharedSnapshot = encodeDocument(componentWorkflow);
        rejects([&] {
            componentCommand(
                {{"command", "geometry.translate"}, {"body", "2"}, {"delta", QJsonArray{0, 0, 1}}});
        });
        auto recursive = shared;
        recursive["commands"] = QJsonArray{shared};
        rejects([&] { componentCommand(recursive); });
        check(encodeDocument(componentWorkflow) == sharedSnapshot,
              "Unscoped and recursively scoped edits reject atomically");
        rejects([&] {
            componentCommand({{"command", "component.instance"},
                              {"definition", "1"},
                              {"matrix", QJsonArray{1, 0}}});
        });
        rejects([&] {
            componentCommand(
                {{"command", "component.edit"},
                 {"definition", "1"},
                 {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                     {"body", "4"},
                                                     {"delta", QJsonArray{0, 0, 1}}}}}});
        });
        componentCommand({{"command", "scene.state"}, {"body", "3"}, {"locked", true}});
        const auto lockedShared = encodeDocument(componentWorkflow);
        rejects([&] {
            componentCommand(
                {{"command", "component.edit"},
                 {"definition", "1"},
                 {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                     {"body", "2"},
                                                     {"delta", QJsonArray{0, 0, 1}}}}}});
        });
        check(encodeDocument(componentWorkflow) == lockedShared,
              "A locked peer rejects public shared commands without changing any instance");
        componentWorkflow.undo();
        componentCommand({{"command", "component.make_unique"}, {"body", "3"}});
        componentCommand({{"command", "component.edit"},
                          {"definition", "2"},
                          {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                              {"body", "2"},
                                                              {"delta", QJsonArray{0, 0, 1}}}}}});
        check(componentWorkflow.worldTransform(4).point({}).z == 1 &&
                  componentWorkflow.worldTransform(2).point({}).z == 0,
              "Public make-unique isolates the selected instance's subsequent shared edit");
        const auto inspected =
            executeQuery(componentWorkflow, {{"query", "component.inspect"}, {"definition", "2"}});
        check(inspected["scope"] == "definition" && inspected["members"].toArray().size() == 2 &&
                  inspected["instances"].toArray() == QJsonArray{"3"},
              "Definition inspection exposes canonical member IDs and affected instances");
        componentCommand({{"command", "group.explode"}, {"body", "3"}});
        check(!componentWorkflow.instances().contains(3) &&
                  componentWorkflow.instances().contains(1),
              "Explode removes only the selected component binding");
        componentWorkflow.undo();
        componentCommand({{"command", "geometry.erase_selection"},
                          {"entities", QJsonArray{QJsonObject{
                                           {"body", "3"}, {"kind", "context"}, {"entity", "0"}}}}});
        check(componentWorkflow.instances().size() == 1 && !componentWorkflow.bodies().contains(4),
              "Typed deletion removes component members and binding in one batch");
        componentWorkflow.undo();
        check(componentWorkflow.instances().size() == 2 && componentWorkflow.bodies().contains(4),
              "Component deletion undo restores sharing and placed records");
        const auto componentRoundTrip = decodeContainer(encodeContainer(componentWorkflow));
        check(encodeDocument(componentRoundTrip) == encodeDocument(componentWorkflow),
              "Public shared edits and unique definitions survive exact container round trips");
        Document sharedAmend = source;
        createComponent(sharedAmend, 1);
        placeComponent(sharedAmend, 1, Transform::translation({3, 0, 0}));
        const auto amendBaseline = sharedAmend.bodies();
        auto sharedSweep = [&](double distance) {
            return QJsonObject{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(sharedAmend.identity())},
                {"expectedRevision", QString::number(sharedAmend.revision())},
                {"commands",
                 QJsonArray{QJsonObject{
                     {"command", "component.edit"},
                     {"definition", "1"},
                     {"commands", QJsonArray{QJsonObject{{"command", "geometry.push_pull"},
                                                         {"body", "2"},
                                                         {"face", "5"},
                                                         {"distance", distance}}}}}}}};
        };
        executeBatch(sharedAmend, sharedSweep(.2));
        const auto amendment = sharedAmend.amendmentStamp();
        previewAmend(sharedAmend, amendment, sharedSweep(.4));
        executeAmend(sharedAmend, amendment, sharedSweep(.4));
        for (auto body : {2, 4}) {
            double height = 0;
            for (const auto &[vertex, point] : sharedAmend.bodies().at(body)->surface.vertices)
                height = std::max(height, point.z);
            check(std::abs(height - .4) < tolerance,
                  "Shared numeric amendment replaces geometry in every instance");
        }
        sharedAmend.undo();
        for (const auto &[id, body] : amendBaseline)
            check(sharedAmend.bodies().at(id)->surface.faces == body->surface.faces &&
                      sharedAmend.bodies().at(id)->surface.vertices == body->surface.vertices,
                  "Shared numeric amendment retains one-undo behavior");
        Document info;
        const auto infoBody = info.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto infoStamp = info.saveStamp();
        const auto openInfo = executeQuery(info, {{"query", "entity.inspect"}, {"body", "1"}});
        check(openInfo["world"].toObject()["volume"].isNull() &&
                  openInfo["solid"].toObject()["status"] == "open_boundary" &&
                  info.isCurrentSnapshot(infoStamp),
              "Open-geometry info is read-only and reports null volume");
        info.extrude(infoBody, 5, 4);
        const auto solidInfo = executeQuery(info, {{"query", "entity.inspect"}, {"body", "1"}});
        check(solidInfo["world"].toObject()["volume"] == 24 &&
                  solidInfo["parent"].toObject()["bounds"].toObject()["dimensions"].toArray() ==
                      QJsonArray{2, 3, 4},
              "Entity query exposes solid volume and explicit parent dimensions");
        const auto infoBytes = encodeDocument(info);
        rejects([&] {
            executeQuery(info, {{"query", "entity.inspect"}, {"body", "1"}, {"kind", "face"}});
        });
        rejects([&] {
            executeQuery(info, {{"query", "entity.inspect"}, {"body", "1"}, {"entity", "5"}});
        });
        rejects([&] {
            executeBatch(
                info, {{"apiVersion", 1},
                       {"documentId", QString::fromStdString(info.identity())},
                       {"expectedRevision", QString::number(info.revision())},
                       {"commands", QJsonArray{QJsonObject{{"command", "entity.position"},
                                                           {"body", "1"},
                                                           {"position", QJsonArray{2, 3, 4}}},
                                               QJsonObject{{"command", "entity.dimensions"},
                                                           {"body", "1"},
                                                           {"dimensions", QJsonArray{2, 0, 4}}}}}});
        });
        check(encodeDocument(info) == infoBytes,
              "Invalid dimension batch rolls back its earlier placement change");
        const auto swatch = createMaterial(info, "Query glass", {.2f, .4f, .6f}, .5f);
        assignMaterial(info, infoBody, Id{5}, swatch, false, true);
        const auto sampleStamp = info.saveStamp();
        const auto sample =
            executeQuery(info, {{"query", "material.sample"}, {"body", "1"}, {"face", "5"}});
        check(sample["front"].toObject()["material"] == "0" &&
                  sample["back"].toObject()["material"] == QString::number(swatch) &&
                  sample["back"].toObject()["opacity"] == .5 && info.isCurrentSnapshot(sampleStamp),
              "Material sampling is read-only and distinguishes sides");
        check(executeQuery(info, {{"query", "materials.describe"}})["materials"].toArray().size() ==
                  1,
              "Swatch query exposes in-model material table");
        const auto beforeMaterialFailure = encodeDocument(info);
        rejects([&] {
            executeBatch(info,
                         {{"apiVersion", 1},
                          {"documentId", QString::fromStdString(info.identity())},
                          {"expectedRevision", QString::number(info.revision())},
                          {"commands", QJsonArray{QJsonObject{{"command", "material.edit"},
                                                              {"material", QString::number(swatch)},
                                                              {"opacity", .8}},
                                                  QJsonObject{{"command", "material.assign"},
                                                              {"body", "1"},
                                                              {"material", "999"}}}}});
        });
        check(encodeDocument(info) == beforeMaterialFailure,
              "Rejected assignment rolls back earlier global swatch edits");
        Document assetDoc;
        auto assetBatch = [&](QJsonArray commands) {
            return executeBatch(assetDoc,
                                {{"apiVersion", 1},
                                 {"documentId", QString::fromStdString(assetDoc.identity())},
                                 {"expectedRevision", QString::number(assetDoc.revision())},
                                 {"commands", commands}});
        };
        const auto assetCreated = assetBatch({QJsonObject{{"command", "asset.missing"},
                                                          {"name", "Lost texture"},
                                                          {"mediaType", "image/png"}},
                                              QJsonObject{{"command", "material.create"},
                                                          {"name", "Textured"},
                                                          {"color", QJsonArray{1, 1, 1}},
                                                          {"asset", "1"}}});
        check(assetCreated["createdAssets"].toArray() == QJsonArray{"1"} &&
                  executeQuery(assetDoc, {{"query", "materials.describe"}})["materials"]
                          .toArray()[0]
                          .toObject()["assetStatus"] == "missing",
              "Public material query explicitly reports missing referenced assets");
        const auto assetStamp = assetDoc.saveStamp();
        const auto assetQuery = executeQuery(assetDoc, {{"query", "assets.describe"}});
        check(assetQuery["assets"].toArray()[0].toObject()["missing"] == true &&
                  assetDoc.isCurrentSnapshot(assetStamp),
              "Asset manifest query is read-only");
        assetBatch({QJsonObject{{"command", "asset.replace"}, {"asset", "1"}, {"data", "AAEC"}}});
        check(executeQuery(assetDoc, {{"query", "materials.describe"}})["materials"]
                      .toArray()[0]
                      .toObject()["assetStatus"] == "present",
              "Resolving bytes updates material resource status without rebinding identity");
        const auto beforeAssetFailure = encodeDocument(assetDoc);
        rejects([&] {
            assetBatch(
                {QJsonObject{
                     {"command", "asset.replace"}, {"asset", "1"}, {"data", QJsonValue::Null}},
                 QJsonObject{{"command", "material.edit"}, {"material", "1"}, {"asset", "999"}}});
        });
        check(encodeDocument(assetDoc) == beforeAssetFailure,
              "Invalid binding rolls back a preceding asset replacement");
        assetDoc.undo();
        check(!assetDoc.assets().at(1)->payload, "Resolution has one undo step");
        assetDoc.undo();
        check(assetDoc.assets().empty() && assetDoc.materials().empty(),
              "Combined asset and material creation undoes atomically");
        QJsonArray cases{
            QJsonObject{{"command", "document.units"}, {"units", "mm"}},
            QJsonObject{{"command", "asset.import"},
                        {"name", "Data"},
                        {"mediaType", "application/octet-stream"},
                        {"data", "AAEC"}},
            QJsonObject{
                {"command", "asset.missing"}, {"name", "Texture"}, {"mediaType", "image/png"}},
            QJsonObject{{"command", "asset.replace"}, {"asset", "1"}, {"data", "AwQF"}},
            QJsonObject{{"command", "asset.delete"}, {"asset", "1"}},
            QJsonObject{{"command", "material.create"},
                        {"name", "Glass"},
                        {"color", QJsonArray{.2, .4, .6}},
                        {"opacity", .5}},
            QJsonObject{{"command", "material.edit"}, {"material", "1"}, {"opacity", .25}},
            QJsonObject{{"command", "material.delete"}, {"material", "1"}},
            QJsonObject{{"command", "material.assign"},
                        {"body", "1"},
                        {"face", "5"},
                        {"material", "1"},
                        {"side", "back"}},
            QJsonObject{
                {"command", "entity.position"}, {"body", "1"}, {"position", QJsonArray{2, 3, 4}}},
            QJsonObject{{"command", "entity.dimensions"},
                        {"body", "1"},
                        {"dimensions", QJsonArray{8, 6, 0}}},
            QJsonObject{
                {"command", "entity.properties"},
                {"body", "1"},
                {"values", QJsonObject{{"role", "wall"}, {"height", 2.4}, {"structural", true}}}},
            QJsonObject{{"command", "tag.create"}, {"name", "Architecture"}, {"folder", true}},
            QJsonObject{
                {"command", "tag.edit"}, {"tag", "1"}, {"name", "Walls"}, {"visible", false}},
            QJsonObject{{"command", "tag.delete"}, {"tag", "1"}},
            QJsonObject{{"command", "tag.assign"}, {"body", "1"}, {"tag", "1"}},
            QJsonObject{{"command", "scene.rename"}, {"body", "1"}, {"name", "Wall"}},
            QJsonObject{{"command", "component.selection"},
                        {"context", "0"},
                        {"entities", QJsonArray{QJsonObject{
                                         {"body", "1"}, {"kind", "face"}, {"entity", "5"}}}}},
            QJsonObject{
                {"command", "component.glue"}, {"definition", "1"}, {"glue", QJsonValue::Null}},
            QJsonObject{{"command", "component.attach"},
                        {"body", "1"},
                        {"host", "1"},
                        {"face", "5"},
                        {"anchor", QJsonArray{0, 0, 0}}},
            QJsonObject{{"command", "component.bind"},
                        {"body", "1"},
                        {"host", "1"},
                        {"face", "5"},
                        {"inset", 0}},
            QJsonObject{{"command", "component.detach"}, {"body", "1"}},
            QJsonObject{{"command", "component.bake_host"}, {"body", "1"}},
            QJsonObject{{"command", "component.create"}, {"body", "1"}, {"name", "Panel"}},
            QJsonObject{{"command", "component.instance"}, {"definition", "1"}, {"matrix", matrix}},
            QJsonObject{{"command", "component.make_unique"}, {"body", "1"}},
            QJsonObject{{"command", "component.replace"}, {"body", "1"}, {"definition", "2"}},
            QJsonObject{{"command", "component.axes"}, {"definition", "1"}, {"matrix", matrix}},
            QJsonObject{
                {"command", "component.edit"},
                {"definition", "1"},
                {"commands",
                 QJsonArray{QJsonObject{{"command", "geometry.insert_edges"},
                                        {"body", "2"},
                                        {"origin", QJsonArray{0, 0, 0}},
                                        {"normal", QJsonArray{0, 0, 1}},
                                        {"edges", QJsonArray{QJsonArray{QJsonArray{.5, 0, 0},
                                                                        QJsonArray{.5, 1, 0}}}}}}}},
            QJsonObject{{"command", "geometry.merge_context"}, {"context", "0"}},
            QJsonObject{{"command", "group.selection"},
                        {"context", "0"},
                        {"entities", QJsonArray{QJsonObject{
                                         {"body", "1"}, {"kind", "face"}, {"entity", "5"}}}}},
            QJsonObject{
                {"command", "group.create"}, {"members", QJsonArray{"1"}}, {"name", "Assembly"}},
            QJsonObject{{"command", "group.explode"}, {"body", "2"}},
            QJsonObject{{"command", "scene.reparent"}, {"body", "1"}, {"parent", "0"}},
            QJsonObject{
                {"command", "scene.state"}, {"body", "1"}, {"locked", true}, {"hidden", true}},
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
            QJsonObject{{"command", "geometry.circle"},
                        {"body", "0"},
                        {"center", QJsonArray{0, 0, 0}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"xAxis", QJsonArray{1, 0, 0}},
                        {"radius", 2},
                        {"segments", 24}},
            QJsonObject{{"command", "geometry.arc_center"},
                        {"body", "0"},
                        {"center", QJsonArray{0, 0, 0}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"xAxis", QJsonArray{1, 0, 0}},
                        {"radius", 2},
                        {"startAngle", 0},
                        {"sweepAngle", 1.5},
                        {"segments", 24}},
            QJsonObject{{"command", "geometry.arc_two_points"},
                        {"body", "0"},
                        {"start", QJsonArray{-1, 0, 0}},
                        {"end", QJsonArray{1, 0, 0}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"bulge", 0.5},
                        {"segments", 24}},
            QJsonObject{{"command", "geometry.arc_three_points"},
                        {"body", "0"},
                        {"start", QJsonArray{1, 0, 0}},
                        {"through", QJsonArray{0, 1, 0}},
                        {"end", QJsonArray{-1, 0, 0}},
                        {"segments", 24}},
            QJsonObject{{"command", "geometry.pie"},
                        {"body", "0"},
                        {"center", QJsonArray{0, 0, 0}},
                        {"normal", QJsonArray{0, 0, 1}},
                        {"xAxis", QJsonArray{1, 0, 0}},
                        {"radius", 2},
                        {"startAngle", 0},
                        {"sweepAngle", 1.5},
                        {"segments", 24}},
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
                {"command", "geometry.offset"}, {"body", "1"}, {"face", "5"}, {"distance", -.1}},
            QJsonObject{{"command", "geometry.edge_appearance"},
                        {"context", "0"},
                        {"entities", QJsonArray{QJsonObject{{"body", "1"}, {"edge", "1"}}}},
                        {"smooth", true}},
            QJsonObject{{"command", "geometry.reverse_faces"},
                        {"context", "0"},
                        {"entities", QJsonArray{QJsonObject{{"body", "1"}, {"face", "5"}}}}},
            QJsonObject{{"command", "geometry.orient_faces"},
                        {"context", "0"},
                        {"body", "1"},
                        {"face", "5"}},
            QJsonObject{{"command", "geometry.boolean"},
                        {"body", "1"},
                        {"tool", "2"},
                        {"context", "0"},
                        {"operation", "union"},
                        {"keepOperands", true}},
            QJsonObject{{"command", "geometry.trim"},
                        {"body", "1"},
                        {"tool", "2"},
                        {"context", "0"},
                        {"keepTarget", true}},
            QJsonObject{{"command", "geometry.split"},
                        {"body", "1"},
                        {"tool", "2"},
                        {"context", "0"},
                        {"keepOperands", true}},
            QJsonObject{{"command", "geometry.outer_shell"},
                        {"body", "1"},
                        {"tool", "2"},
                        {"context", "0"},
                        {"keepOperands", true}},
            QJsonObject{{"command", "geometry.intersect"},
                        {"context", "0"},
                        {"mode", "context"},
                        {"entities", QJsonArray{QJsonObject{{"body", "1"}, {"face", "5"}}}}},
            QJsonObject{{"command", "geometry.sweep"},
                        {"body", "1"},
                        {"face", "5"},
                        {"path", QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{0, 0, 2}}}},
            QJsonObject{
                {"command", "geometry.push_pull"}, {"body", "1"}, {"face", "5"}, {"distance", 2}},
            QJsonObject{{"command", "geometry.extrude_isolated"},
                        {"body", "1"},
                        {"face", "5"},
                        {"distance", 2}},
            QJsonObject{{"command", "geometry.array_selection"},
                        {"entities",
                         QJsonArray{QJsonObject{{"body", "1"}, {"kind", "face"}, {"entity", "5"}}}},
                        {"mode", "linear"},
                        {"delta", QJsonArray{2, 0, 0}},
                        {"count", 3}},
            QJsonObject{{"command", "geometry.transform_selection"},
                        {"matrix", matrix},
                        {"entities", QJsonArray{QJsonObject{
                                         {"body", "1"}, {"kind", "face"}, {"entity", "5"}}}}},
            QJsonObject{
                {"command", "geometry.translate"}, {"body", "1"}, {"delta", QJsonArray{1, 0, 0}}},
            QJsonObject{{"command", "geometry.erase_selection"},
                        {"entities", QJsonArray{QJsonObject{
                                         {"body", "1"}, {"kind", "face"}, {"entity", "5"}}}}},
            QJsonObject{{"command", "geometry.delete"}, {"body", "1"}},
            QJsonObject{
                {"command", "material.color"}, {"body", "1"}, {"color", QJsonArray{.1, .2, .3}}},
            QJsonObject{{"command", "scene.transform"}, {"body", "1"}, {"matrix", matrix}},
            QJsonObject{
                {"command", "guide.point"}, {"body", "0"}, {"origin", QJsonArray{0, 0, .9}}},
            QJsonObject{{"command", "guide.line"},
                        {"body", "1"},
                        {"origin", QJsonArray{0, 0, .9}},
                        {"direction", QJsonArray{1, 0, 0}}},
            QJsonObject{{"command", "guide.angle"},
                        {"body", "1"},
                        {"origin", QJsonArray{0, 0, 0}},
                        {"normal", QJsonArray{0, -1, 0}},
                        {"xAxis", QJsonArray{1, 0, 0}},
                        {"angle", .5}},
            QJsonObject{{"command", "guide.offset"},
                        {"body", "1"},
                        {"guide", "6"},
                        {"normal", QJsonArray{0, -1, 0}},
                        {"distance", .9}},
            QJsonObject{{"command", "guide.erase"}, {"body", "1"}, {"guide", "6"}},
            QJsonObject{{"command", "guide.clear"}, {"body", "0"}}};
        cases.append(
            QJsonObject{{"command", "component.edit_instance"},
                        {"body", "1"},
                        {"commands", QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                            {"body", "2"},
                                                            {"delta", QJsonArray{.1, 0, 0}}}}}});
        cases.append(QJsonObject{{"command", "assert.measurement"}, {"body", "1"},
                                 {"space", "local"}, {"metric", "area"}, {"expected", 1},
                                 {"tolerance", 1e-7}});
        cases.append(QJsonObject{{"command", "assembly.roof"}});
        cases.append(QJsonObject{{"command", "assembly.stairs"}});
        cases.append(QJsonObject{{"command", "assembly.room"}});
        cases.append(QJsonObject{{"command", "assembly.room.adopt_hosted"}, {"body", "3"}});
        cases.append(QJsonObject{{"command", "assembly.window.resize"},
                                 {"body", "6"},
                                 {"width", 1.4},
                                 {"scope", "instance"}});
        check(commandCatalog().size() == cases.size(),
              "All published commands have executable cases");
        for (auto value : cases) {
            auto command = value.toObject();
            auto descriptor = commandDescription(command["command"].toString());
            check(!descriptor["label"].toString().isEmpty(), "Human command label exists");
            auto schema = descriptor["parameters"].toObject();
            auto request = [&](const Document &doc, QJsonObject item) {
                QJsonArray commands{item};
                if (command["command"] == "assert.measurement")
                    commands.prepend(QJsonObject{{"command", "document.units"}, {"units", "mm"}});
                return QJsonObject{{"apiVersion", 1},
                                   {"documentId", QString::fromStdString(doc.identity())},
                                   {"expectedRevision", QString::number(doc.revision())},
                                   {"commands", commands}};
            };
            Document doc = source;
            if (command["command"].toString().startsWith("assembly.")) {
                doc = Document{};
                if (command["command"] == "assembly.window.resize" ||
                    command["command"] == "assembly.room.adopt_hosted") {
                    const auto room =
                        executeBatch(doc, request(doc, {{"command", "assembly.room"}}));
                    const auto authored = room["recipeOperations"].toArray()[0].toObject();
                    command["body"] = command["command"] == "assembly.room.adopt_hosted"
                                          ? authored["room"]
                                          : authored["windows"].toArray()[0].toObject()["body"];
                }
            }
            if (command["command"].toString().startsWith("asset.") &&
                command["command"] != "asset.import" && command["command"] != "asset.missing")
                createAsset(doc, "Data", "application/octet-stream",
                            std::make_shared<AssetPayload>(std::vector<std::uint8_t>{0, 1, 2}));
            if (command["command"].toString().startsWith("material.") &&
                command["command"] != "material.create" && command["command"] != "material.color")
                createMaterial(doc, "Paint", {.7f, .2f, .3f});
            if (command["command"].toString().startsWith("tag.") &&
                command["command"] != "tag.create")
                createTag(doc, "Tag");
            if (command["command"].toString().startsWith("component.") &&
                command["command"] != "component.create" &&
                command["command"] != "component.selection") {
                createComponent(doc, 1);
                if (command["command"] == "component.replace") {
                    const auto raw = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
                    createComponent(doc, raw);
                }
            }
            if (command["command"] == "component.glue" ||
                command["command"] == "component.attach" ||
                command["command"] == "component.bind" ||
                command["command"] == "component.detach" ||
                command["command"] == "component.bake_host") {
                const auto &definition = *doc.definitions().at(1);
                Id member = 0, face = 0;
                for (const auto &[id, body] : definition.members)
                    if (!body->surface.faces.empty()) {
                        member = id;
                        face = body->surface.faces.begin()->first;
                        break;
                    }
                setComponentGlue(doc, 1, ComponentGlue{member, face, {}, {1, 0, 0}, false});
                const auto host = doc.addFace({{{-5, -5, 0}, {5, -5, 0}, {5, 5, 0}, {-5, 5, 0}}});
                const auto hostFace = doc.bodies().at(host)->surface.faces.begin()->first;
                if (command.contains("host")) {
                    command["host"] = QString::number(host);
                    command["face"] = QString::number(hostFace);
                }
                if (command["command"] == "component.detach" ||
                    command["command"] == "component.bake_host")
                    bindComponentAtCurrentPose(doc, 1, host, hostFace, 0);
                if (command["command"] == "component.bake_host")
                    command["body"] = QString::number(host);
            }
            if (command["command"] == "geometry.orient_faces") {
                doc.extrude(1, 5, 1);
                reverseSelectedFaces(
                    doc,
                    {{1, SelectionKind::Face, doc.bodies().at(1)->surface.faces.rbegin()->first}},
                    0);
            }
            if (command["command"] == "geometry.boolean" || command["command"] == "geometry.trim" ||
                command["command"] == "geometry.split" ||
                command["command"] == "geometry.outer_shell") {
                doc.extrude(1, 5, 1);
                const auto other =
                    doc.addFace({{{.5, 0, 0}, {1.5, 0, 0}, {1.5, 1, 0}, {.5, 1, 0}}});
                doc.extrude(other, 5, 1);
            }
            if (command["command"] == "geometry.intersect")
                doc.addFace({{{.5, 0, -1}, {.5, 1, -1}, {.5, 1, 1}, {.5, 0, 1}}});
            if (command["command"] == "geometry.merge_context")
                doc.addFace({{{1, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}}});
            if (command["command"] == "group.explode" || command["command"] == "scene.reparent")
                createGroup(doc, {1});
            if (command["command"] == "guide.offset" || command["command"] == "guide.erase" ||
                command["command"] == "guide.clear")
                doc.addGuide(1, guideLine({}, {1, 0, 0}));
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
            const auto response = executeBatch(doc, request(doc, command));
            if (command["command"] == "group.selection") {
                const auto transfer = response["transfers"].toArray()[0].toObject();
                check(response["copies"].toArray().empty() && transfer["sourceBody"] == "1" &&
                          transfer["faces"].toObject()["5"] == "5",
                      "Grouping returns typed transfers separately from copies");
            }
            check(doc.revision() == revision + 1, "Registered command commits once");
            doc.undo();
            if (baseline.bodies().empty()) {
                check(doc.bodies().empty(), "Room creation undo restores an empty model");
            } else {
                auto expectedBody = *baseline.bodies().at(1);
                check(doc.bodies().at(1)->surface.nextId >= expectedBody.surface.nextId,
                      "Undo retains surface allocator high-water mark");
                expectedBody.surface.nextId = doc.bodies().at(1)->surface.nextId;
                expectedBody.topology.nextId = doc.bodies().at(1)->topology.nextId;
                check(doc.bodies().size() == baseline.bodies().size() &&
                          *doc.bodies().at(1) == expectedBody,
                      "Registered command undo restores source geometry and metadata");
            }
            check(doc.hostedComponents() == baseline.hostedComponents(),
                  "Registered command undo restores attachment records");
            check(doc.tags() == baseline.tags(), "Registered command undo restores tag records");
            check(doc.materials() == baseline.materials(),
                  "Registered command undo restores material records");
            check(doc.assets() == baseline.assets(),
                  "Registered command undo restores asset records");
        }
        Document groupedBatch = source;
        Document taggedMerge;
        const auto redTag = createTag(taggedMerge, "Red");
        const auto blueTag = createTag(taggedMerge, "Blue");
        for (unsigned i = 0; i < 4; ++i) {
            const auto body = taggedMerge.addFace({{{double(i), 0, 0},
                                                    {double(i + 1), 0, 0},
                                                    {double(i + 1), 1, 0},
                                                    {double(i), 1, 0}}});
            assignTag(taggedMerge, body, i < 2 ? redTag : blueTag);
        }
        const auto taggedBefore = encodeDocument(taggedMerge);
        const auto tagQuery = executeQuery(taggedMerge, {{"query", "tags.describe"}});
        check(tagQuery["tags"].toArray().size() == 2 && encodeDocument(taggedMerge) == taggedBefore,
              "Tag inspection is read only");
        auto tagBatch = [&](QJsonArray commands) {
            return QJsonObject{{"apiVersion", 1},
                               {"documentId", QString::fromStdString(taggedMerge.identity())},
                               {"expectedRevision", QString::number(taggedMerge.revision())},
                               {"commands", commands}};
        };
        rejects([&] {
            executeBatch(
                taggedMerge,
                tagBatch({QJsonObject{{"command", "tag.edit"}, {"tag", "1"}, {"name", "Changed"}},
                          QJsonObject{{"command", "tag.assign"}, {"body", "1"}, {"tag", "999"}}}));
        });
        check(encodeDocument(taggedMerge) == taggedBefore,
              "Failed tag batch rolls back metadata and assignments together");
        const auto mergedTags = executeBatch(
            taggedMerge,
            tagBatch({QJsonObject{{"command", "geometry.merge_context"}, {"context", "0"}}}));
        check(taggedMerge.bodies().size() == 2 && taggedMerge.bodies().at(1)->tag == redTag &&
                  taggedMerge.bodies().at(3)->tag == blueTag &&
                  mergedTags["transfers"].toArray().size() == 4,
              "Public merge welds matching tag partitions while preserving separate tag ownership");
        taggedMerge.undo();
        check(taggedMerge.bodies().size() == 4, "Partitioned tagged merge remains one undo");
        const auto beforeTagVisibility = taggedMerge.bodies();
        executeBatch(
            taggedMerge,
            tagBatch({QJsonObject{{"command", "tag.edit"}, {"tag", "1"}, {"visible", false}}}));
        check(taggedMerge.bodies() == beforeTagVisibility &&
                  executeQuery(taggedMerge, {{"query", "document.describe"}})["bodies"]
                      .toArray()[0]
                      .toObject()["effectiveHidden"]
                      .toBool(),
              "Public visibility metadata changes presentation without replacing geometry records");
        executeBatch(
            groupedBatch,
            {{"apiVersion", 1},
             {"documentId", QString::fromStdString(groupedBatch.identity())},
             {"expectedRevision", QString::number(groupedBatch.revision())},
             {"commands",
              QJsonArray{
                  QJsonObject{{"command", "group.create"}, {"members", QJsonArray{"1"}}},
                  QJsonObject{{"command", "scene.state"}, {"body", "2"}, {"locked", true}}}}});
        check(groupedBatch.bodies().at(2)->locked && groupedBatch.bodies().at(1)->parent == 2,
              "One batch can create and then lock a new assembly");
        const auto lockedBytes = encodeDocument(groupedBatch);
        rejects([&] {
            executeBatch(groupedBatch,
                         {{"apiVersion", 1},
                          {"documentId", QString::fromStdString(groupedBatch.identity())},
                          {"expectedRevision", QString::number(groupedBatch.revision())},
                          {"commands", QJsonArray{QJsonObject{{"command", "scene.state"},
                                                              {"body", "2"},
                                                              {"locked", false}},
                                                  QJsonObject{{"command", "geometry.translate"},
                                                              {"body", "1"},
                                                              {"delta", QJsonArray{1, 0, 0}}}}}});
        });
        check(encodeDocument(groupedBatch) == lockedBytes,
              "Unlock-and-edit batch cannot bypass an original ancestor lock");
        for (const auto &invalid :
             {QJsonObject{{"command", "scene.state"}, {"body", "1"}, {"hidden", 1}},
              QJsonObject{{"command", "scene.state"}, {"body", "1"}},
              QJsonObject{{"command", "group.create"}, {"members", QJsonArray{"1", "1"}}}})
            rejects([&] {
                executeBatch(groupedBatch,
                             {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(groupedBatch.identity())},
                              {"expectedRevision", QString::number(groupedBatch.revision())},
                              {"commands", QJsonArray{invalid}}});
            });
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
