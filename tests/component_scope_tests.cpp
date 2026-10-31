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
// A host definition with one member to place under, and a second definition to place there.
struct NestedPlacementSetup {
    Document doc;
    Id host{}, hostInstance{}, secondInstance{}, part{}, member{}, hostRoot{}, partBody{};
};

NestedPlacementSetup nestedPlacementSetup() {
    NestedPlacementSetup setup;
    auto &doc = setup.doc;
    // The host's member to place under is a group, the only kind a placement may name as parent.
    const auto face = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
    const auto inner = createGroup(doc, {face}, "Inner");
    const auto outer = createGroup(doc, {inner}, "Outer");
    const auto host = createComponent(doc, outer);
    setup.host = host.definition;
    setup.hostInstance = host.instance;
    const auto hostDefinition = doc.definitions().at(setup.host);
    setup.hostRoot = hostDefinition->root;
    for (const auto &[id, body] : hostDefinition->members)
        if (id != setup.hostRoot && body->kind == BodyKind::Group)
            setup.member = id;
    check(setup.member != 0, "The host definition has a group member to place under");
    // A body outside the host definition: its ID is no member of the edit.
    setup.partBody = doc.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}});
    setup.part = createComponent(doc, setup.partBody).definition;
    // A second placement has scene IDs of its own, unlike the canonical member IDs.
    setup.secondInstance =
        placeComponent(doc, setup.host, Transform::translation({10, 0, 0})).instance;
    return setup;
}

// D-083: a placement made inside a shared definition edit names its parent by the IDs that edit
// shows: a member of the definition, or "0" for the definition's root. An ID outside the
// definition is refused by name, and nothing is published.
void nestedPlacementParents() {
    const QJsonArray identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // The request that places the part inside the host definition under `parent`.
    auto request = [&](const NestedPlacementSetup &setup, const QJsonValue &parent) {
        const QJsonObject place{{"command", "component.instance"},
                                {"definition", QString::number(setup.part)},
                                {"matrix", identity},
                                {"parent", parent}};
        const QJsonObject edit{{"command", "component.edit"},
                               {"definition", QString::number(setup.host)},
                               {"commands", QJsonArray{place}}};
        return QJsonObject{{"apiVersion", 1},
                           {"documentId", QString::fromStdString(setup.doc.identity())},
                           {"expectedRevision", QString::number(setup.doc.revision())},
                           {"commands", QJsonArray{edit}}};
    };
    // The parent of the one placement of the part that the host definition now records.
    auto placedUnder = [](const NestedPlacementSetup &setup) {
        const auto definition = setup.doc.definitions().at(setup.host);
        Id parent = 0;
        int placements = 0;
        for (const auto &[root, target] : definition->references)
            if (target == setup.part) {
                parent = definition->members.at(root)->parent;
                ++placements;
            }
        check(placements == 1, "The edit records exactly one placement of the part");
        return parent;
    };

    {
        auto setup = nestedPlacementSetup();
        executeBatch(setup.doc, request(setup, QString::number(setup.member)));
        check(placedUnder(setup) == setup.member,
              "A member of the edited definition is accepted as a placement parent");
    }
    {
        auto setup = nestedPlacementSetup();
        executeBatch(setup.doc, request(setup, "0"));
        check(placedUnder(setup) == setup.hostRoot,
              "\"0\" in a definition edit places the component under the definition's root");
    }
    {
        // A body outside the definition, and the scene ID of a placement's member: neither is in
        // the definition, so each is refused by name and publishes nothing.
        auto setup = nestedPlacementSetup();
        const auto sceneMember =
            setup.doc.instances().at(setup.secondInstance)->members.at(setup.member);
        const auto stable = encodeDocument(setup.doc);
        for (const auto outside : {setup.partBody, sceneMember}) {
            bool refused = false;
            try {
                executeBatch(setup.doc, request(setup, QString::number(outside)));
            } catch (const std::exception &error) {
                refused = QString::fromStdString(error.what()).contains("is not in this edit");
            }
            check(refused, "A parent outside the edited definition is refused by name");
            check(encodeDocument(setup.doc) == stable,
                  "A refused placement parent publishes nothing");
        }
    }
    {
        // The same placement inside an instance's scope names the member by its scene ID.
        auto setup = nestedPlacementSetup();
        const auto scene = setup.doc.instances().at(setup.secondInstance)->members.at(setup.member);
        const QJsonObject place{{"command", "component.instance"},
                                {"definition", QString::number(setup.part)},
                                {"matrix", identity},
                                {"parent", QString::number(scene)}};
        executeBatch(setup.doc,
                     QJsonObject{{"apiVersion", 1},
                                 {"documentId", QString::fromStdString(setup.doc.identity())},
                                 {"expectedRevision", QString::number(setup.doc.revision())},
                                 {"commands", QJsonArray{componentScopeCommand(
                                                  setup.doc, setup.secondInstance, {place})}}});
        check(placedUnder(setup) == setup.member,
              "A scene ID inside an instance scope resolves to the definition member it shows");
    }
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
        nestedPlacementParents();
        std::cout << "Component scene-ID scopes, placement frames, preview, selection, arrays and "
                     "undo passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
