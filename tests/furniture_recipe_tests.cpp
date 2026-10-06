#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const char *message) { check(std::abs(a - b) < 1e-6, message); }
QString id(Id value) { return QString::number(value); }
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", id(doc.revision())},
            {"commands", commands}};
}
QJsonObject report(QJsonObject result) {
    return result["recipeOperations"].toArray()[0].toObject();
}
QJsonObject command(bool cabinet, QJsonObject fields = {}) {
    fields["command"] = cabinet ? "assembly.cabinet" : "assembly.table";
    return fields;
}
struct Member {
    Vec3 dimensions, position;
};
std::map<QString, Member> table(double w, double d, double h, double top, double leg,
                                double inset) {
    return {{"top", {{w, d, top}, {0, 0, h - top}}},
            {"leg-1", {{leg, leg, h - top}, {inset, inset, 0}}},
            {"leg-2", {{leg, leg, h - top}, {w - inset - leg, inset, 0}}},
            {"leg-3", {{leg, leg, h - top}, {w - inset - leg, d - inset - leg, 0}}},
            {"leg-4", {{leg, leg, h - top}, {inset, d - inset - leg, 0}}}};
}
std::map<QString, Member> cabinet(double w, double d, double h, double t, int shelves) {
    std::map<QString, Member> parts{{"side-left", {{t, d, h}, {0, 0, 0}}},
                                    {"side-right", {{t, d, h}, {w - t, 0, 0}}},
                                    {"bottom", {{w - 2 * t, d - t, t}, {t, 0, 0}}},
                                    {"top", {{w - 2 * t, d - t, t}, {t, 0, h - t}}},
                                    {"back", {{w - 2 * t, t, h}, {t, d - t, 0}}}};
    const double gap = (h - (shelves + 2) * t) / (shelves + 1);
    for (int i = 1; i <= shelves; ++i)
        parts[QString("shelf-%1").arg(i)] = {{w - 2 * t, d - t, t}, {t, 0, i * (gap + t)}};
    return parts;
}
std::map<QString, Id> verify(const Document &doc, const QJsonObject &result,
                             const std::map<QString, Member> &expected, Vec3 envelope,
                             Vec3 origin) {
    const auto root = result["assembly"].toString().toULongLong();
    check(doc.bodies().at(root)->kind == BodyKind::Group,
          "Furniture has an ordinary assembly group");
    const auto measured = measureEntity(doc, {root, SelectionKind::Body, 0});
    check(measured.local.bounds && !measured.local.volume,
          "Multi-member furniture retains conservative whole-assembly volume semantics");
    check(length(measured.local.bounds->low) < 1e-6 &&
              length(measured.local.bounds->high - envelope) < 1e-6,
          "Furniture has the exact requested envelope");
    check(doc.worldTransform(root).point({}) == origin, "Furniture has its explicit world origin");
    std::map<QString, Id> parts;
    double volume{};
    for (const auto &value : result["members"].toArray()) {
        const auto item = value.toObject();
        const auto role = item["role"].toString();
        const auto body = item["body"].toString().toULongLong();
        check(expected.contains(role) && parts.emplace(role, body).second,
              "Each expected member appears exactly once");
        const auto &want = expected.at(role);
        check(doc.bodies().at(body)->parent == root, "Each member belongs to the assembly");
        const auto actual = measureEntity(doc, {body, SelectionKind::Body, 0});
        check(actual.world.volume && actual.local.bounds && actual.world.bounds &&
                  actual.solid.status == "solid",
              "Every furniture member is independently a validated material solid");
        check(length(actual.local.bounds->dimensions() - want.dimensions) < 1e-6 &&
                  length(actual.world.bounds->low - (origin + want.position)) < 1e-6 &&
                  length(actual.world.bounds->high - (origin + want.position + want.dimensions)) <
                      1e-6,
              "Actual member dimensions, thicknesses and assembly clearances match the design");
        near(*actual.world.volume, want.dimensions.x * want.dimensions.y * want.dimensions.z,
             "Member volume matches independent rectangular prism");
        volume += *actual.world.volume;
        const auto &surface = doc.bodies().at(actual.solidBody)->surface;
        const auto shells =
            analyzeSolidShells(surface, doc.bodies().at(actual.solidBody)->topology);
        check(shells.shells.size() == 1 && shells.shells.front().signedVolume > 0,
              "Furniture member shell is outward oriented");
    }
    check(parts.size() == expected.size(), "All required furniture members exist");
    near(result["memberVolumeSum"].toDouble(), volume,
         "Receipt explicitly sums separately validated member volumes");
    for (const auto &[a, x] : expected)
        for (const auto &[b, y] : expected)
            if (a < b) {
                const Vec3 overlap{
                    std::min(x.position.x + x.dimensions.x, y.position.x + y.dimensions.x) -
                        std::max(x.position.x, y.position.x),
                    std::min(x.position.y + x.dimensions.y, y.position.y + y.dimensions.y) -
                        std::max(x.position.y, y.position.y),
                    std::min(x.position.z + x.dimensions.z, y.position.z + y.dimensions.z) -
                        std::max(x.position.z, y.position.z)};
                check(overlap.x < 1e-7 || overlap.y < 1e-7 || overlap.z < 1e-7,
                      "Member interiors do not overlap");
            }
    for (const auto &value : result["assertions"].toArray())
        check(value.toObject()["passed"] == true &&
                  value.toObject()["evaluation"] == "recipe_completion",
              "Completion postconditions pass");
    check(result["assertions"].toArray().size() == int(2 * parts.size() + 3),
          "Every member and the assembly envelope have assertions");
    return parts;
}
void preserved(const Document &before, const Document &after) {
    for (const auto &[id, body] : before.bodies())
        check(*after.bodies().at(id) == *body, "Existing scene body records remain exact");
    for (const auto &[id, value] : before.materials())
        check(after.materials().at(id) == value, "Existing materials remain unchanged");
    for (const auto &[id, value] : before.definitions())
        check(after.definitions().at(id) == value, "Existing definitions remain unchanged");
    for (const auto &[id, value] : before.instances())
        check(after.instances().at(id) == value, "Existing instance bindings remain unchanged");
    check(before.hostedComponents() == after.hostedComponents() &&
              before.assets() == after.assets() && before.tags() == after.tags(),
          "Existing hosts and resources remain unchanged");
}
void run(const QString &capture) {
    Document scene;
    const auto room =
        report(executeBatch(scene, batch(scene, {QJsonObject{{"command", "assembly.room"}}})));
    executeBatch(
        scene,
        batch(scene,
              {QJsonObject{{"command", "assembly.room.adopt_hosted"}, {"body", room["room"]}},
               QJsonObject{{"command", "assembly.roof"}},
               QJsonObject{{"command", "assembly.stairs"}, {"origin", QJsonArray{7, 0, 0}}}}));
    for (const bool isCabinet : {false, true}) {
        Document doc = scene;
        const auto original = doc.readSnapshot();
        const auto before = encodeContainer(doc);
        const auto history = doc.history().total;
        const auto request = batch(doc, {command(isCabinet, {{"origin", QJsonArray{0, -2, 0}}})});
        const auto preview = previewBatch(doc, request);
        check(encodeContainer(doc) == before, "Furniture preview is private");
        const auto made = executeBatch(doc, request);
        const auto result = report(made);
        check(preview["changes"] == made["changes"] && doc.history().total == history + 1,
              "Furniture commit agrees with preview and uses one Undo");
        const auto parts =
            verify(doc, result,
                   isCabinet ? cabinet(.9, .4, 1.2, .018, 2) : table(1.2, .8, .75, .04, .05, .06),
                   isCabinet ? Vec3{.9, .4, 1.2} : Vec3{1.2, .8, .75}, {0, -2, 0});
        preserved(original, doc);
        const auto bytes = encodeContainer(doc);
        check(encodeContainer(decodeContainer(bytes)) == bytes,
              "Furniture components and scene reopen exactly");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            auto start = original.readSnapshot();
            saveDocument(start, capture + "/furniture-before.sketchyup");
            saveDocument(
                doc, capture + (isCabinet ? "/cabinet-after.sketchyup" : "/table-after.sketchyup"));
        }
        const auto first = parts.at(isCabinet ? "side-left" : "leg-1"),
                   peer = parts.at(isCabinet ? "side-right" : "leg-2");
        const auto definition = doc.instances().at(first)->definition;
        check(doc.instances().at(peer)->definition == definition,
              "Repeated furniture members share a definition");
        if (isCabinet) {
            const auto horizontal = doc.instances().at(parts.at("bottom"))->definition;
            check(horizontal != definition &&
                      doc.instances().at(parts.at("top"))->definition == horizontal &&
                      doc.instances().at(parts.at("shelf-1"))->definition == horizontal &&
                      doc.instances().at(parts.at("shelf-2"))->definition == horizontal,
                  "Cabinet top, bottom and shelves share a separate panel definition");
        } else
            for (const auto &role : {"leg-3", "leg-4"})
                check(doc.instances().at(parts.at(role))->definition == definition,
                      "All four table legs share geometry");
        doc.undo();
        preserved(original, doc);
        check(doc.bodies().size() == original.bodies().size() &&
                  doc.definitions().size() == original.definitions().size() &&
                  doc.materials().size() == original.materials().size(),
              "One Undo removes all furniture and only its new resources");
        doc.redo();
        Id member{};
        for (const auto &[id, body] : doc.bodies())
            if (body->parent == first && !body->surface.faces.empty())
                member = id;
        check(member != 0, "Repeated member exposes ordinary geometry for instance-only edits");
        const auto peerBefore = doc.bodies().at(peer);
        const auto bodiesBeforeEdit = doc.bodies();
        std::set<Id> editedMembers;
        for (const auto &[canonical, sceneId] : doc.instances().at(first)->members)
            editedMembers.insert(sceneId);
        const auto peerVolume = measureEntity(doc, {peer, SelectionKind::Body, 0}).world.volume;
        executeBatch(
            doc,
            batch(doc, {QJsonObject{{"command", "component.edit_instance"},
                                    {"body", id(first)},
                                    {"commands",
                                     QJsonArray{QJsonObject{{"command", "geometry.translate"},
                                                            {"body", id(member)},
                                                            {"delta", QJsonArray{.01, 0, 0}}}}}}}));
        check(doc.instances().at(first)->definition != definition &&
                  doc.instances().at(peer)->definition == definition &&
                  *doc.bodies().at(peer) == *peerBefore &&
                  measureEntity(doc, {peer, SelectionKind::Body, 0}).world.volume == peerVolume,
              "Ordinary unique-instance edits preserve repeated siblings");
        for (const auto &[bodyId, body] : bodiesBeforeEdit)
            if (!editedMembers.contains(bodyId))
                check(*doc.bodies().at(bodyId) == *body,
                      "Unique-instance edit preserves every sibling and unrelated geometry record");
        for (auto invalid :
             {command(isCabinet, {{"width", 0}}),
              command(isCabinet, {{"origin", QJsonArray{999999.9, 0, 0}}}),
              command(isCabinet, {{"guessSize", true}}),
              isCabinet ? command(true, {{"shelves", 2.5}}) : command(false, {{"legSize", 0}}),
              isCabinet ? command(true, {{"height", .3}, {"panelThickness", .1}, {"shelves", 6}})
                        : command(false, {{"width", .3}, {"legInset", .2}})}) {
            const auto unchanged = encodeContainer(doc);
            bool failed{};
            try {
                executeBatch(
                    doc, batch(doc, {QJsonObject{{"command", "document.units"}, {"units", "mm"}},
                                     invalid}));
            } catch (const std::exception &) {
                failed = true;
            }
            check(failed && encodeContainer(doc) == unchanged,
                  "Invalid furniture rolls back preceding batch edits");
        }
    }
    for (const bool large : {false, true}) {
        Document tableDoc, cabinetDoc;
        const double w = large ? 5 : .3, td = large ? 5 : .3, th = large ? 3 : .3,
                     top = large ? .2 : .01, leg = large ? .3 : .02, inset = large ? .5 : 0;
        const auto madeTable =
            executeBatch(tableDoc, batch(tableDoc, {command(false, {{"width", w},
                                                                    {"depth", td},
                                                                    {"height", th},
                                                                    {"topThickness", top},
                                                                    {"legSize", leg},
                                                                    {"legInset", inset}})}));
        verify(tableDoc, report(madeTable), table(w, td, th, top, leg, inset), {w, td, th}, {});
        const double cd = large ? 3 : .2, ch = large ? 5 : .3, t = large ? .1 : .01;
        const int shelves = large ? 6 : 0;
        const auto madeCabinet =
            executeBatch(cabinetDoc, batch(cabinetDoc, {command(true, {{"width", w},
                                                                       {"depth", cd},
                                                                       {"height", ch},
                                                                       {"panelThickness", t},
                                                                       {"shelves", shelves}})}));
        verify(cabinetDoc, report(madeCabinet), cabinet(w, cd, ch, t, shelves), {w, cd, ch}, {});
    }
    for (const int shelves : {0, 6}) {
        Document doc;
        const auto made = executeBatch(doc, batch(doc, {command(true, {{"shelves", shelves},
                                                                       {"width", 2},
                                                                       {"depth", .6},
                                                                       {"height", 2},
                                                                       {"panelThickness", .02}})}));
        verify(doc, report(made), cabinet(2, .6, 2, .02, shelves), {2, .6, 2}, {});
    }
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        run(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{});
        std::cout << "Furniture envelopes, member thicknesses, shared components, preserved scene, "
                     "Undo and persistence passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
