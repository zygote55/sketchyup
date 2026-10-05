#include "automation/component_scope.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <iostream>
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
    check(rejected, "Invalid component scope must reject");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        createComponent(doc, 1);
        const auto frame = Transform::translation({10, 5, 0}) * Transform::scaling({-2, 3, 1});
        const auto peer = placeComponent(doc, 1, frame).instance;
        const auto member = doc.instances().at(peer)->members.at(2);
        auto request = [&](const QJsonObject &command) {
            return QJsonObject{
                {"apiVersion", 1},
                {"documentId", QString::fromStdString(doc.identity())},
                {"expectedRevision", QString::number(doc.revision())},
                {"commands", QJsonArray{componentScopeCommand(doc, peer, {command})}}};
        };
        const QJsonObject rectangle{{"command", "geometry.rectangle"},
                                    {"body", QString::number(member)},
                                    {"space", "world"},
                                    {"origin", QJsonArray{8, 6, 1}},
                                    {"normal", QJsonArray{0, 0, 1}},
                                    {"xAxis", QJsonArray{1, 0, 0}},
                                    {"width", 1},
                                    {"height", 1}};
        const auto before = encodeDocument(doc);
        const auto preview = previewBatch(doc, request(rectangle));
        check(encodeDocument(doc) == before, "Placed component preview is read only");
        const auto result = executeBatch(doc, request(rectangle));
        check(result["changes"] == preview["changes"], "Placed preview and commit lineage agree");
        Id added = 0;
        for (const auto &[id, face] : doc.bodies().at(member)->surface.faces)
            if (id != 5)
                added = id;
        check(added && std::abs(doc.worldArea(member, added) - 1) < tolerance &&
                  std::abs(doc.worldArea(2, added) - 1. / 6) < tolerance,
              "World drawing honors the initiating mirrored nonuniform instance frame");
        check(doc.worldTransform(peer) == frame &&
                  doc.definitions().at(1)->members.at(1)->transform == Transform{},
              "Temporary edit placement never enters the canonical definition");
        const QJsonObject draw{{"command", "geometry.rectangle"},
                               {"body", "0"},
                               {"space", "world"},
                               {"origin", QJsonArray{12, 6, 2}},
                               {"normal", QJsonArray{0, 0, 1}},
                               {"xAxis", QJsonArray{1, 0, 0}},
                               {"width", 1},
                               {"height", 1}};
        const auto created =
            componentScopeResult(executeBatch(doc, request(draw)), peer)["created"].toArray();
        check(created.size() == 1,
              "Native created selection contains only the initiating placement");
        const auto body = created[0].toString().toULongLong();
        const auto &surface = doc.bodies().at(body)->surface;
        check(std::abs(doc.worldArea(body, surface.faces.begin()->first) - 1) < tolerance,
              "New world-root geometry is converted into the component frame");
        double x = 1e9;
        for (const auto &[id, point] : surface.vertices)
            x = std::min(x, doc.worldTransform(body).point(point).x);
        check(std::abs(x - 12) < tolerance,
              "New component member stays at the requested world position");
        doc.undo();
        QJsonObject array{{"command", "geometry.array_selection"},
                          {"entities", QJsonArray{QJsonObject{{"body", QString::number(member)},
                                                              {"kind", "context"},
                                                              {"entity", "0"}}}},
                          {"space", "world"},
                          {"mode", "linear"},
                          {"delta", QJsonArray{0, 0, 1}},
                          {"count", 2}};
        auto copies = executeBatch(doc, request(array))["copies"].toArray();
        check(copies.size() == 2 && copies[0].toObject()["sourceBody"] == QString::number(member),
              "Shared array copy selection uses the initiating instance's scene IDs");
        const auto stamp = doc.amendmentStamp();
        array["count"] = 3;
        executeAmend(doc, stamp, request(array));
        check(doc.instances().at(peer)->members.size() == 5 &&
                  doc.instances().at(1)->members.size() == 5,
              "Shared array count amendment updates all placements");
        doc.undo();
        check(doc.instances().at(peer)->members.size() == 2,
              "Shared array amendment remains one undo");
        const auto stable = encodeDocument(doc);
        auto invalid = rectangle;
        invalid["body"] = "2";
        rejects([&] { executeBatch(doc, request(invalid)); });
        check(encodeDocument(doc) == stable,
              "Peer scene IDs cannot cross the explicit instance scope");
        executeBatch(doc, request(QJsonObject{{"command", "guide.point"},
                                              {"body", QString::number(member)},
                                              {"space", "world"},
                                              {"origin", QJsonArray{8, 6, 0}}}));
        doc.clearGuides();
        check(doc.bodies().at(2)->guides.empty() && doc.bodies().at(member)->guides.empty() &&
                  doc.definitions().at(1)->members.at(2)->guides.empty(),
              "Clear all placed guides updates canonical and resolved component records together");
        executeBatch(doc, request(QJsonObject{{"command", "scene.state"},
                                              {"body", QString::number(member)},
                                              {"hidden", true},
                                              {"locked", true}}));
        executeBatch(doc, {{"apiVersion", 1},
                           {"documentId", QString::fromStdString(doc.identity())},
                           {"expectedRevision", QString::number(doc.revision())},
                           {"commands", QJsonArray{QJsonObject{{"command", "scene.state"},
                                                               {"body", "0"},
                                                               {"hidden", false},
                                                               {"locked", false}}}}});
        check(!doc.bodies().at(2)->hidden && !doc.bodies().at(member)->locked &&
                  !doc.definitions().at(1)->members.at(2)->locked,
              "Global reveal/unlock resets canonical and placed member states atomically");
        rejects([&] { setEntityState(doc, 0, true, {}); });
        for (bool nested : {false, true}) {
            Document isolated;
            isolated.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
            createComponent(isolated, 1);
            Id outer = 1;
            if (nested) {
                outer = createGroup(isolated, {1});
                createComponent(isolated, outer);
            }
            placeComponent(isolated, isolated.instances().at(outer)->definition,
                           Transform::translation({10, 0, 0}));
            const auto baseline = isolated.readSnapshot();
            const auto history = isolated.history().position;
            auto scoped = [&](QJsonArray commands) {
                return QJsonObject{
                    {"apiVersion", 1},
                    {"documentId", QString::fromStdString(isolated.identity())},
                    {"expectedRevision", QString::number(isolated.revision())},
                    {"commands", QJsonArray{QJsonObject{{"command", "component.edit_instance"},
                                                        {"body", "1"},
                                                        {"commands", commands}}}}};
            };
            const QJsonObject shift{
                {"command", "geometry.translate"}, {"body", "2"}, {"delta", QJsonArray{.25, 0, 0}}};
            const auto bytes = encodeDocument(isolated);
            previewBatch(isolated, scoped({shift}));
            check(encodeDocument(isolated) == bytes,
                  "Instance-only preview leaves shared model unchanged");
            executeBatch(isolated, scoped({shift}));
            check(isolated.history().position == history + 1 &&
                      isolated.instances().at(1)->definition !=
                          baseline.instances().at(1)->definition,
                  "Instance edit makes definition unique and occupies one undo entry");
            for (const auto &[id, record] : baseline.bodies())
                if (id != 2)
                    check(*isolated.bodies().at(id) == *record,
                          "Instance edit preserves all other body records");
            for (const auto &[id, record] : baseline.definitions())
                check(isolated.definitions().at(id) == record,
                      "Shared and ancestor source definitions remain intact");
            check(std::abs(isolated.worldTransform(2)
                               .point(isolated.bodies().at(2)->surface.vertices.begin()->second)
                               .x -
                           .25) < tolerance,
                  "Selected component geometry moves by exact requested amount");
            const auto after = encodeDocument(isolated);
            auto reopened = decodeDocument(after);
            check(encodeDocument(reopened) == after,
                  "Unique instance edits persist with coherent bindings");
            isolated.undo();
            check(isolated.instances().size() == baseline.instances().size() &&
                      isolated.definitions().size() == baseline.definitions().size() &&
                      *isolated.bodies().at(2) == *baseline.bodies().at(2),
                  "One Undo restores unique edit and ancestor ownership");
            for (const auto &[id, record] : baseline.instances())
                check(*isolated.instances().at(id) == *record,
                      "Undo restores all component bindings");
            check(QJsonDocument::fromJson(encodeDocument(isolated)).object()["definitions"] ==
                      QJsonDocument::fromJson(encodeDocument(baseline)).object()["definitions"],
                  "Undo restores all definition records");
            isolated.redo();
            check(isolated.instances().at(1)->definition == reopened.instances().at(1)->definition,
                  "One Redo restores instance edit");
            const auto stableBytes = encodeDocument(isolated);
            auto outside = shift;
            outside["body"] = QString::number(isolated.nextId() - 1);
            rejects([&] { executeBatch(isolated, scoped({outside})); });
            for (auto forbidden : {QJsonObject{{"command", "component.edit"},
                                               {"definition", "1"},
                                               {"commands", QJsonArray{shift}}},
                                   QJsonObject{{"command", "material.edit"}, {"material", "1"}},
                                   QJsonObject{{"command", "document.units"}, {"units", "mm"}}})
                rejects([&] { executeBatch(isolated, scoped({shift, forbidden})); });
            check(encodeDocument(isolated) == stableBytes,
                  "Invalid scoped commands roll back geometry and unique definitions");
        }
        std::cout << "Component scene-ID scopes, placement frames, preview, selection, arrays and "
                     "undo passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
