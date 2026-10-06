#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "core/appearance.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/solid_boolean.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message) {
    check(std::abs(actual - expected) < 1e-6, message);
}
Id box(Document &doc, Vec3 o = {}, Vec3 size = {2, 2, 2}) {
    const auto id = doc.addFace(
        {{o, o + Vec3{size.x, 0, 0}, o + Vec3{size.x, size.y, 0}, o + Vec3{0, size.y, 0}}});
    doc.extrude(id, 5, size.z);
    return id;
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject command(Id body, Id tool, const QString &op, bool keep, Id context = 0) {
    return {{"command", "geometry." + op},
            {"body", QString::number(body)},
            {"tool", QString::number(tool)},
            {"context", QString::number(context)},
            {op == "trim" ? "keepTarget" : "keepOperands", keep}};
}
QJsonObject record(const QJsonObject &result) {
    return result["solidOperations"].toArray().at(0).toObject();
}
QJsonArray parts(const QJsonObject &result) { return record(result)["parts"].toArray(); }
Id identity(const QJsonObject &part) { return part["body"].toString().toULongLong(); }
double volume(const Document &doc, Id body) {
    auto surface = doc.bodies().at(body)->surface;
    for (auto &[id, point] : surface.vertices)
        point = doc.worldTransform(body).point(point);
    const auto report = inspectSolid(surface, Topology::rebuild(surface, {}));
    check(report.status == "solid" && report.volume.has_value(),
          "Result validates as native solid");
    return *report.volume;
}
Vec3 normal(const Document &doc, Id body, Id face) {
    const auto n = doc.bodies().at(body)->surface.normal(face);
    const auto inverse = doc.worldTransform(body).inverse();
    return normalized(Vec3{inverse.m[0] * n.x + inverse.m[1] * n.y + inverse.m[2] * n.z,
                           inverse.m[4] * n.x + inverse.m[5] * n.y + inverse.m[6] * n.z,
                           inverse.m[8] * n.x + inverse.m[9] * n.y + inverse.m[10] * n.z});
}
template <class F> void rejects(F fn) {
    try {
        fn();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected solid command rejection");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        for (const QString op : {"trim", "split", "outer_shell"}) {
            for (bool keep : {true, false}) {
                Document doc;
                const auto a = box(doc), b = box(doc, {1, 0, 0});
                const auto source = doc.bodies().at(a), tool = doc.bodies().at(b);
                const auto before = encodeDocument(doc);
                const auto history = doc.history().total;
                const auto request = batch(doc, {command(a, b, op, keep)});
                const auto preview = previewBatch(doc, request);
                check(encodeDocument(doc) == before && doc.history().total == history,
                      "Preview preserves document, allocator and history");
                const auto result = executeBatch(doc, request, BatchResponse::CreatedIds);
                check(result["solidOperations"] == preview["solidOperations"],
                      "Compact receipt and preview predict identical regions/provenance");
                check(doc.history().total == history + 1 && doc.bodies().contains(a) == keep &&
                          doc.bodies().contains(b) == (keep || op == "trim"),
                      "Explicit retention publishes all regions as one edit");
                check(record(result)["keepTarget"].toBool() == keep &&
                          record(result)["keepTool"].toBool() == (keep || op == "trim"),
                      "Receipt makes independent target/tool retention explicit");
                check(parts(result).size() == (op == "split" ? 3 : 1), "Expected region count");
                std::set<QString> portions;
                for (auto value : parts(result)) {
                    const auto part = value.toObject();
                    portions.insert(part["portion"].toString());
                    const auto expected = op == "outer_shell" ? 12 : 4;
                    near(volume(doc, identity(part)), expected, "Analytical region volume");
                    near(part["generatedVolume"].toDouble(), expected, "Receipt world volume");
                }
                check(portions == (op == "split" ? std::set<QString>{"target", "tool", "overlap"}
                                                 : std::set<QString>{"result"}),
                      "Regions have stable semantic labels");
                if (op == "trim")
                    check(doc.bodies().at(b) == tool, "Trim preserves exact cutting tool record");
                auto reopened = decodeContainer(encodeContainer(doc));
                check(encodeDocument(reopened) == encodeDocument(doc),
                      "All regions persist exactly");
                const auto saved = doc.bodies();
                doc.undo();
                check(doc.bodies().size() == 2 && doc.bodies().at(a) == source &&
                          doc.bodies().at(b) == tool,
                      "One Undo restores both exact operands");
                doc.redo();
                for (const auto &[id, body] : saved)
                    check(doc.bodies().at(id)->surface == body->surface,
                          "Redo restores every region");
                rejects([&] { executeBatch(doc, request); });
            }
            Document doc;
            const auto a = box(doc), b = box(doc, {1, 0, 0});
            auto before = encodeDocument(doc);
            rejects([&] {
                executeBatch(doc,
                             batch(doc, {command(a, b, op, false), command(a, 999, op, false)}));
            });
            check(encodeDocument(doc) == before,
                  "Late rejection rolls back all parts and identities");
            auto missing = command(a, b, op, true);
            missing.remove(op == "trim" ? "keepTarget" : "keepOperands");
            rejects([&] { executeBatch(doc, batch(doc, {missing})); });
            setEntityState(doc, b, {}, true);
            before = encodeDocument(doc);
            rejects([&] { executeBatch(doc, batch(doc, {command(a, b, op, false)})); });
            check(encodeDocument(doc) == before, "Locked tool rejects before consuming anything");
            doc.undo();
            const auto prediction = previewBatch(doc, batch(doc, {command(a, b, op, true)}));
            const auto predicted = parts(prediction).at(0).toObject();
            const QJsonValue face = predicted["faces"].toArray().at(0).toObject()["face"];
            const auto pruned = executeBatch(
                doc,
                batch(doc, {command(a, b, op, true), QJsonObject{{"command", "geometry.erase_face"},
                                                                 {"body", predicted["body"]},
                                                                 {"face", face}}}));
            for (auto value : parts(pruned).at(0).toObject()["faces"].toArray())
                check(value.toObject()["face"] != face, "Later erasure prunes face receipts");
            doc.undo();
            const auto removed = executeBatch(
                doc, batch(doc, {command(a, b, op, false),
                                 QJsonObject{{"command", "geometry.delete"},
                                             {"body", QString::number(doc.nextId())}}}));
            check(parts(removed).size() == (op == "split" ? 2 : 0),
                  "Later body deletion prunes the entire result part");
        }
        // Empty Trim replaces only the target; the cutter survives even with no result.
        Document empty;
        const auto ea = box(empty), eb = box(empty);
        const auto originalTool = empty.bodies().at(eb);
        const auto before = encodeDocument(empty);
        rejects([&] { executeBatch(empty, batch(empty, {command(ea, eb, "trim", true)})); });
        check(encodeDocument(empty) == before, "Empty retained Trim is rejected without history");
        auto result = executeBatch(empty, batch(empty, {command(ea, eb, "trim", false)}));
        check(parts(result).empty() && empty.bodies().size() == 1 &&
                  empty.bodies().at(eb) == originalTool,
              "Empty Trim consumes target only");
        empty.undo();
        check(empty.bodies().size() == 2, "Empty Trim Undo restores target");
        // Split styles and local frames follow each region's owner; physical material
        // orientation is independently checked with inverse-transpose world normals.
        for (unsigned mirrors = 0; mirrors < 4; ++mirrors) {
            Document doc;
            const auto a = box(doc), b = box(doc, {1, 0, 0});
            if (mirrors & 1)
                doc.transform(a,
                              Transform::translation({2, 0, 0}) * Transform::scaling({-1, 1, 1}));
            if (mirrors & 2)
                doc.transform(b,
                              Transform::translation({4, 0, 0}) * Transform::scaling({-1, 1, 1}));
            for (auto body : {a, b}) {
                const auto front =
                    createMaterial(doc, "Front " + std::to_string(body), {.8f, .1f, .1f});
                const auto back =
                    createMaterial(doc, "Back " + std::to_string(body), {.1f, .1f, .8f});
                assignMaterial(doc, body, {}, front, true, false);
                assignMaterial(doc, body, {}, back, false, true);
                const auto tag = createTag(doc, "Owner " + std::to_string(body));
                auto styled = std::make_shared<Body>(*doc.bodies().at(body));
                styled->tag = tag;
                styled->color = body == a ? std::array<float, 3>{.7f, .2f, .4f}
                                          : std::array<float, 3>{.1f, .8f, .3f};
                doc.apply({"Style", {{body, doc.bodies().at(body), styled}}}, doc.revision());
            }
            const auto group = createGroup(doc, {a, b});
            doc.transform(group, Transform::translation({21, -7, 4}) *
                                     Transform::rotation({2, 1, 3}, .37) *
                                     Transform::scaling({-1.5, .75, 1.2}));
            const auto stable = encodeDocument(doc);
            rejects([&] { executeBatch(doc, batch(doc, {command(a, b, "split", true)})); });
            check(encodeDocument(doc) == stable, "Wrong context rejects atomically");
            result = executeBatch(doc, batch(doc, {command(a, b, "split", true, group)}));
            for (auto value : parts(result)) {
                const auto part = value.toObject();
                const auto body = identity(part);
                const auto owner = part["portion"] == "tool" ? b : a;
                const auto &created = *doc.bodies().at(body), &source = *doc.bodies().at(owner);
                check(created.parent == group && created.transform == source.transform &&
                          created.color == source.color && created.tag == source.tag &&
                          created.materials == source.materials,
                      "Split inherits region owner placement/style");
                near(volume(doc, body), 5.4, "Split respects reflected nonuniform parent frame");
                for (auto faceValue : part["faces"].toArray()) {
                    const auto face = faceValue.toObject();
                    const auto fid = face["face"].toString().toULongLong();
                    const auto sid = face["sourceBody"].toString().toULongLong();
                    const auto sfid = face["sourceFace"].toString().toULongLong();
                    const bool reversed = dot(normal(doc, body, fid), normal(doc, sid, sfid)) < 0;
                    auto materials = faceMaterials(*doc.bodies().at(sid), sfid);
                    if (reversed)
                        std::swap(materials.front, materials.back);
                    check(face["reversed"].toBool() == reversed &&
                              faceMaterials(created, fid) == materials &&
                              faceColor(created, fid) == faceColor(*doc.bodies().at(sid), sfid),
                          "Every Split region preserves physical material sides/source colors");
                }
            }
        }
        // Fill a hollow target and remove an island that becomes covered by the fill.
        Document shell;
        const auto sa = box(shell), sb = box(shell, {.5, .5, .5}, {1, 1, 1});
        const auto hollow =
            booleanBodies(shell, sa, sb, BooleanOperation::Subtract, 0, false).parts[0].body;
        const auto island = box(shell, {.75, .75, .75}, {.5, .5, .5});
        result = executeBatch(shell, batch(shell, {command(hollow, island, "outer_shell", false)}));
        check(parts(result).size() == 1 && shell.bodies().size() == 1,
              "Filled shell removes covered island");
        const auto filled = parts(result).at(0).toObject();
        near(volume(shell, identity(filled)), 8, "Filled outer volume excludes cavity boundaries");
        check(filled["faces"].toArray().size() == 6, "Only exterior face mappings survive filling");
        for (const QString op : {"trim", "split", "outer_shell"}) {
            Document doc;
            const auto a = box(doc), b = box(doc, {1, 0, 0});
            const auto root = createGroup(doc, {a, b});
            const auto definition = createComponent(doc, root).definition;
            const auto instance =
                placeComponent(doc, definition, Transform::translation({10, 0, 0})).instance;
            const auto ma = doc.instances().at(instance)->members.at(a),
                       mb = doc.instances().at(instance)->members.at(b);
            const auto original = doc.definitions().at(definition);
            const auto siblingA = doc.bodies().at(a), siblingB = doc.bodies().at(b);
            const auto request = batch(
                doc, {QJsonObject{{"command", "component.edit_instance"},
                                  {"body", QString::number(instance)},
                                  {"commands", QJsonArray{command(ma, mb, op, false, instance)}}}});
            const auto stable = encodeDocument(doc);
            const auto preview = previewBatch(doc, request);
            check(encodeDocument(doc) == stable, "Scoped preview preserves shared definition");
            result = executeBatch(doc, request);
            check(result["solidOperations"] == preview["solidOperations"] &&
                      record(result)["sourceBody"] == QString::number(ma) &&
                      record(result)["toolBody"] == QString::number(mb),
                  "Scoped receipt resolves consumed source and generated scene identities");
            check(doc.definitions().at(definition) == original && doc.bodies().at(a) == siblingA &&
                      doc.bodies().at(b) == siblingB && !doc.bodies().contains(ma) &&
                      doc.bodies().contains(mb) == (op == "trim"),
                  "Unique edit isolates sibling definition");
            check(!result["componentOperations"]
                       .toArray()
                       .at(0)
                       .toObject()["solidOperations"]
                       .toArray()
                       .empty(),
                  "Component receipt retains canonical region records");
            for (auto value : parts(result)) {
                const auto part = value.toObject();
                check(doc.bodies().at(identity(part))->parent == instance,
                      "Scoped regions stay in instance");
                near(volume(doc, identity(part)), op == "outer_shell" ? 12 : 4,
                     "Scoped region world volume");
                for (auto fv : part["faces"].toArray()) {
                    const auto face = fv.toObject();
                    check(face["sourceBody"] == QString::number(ma) ||
                              face["sourceBody"] == QString::number(mb),
                          "Scoped provenance resolves original scene source");
                }
            }
            auto reopened = decodeContainer(encodeContainer(doc));
            check(encodeDocument(reopened) == encodeDocument(doc),
                  "Scoped regions persist exactly");
            doc.undo();
            check(doc.bodies().contains(ma) && doc.bodies().contains(mb),
                  "Scoped Undo restores operands");
            doc.redo();
            check(!doc.bodies().contains(ma), "Scoped Redo reapplies replacement");
        }
        std::cout << "Trim/Split/Outer Shell retention, regions, physical materials, scope, "
                     "preview, rollback, Undo and persistence passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
