#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "core/annotations.hpp"
#include "core/components.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    try {
        operation();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected annotation command rejection");
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject query(const Document &doc, const char *name, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return extra;
}
QJsonObject fixed(Vec3 p) {
    return {{"kind", "point"}, {"space", "world"}, {"point", QJsonArray{p.x, p.y, p.z}}};
}
QJsonObject describe(const Document &doc, Id id) {
    return inspectDocument(doc, query(doc, "annotation.describe",
                                      {{"annotation", QString::number(id)}}))["data"]
        .toObject();
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addWire(0, {0, 0, 0}, {4, 0, 0});
        const auto edge = doc.bodies().at(body)->topology.edges.begin()->first;
        const auto geometry = doc.bodies();
        auto onEdge = [&](double fraction) {
            return QJsonObject{{"kind", "edge"},
                               {"body", QString::number(body)},
                               {"edge", QString::number(edge)},
                               {"fraction", fraction}};
        };
        const QJsonObject create{{"command", "annotation.create"},
                                 {"name", "Span"},
                                 {"kind", "distance"},
                                 {"anchors", QJsonArray{onEdge(.25), onEdge(.75)}},
                                 {"offset", QJsonArray{0, 1, 0}}};
        const auto original = encodeContainer(doc);
        previewBatch(doc, batch(doc, {create}));
        check(encodeContainer(doc) == original, "Preview is immutable");
        StagingSession staging;
        const auto stage = staging.prepare(doc, batch(doc, {create}));
        const auto stageId = stage["stageId"].toString();
        const auto changes = staging.changes(doc, stageId)["changes"].toArray();
        check(changes.size() == 1 && changes[0].toObject()["kind"] == "annotation",
              "Stage names annotation resource");
        doc.applyPrepared(*staging.proposal(doc, stageId));
        const auto span = doc.annotations().begin()->first;
        check(describe(doc, span)["distanceMetres"] == 2 && doc.bodies() == geometry,
              "Annotation measures world metres without changing geometry");
        const auto history = doc.history().total;
        const QJsonObject label{{"command", "annotation.create"},
                                {"name", "Joint"},
                                {"kind", "label"},
                                {"text", "Keep clear\nJoint"},
                                {"anchors", QJsonArray{onEdge(.5)}}};
        const auto result = executeBatch(doc, batch(doc, {label}));
        const auto labelId = result["createdAnnotations"].toArray()[0].toString();
        check(doc.history().total == history + 1 &&
                  doc.annotations().contains(labelId.toULongLong()),
              "Created identities explicit in one history item");
        auto request = query(doc, "annotations.query", {{"limit", 1}});
        auto data = inspectDocument(doc, request)["data"].toObject();
        check(data["items"].toArray().size() == 1 && !data["nextCursor"].isNull(),
              "First bounded page");
        request["cursor"] = data["nextCursor"];
        data = inspectDocument(doc, request)["data"].toObject();
        check(data["items"].toArray().size() == 1 && data["nextCursor"].isNull(),
              "Second bounded page");
        check(inspectDocument(doc, query(doc, "annotations.query", {{"kind", "label"}}))["data"]
                      .toObject()["items"]
                      .toArray()
                      .size() == 1,
              "Kind filter");
        const auto beforeSplit = doc.annotations();
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "geometry.split_edge"},
                                                  {"body", QString::number(body)},
                                                  {"edge", QString::number(edge)},
                                                  {"fraction", .5}},
                                      QJsonObject{{"command", "annotation.update"},
                                                  {"annotation", labelId},
                                                  {"text", "Split joint"}}}));
        doc.undo();
        check(doc.annotations() == beforeSplit, "Compound geometry and annotation Undo exact");
        doc.redo();
        check(describe(doc, span)["distanceMetres"] == 2 &&
                  describe(doc, labelId.toULongLong())["state"] == "ambiguous",
              "Shared command records follow topology and expose ambiguity");
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "annotation.update"},
                                                  {"annotation", labelId},
                                                  {"text", "Review attachment"},
                                                  {"color", QJsonArray{.125, .25, .5}}}}));
        check(describe(doc, labelId.toULongLong())["state"] == "ambiguous",
              "Broken text edits preserve association");
        executeBatch(doc, batch(doc, {QJsonObject{{"command", "annotation.update"},
                                                  {"annotation", labelId},
                                                  {"anchors", QJsonArray{fixed({1, 2, 3})}}}}));
        check(describe(doc, labelId.toULongLong())["state"] == "resolved",
              "Explicit rebind resolves broken label");
        doc.undo();
        check(describe(doc, labelId.toULongLong())["state"] == "ambiguous", "Rebind Undo exact");
        doc.erase(body);
        check(describe(doc, span)["distanceMetres"].isNull() &&
                  describe(doc, span)["state"] == "missing",
              "Missing dimension never returns stale numeric measurement");
        const auto before = encodeContainer(doc);
        auto valid = create;
        valid["name"] = "Fixed";
        valid["anchors"] = QJsonArray{fixed({}), fixed({3, 4, 0})};
        for (int variant = 0; variant < 15; ++variant) {
            auto bad = valid;
            if (variant == 0)
                bad["unknown"] = true;
            if (variant == 1)
                bad["leader"] = 1;
            if (variant == 2)
                bad["textSize"] = 49;
            if (variant == 3)
                bad["color"] = QJsonArray{1.01, 0, 0};
            if (variant == 4)
                bad["anchors"] = QJsonArray{fixed({})};
            if (variant == 5)
                bad["kind"] = "angular";
            if (variant == 6)
                bad["name"] = "  ";
            if (variant == 7)
                bad["offset"] = QJsonArray{0, 0, "0"};
            if (variant == 8) {
                auto a = fixed({});
                a["space"] = "local";
                bad["anchors"] = QJsonArray{a, fixed({})};
            }
            if (variant == 9) {
                auto a = fixed({});
                a["state"] = "resolved";
                bad["anchors"] = QJsonArray{a, fixed({})};
            }
            if (variant == 10) {
                auto a = onEdge(.5);
                a["body"] = "01";
                bad["anchors"] = QJsonArray{a, fixed({})};
            }
            if (variant == 11) {
                auto a = onEdge(1.1);
                bad["anchors"] = QJsonArray{a, fixed({})};
            }
            if (variant == 12)
                bad["anchors"] = QJsonArray{QJsonObject{{"kind", "vertex"},
                                                        {"body", QString::number(body)},
                                                        {"vertex", "1"}},
                                            fixed({})};
            if (variant == 13)
                bad["text"] = false;
            if (variant == 14) {
                bad["kind"] = "label";
                bad["anchors"] = QJsonArray{fixed({})};
                bad["text"] = "";
            }
            rejects([&] {
                executeBatch(doc, batch(doc, {QJsonObject{{"command", "annotation.delete"},
                                                          {"annotation", labelId}},
                                              bad}));
            });
            check(encodeContainer(doc) == before, "Invalid compound batch is atomic");
        }
        executeBatch(doc, batch(doc, {valid, QJsonObject{{"command", "document.units"},
                                                         {"units", "ft-in"}}}));
        const auto fixedId = doc.annotations().rbegin()->first;
        check(describe(doc, fixedId)["distanceMetres"] == 5 &&
                  describe(doc, fixedId)["displayUnits"] == "ft-in",
              "Distance and display units explicit");
        rejects([&] { inspectDocument(doc, request); }); // stale revision/cursor
        rejects([&] {
            executeBatch(doc, batch(doc, {QJsonObject{{"command", "annotation.update"},
                                                      {"annotation", QString::number(fixedId)}}}));
        });
        doc.undo();
        check(doc.annotations().size() == 2, "Compound metadata Undo exact");
        const auto faceBody = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        const auto face = doc.bodies().at(faceBody)->surface.faces.begin()->first;
        const auto vertex = doc.bodies().at(faceBody)->surface.vertices.begin()->first;
        doc.transform(faceBody,
                      Transform::translation({10, 20, 30}) * Transform::scaling({-2, 3, 1}));
        auto faceLabel = label;
        faceLabel["name"] = "Face note";
        faceLabel["anchors"] = QJsonArray{QJsonObject{{"kind", "face"},
                                                      {"body", QString::number(faceBody)},
                                                      {"face", QString::number(face)},
                                                      {"space", "world"},
                                                      {"point", QJsonArray{8, 23, 30}}}};
        executeBatch(doc, batch(doc, {faceLabel}));
        const auto faceId = doc.annotations().rbegin()->first;
        check(describe(doc, faceId)["resolvedAnchors"].toArray()[0].toObject()["point"].toArray() ==
                  QJsonArray{8, 23, 30},
              "World face anchor respects reflection and nonuniform scale");
        auto vertexLabel = faceLabel;
        vertexLabel["name"] = "Vertex note";
        vertexLabel["anchors"] = QJsonArray{QJsonObject{{"kind", "vertex"},
                                                        {"body", QString::number(faceBody)},
                                                        {"vertex", QString::number(vertex)}}};
        executeBatch(doc, batch(doc, {vertexLabel}));
        const auto component = createComponent(doc, faceBody, "Shared");
        rejects([&] {
            executeBatch(doc,
                         batch(doc, {componentScopeCommand(doc, component.instance, {valid})}));
        });
        std::cout << "Annotation commands, staging, inspection, association and rollback passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
