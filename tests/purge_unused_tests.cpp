// R111: component.delete_definition and document.purge_unused (ADR 0005, "Removing unused
// definitions"). Every model here is synthetic and built in code.
#include "automation/commands.hpp"
#include "automation/session.hpp"
#include "component_parameter_fixture.hpp"
#include "core/assets.hpp"
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/model_style.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
using namespace sketchy;
using namespace sketchy::parameter_fixture;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
QString id(Id value) { return QString::number(value); }
QJsonObject batchFor(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", id(doc.revision())},
            {"commands", commands}};
}
QJsonObject run(Document &doc, const QJsonObject &command) {
    return executeBatch(doc, batchFor(doc, QJsonArray{command}));
}
QJsonObject purge(QJsonObject fields = {}) {
    fields["command"] = "document.purge_unused";
    return fields;
}
// A refusal that carries a code. The document must not change.
QJsonObject refused(Document &doc, const QJsonObject &command, const std::string &code) {
    const auto before = encodeDocument(doc);
    const auto revision = doc.revision();
    try {
        run(doc, command);
    } catch (const InspectionError &error) {
        check(error.code() == code, "Refusal code " + error.code() + " differs from " + code);
        check(encodeDocument(doc) == before && doc.revision() == revision,
              "A refused command changes nothing");
        return error.details();
    }
    throw std::runtime_error("Expected a refusal: " + code);
}
// A rejection without a code (the same rule, enforced as a plain error). The document must not
// change.
void rejected(Document &doc, const QJsonObject &command, const std::string &why) {
    const auto before = encodeDocument(doc);
    const auto revision = doc.revision();
    try {
        run(doc, command);
    } catch (const std::exception &) {
        check(encodeDocument(doc) == before && doc.revision() == revision,
              "A rejected command changes nothing: " + why);
        return;
    }
    throw std::runtime_error("Expected a rejection: " + why);
}
// The document's content without its revision counter. An undo or a redo is an edit that moves the
// counter, so these comparisons ask whether the records are the same.
QJsonObject content(const Document &doc) {
    auto object = QJsonDocument::fromJson(encodeDocument(doc)).object();
    check(!object.isEmpty(), "The document encodes as a JSON object");
    object.remove("revision");
    return object;
}
std::set<std::string> ids(std::initializer_list<Id> values) {
    std::set<std::string> result;
    for (const auto value : values)
        result.insert(std::to_string(value));
    return result;
}
// The IDs one category of a purge result removed.
std::set<std::string> removedIds(const QJsonObject &result, const char *category) {
    const auto operations = result["purgeOperations"].toArray();
    check(operations.size() == 1, "A purge reports one operation");
    const auto first = operations.at(0).toObject();
    std::set<std::string> removed;
    for (const auto value : first[category].toObject()["removed"].toArray())
        removed.insert(value.toObject()["id"].toString().toStdString());
    return removed;
}
std::size_t verticesOf(const Document &doc, Id definition) {
    std::size_t vertices = 0;
    for (const auto &[member, body] : doc.definitions().at(definition)->members)
        vertices += body->surface.vertices.size();
    return vertices;
}
// Unplaces one top-level placement, as the typed erase does: its members and binding go with it.
void unplace(Document &doc, Id instance) {
    run(doc, {{"command", "geometry.erase_selection"},
              {"entities", QJsonArray{QJsonObject{{"body", id(instance)},
                                                  {"kind", "context"},
                                                  {"entity", "0"}}}}});
}
// A synthetic mixed document. Placed: "Post" with two placements. Library assembly, unplaced:
// "Room". A nested chain, unplaced: "Outer" nests "Inner". Materials and assets:
//   Oak (used by Post) with asset Oak.png (kept);
//   Tile (only in Outer, so Outer's removal frees it) with asset Tile.png (freed by Tile);
//   Glass (only in Inner, so Inner's removal frees it);
//   Library finish (used by Room, kept);
//   Spare (used by nothing) and Loose.png (used by nothing).
struct Scene {
    Document doc;
    Id oak{}, tile{}, glass{}, spare{}, lib{};
    Id oakAsset{}, tileAsset{}, looseAsset{};
    Id post{}, postInstance{}, room{}, outer{}, inner{};
};
Scene build() {
    Scene s;
    auto &doc = s.doc;
    s.oakAsset = createAsset(doc, "Oak.png", "image/png");
    s.oak = createMaterial(doc, "Oak", {.6f, .4f, .2f}, 1, s.oakAsset);
    s.tileAsset = createAsset(doc, "Tile.png", "image/png");
    s.tile = createMaterial(doc, "Tile", {.9f, .9f, .85f}, 1, s.tileAsset);
    s.glass = createMaterial(doc, "Glass", {.2f, .55f, .8f}, .35f);
    s.lib = createMaterial(doc, "Library finish", {.5f, .5f, .5f});
    s.spare = createMaterial(doc, "Spare", {.1f, .1f, .1f});
    s.looseAsset = createAsset(doc, "Loose.png", "image/png");
    const auto postBox = box(doc, {0, 0, 0}, {1, 1, 1}, "Post");
    assignMaterial(doc, postBox, std::nullopt, s.oak);
    const auto post = createComponent(doc, createGroup(doc, {postBox}, "Post"), "Post");
    s.post = post.definition;
    s.postInstance = post.instance;
    placeComponent(doc, s.post, Transform::translation({3, 0, 0}));
    const auto roomBox = box(doc, {0, 0, 0}, {2, 2, 2}, "Room");
    assignMaterial(doc, roomBox, std::nullopt, s.lib);
    const auto room = createComponent(doc, createGroup(doc, {roomBox}, "Room"), "Room");
    s.room = room.definition;
    defineComponentParameter(doc, s.room,
                             parameter("width", "Width", ParameterType::Length,
                                       ComponentParameterMode::Input, "2 m"));
    setComponentAssembly(doc, s.room, ComponentAssembly{"sketchyup-tests/room", 1});
    unplace(doc, room.instance);
    const auto innerBox = box(doc, {0, 0, 0}, {1, 1, 1}, "Inner");
    assignMaterial(doc, innerBox, std::nullopt, s.glass);
    const auto inner = createComponent(doc, createGroup(doc, {innerBox}, "Inner"), "Inner");
    s.inner = inner.definition;
    const auto tileBox = box(doc, {2, 0, 0}, {3, 1, 1}, "Outer");
    assignMaterial(doc, tileBox, std::nullopt, s.tile);
    // Outer's group takes Inner's placement as a nested reference; unplacing Outer takes that too.
    const auto outer = createComponent(doc, createGroup(doc, {tileBox, inner.instance}, "Outer"),
                                       "Outer");
    s.outer = outer.definition;
    unplace(doc, outer.instance);
    return s;
}
void checkBuilt(const Scene &s) {
    const auto &doc = s.doc;
    check(doc.definitions().size() == 4, "The fixture holds four definitions");
    check(doc.instances().size() == 2 && doc.instances().contains(s.postInstance),
          "Only Post is placed, twice");
    check(definitionUse(doc, s.outer).placements.empty() && definitionUse(doc, s.inner).placements.empty(),
          "Outer and Inner are unplaced");
    check(definitionUse(doc, s.inner).nestedBy == std::vector<Id>{s.outer},
          "Outer nests Inner");
    check(definitionUse(doc, s.room).assembly.has_value(), "Room has an assembly identity");
    check(doc.materials().size() == 5 && doc.assets().size() == 3, "The fixture holds five materials and three assets");
}
void deleteUnusedDefinition() {
    auto s = build();
    checkBuilt(s);
    auto &doc = s.doc;
    const auto before = content(doc);
    const auto result = run(doc, {{"command", "component.delete_definition"},
                                  {"definition", id(s.outer)}});
    check(!doc.definitions().contains(s.outer), "The unused definition is removed");
    check(doc.definitions().contains(s.inner), "A definition it nests is kept");
    const auto operation = result["componentOperations"].toArray().at(0).toObject();
    check(operation["definition"] == id(s.outer) && operation["nowUnused"].toArray() == QJsonArray{id(s.inner)},
          "The result names the released definition as nowUnused");
    doc.undo();
    check(content(doc) == before, "One undo restores the definition with its ID and content");
    doc.redo();
    check(!doc.definitions().contains(s.outer), "Redo removes it again");
}
void deleteRefusals() {
    auto s = build();
    auto &doc = s.doc;
    const auto placed = refused(doc, {{"command", "component.delete_definition"},
                                      {"definition", id(s.post)}},
                                "DEFINITION_PLACED");
    check(placed["placements"].toDouble() == 2 && placed["placementIds"].toArray().size() == 2,
          "A placed refusal counts the placements and names them");
    const auto nested = refused(doc, {{"command", "component.delete_definition"},
                                      {"definition", id(s.inner)}},
                                "DEFINITION_NESTED");
    check(nested["nestedBy"].toArray() == QJsonArray{id(s.outer)},
          "A nested refusal names the definition that nests it");
    const auto assembly = refused(doc, {{"command", "component.delete_definition"},
                                        {"definition", id(s.room)}},
                                  "DEFINITION_ASSEMBLY");
    check(assembly["assembly"].toObject()["id"].toString() == "sketchyup-tests/room" &&
              assembly["assembly"].toObject()["version"].toDouble() == 1,
          "An assembly refusal names the library identity");
    refused(doc, {{"command", "component.delete_definition"}, {"definition", "999999"}}, "NOT_FOUND");
    rejected(doc, {{"command", "component.edit"},
                   {"definition", id(s.post)},
                   {"commands", QJsonArray{QJsonObject{{"command", "component.delete_definition"},
                                                       {"definition", id(s.outer)}}}}},
             "component.delete_definition inside a component edit");
    rejected(doc, {{"command", "component.edit"},
                   {"definition", id(s.post)},
                   {"commands", QJsonArray{QJsonObject{{"command", "document.purge_unused"}}}}},
             "document.purge_unused inside a component edit");
}
void purgeMixedDocument() {
    auto s = build();
    auto &doc = s.doc;
    const auto outerVertices = verticesOf(doc, s.outer) + verticesOf(doc, s.inner);
    check(outerVertices == 16, "Outer and Inner store two boxes of eight vertices");
    const auto before = encodeDocument(doc);
    const auto beforeBytes = encodeContainer(doc);
    const auto beforeContent = content(doc);
    const auto revision = doc.revision();
    const auto preview = run(doc, purge({{"dryRun", true}}));
    check(encodeDocument(doc) == before && encodeContainer(doc) == beforeBytes &&
              doc.revision() == revision,
          "A dry run leaves the document byte-identical");
    check(removedIds(preview, "definitions") == ids({s.outer, s.inner}),
          "A dry run reports the two unused definitions");
    check(removedIds(preview, "materials") == ids({s.tile, s.glass, s.spare}),
          "A dry run reports the materials that the removals free");
    check(removedIds(preview, "assets") == ids({s.tileAsset, s.looseAsset}),
          "A dry run reports the assets that the removals free");
    const auto first = preview["purgeOperations"].toArray().at(0).toObject();
    check(first["dryRun"].toBool() &&
              first["definitions"].toObject()["uniqueVerticesFreed"].toDouble() == double(outerVertices),
          "A dry run reports the unique vertices it would free");
    const auto result = run(doc, purge());
    check(removedIds(result, "definitions") == ids({s.outer, s.inner}) &&
              removedIds(result, "materials") == ids({s.tile, s.glass, s.spare}) &&
              removedIds(result, "assets") == ids({s.tileAsset, s.looseAsset}),
          "The purge reports exactly what the dry run reported");
    const auto operation = result["purgeOperations"].toArray().at(0).toObject();
    check(!operation["dryRun"].toBool() &&
              operation["definitions"].toObject()["uniqueVerticesFreed"].toDouble() == double(outerVertices),
          "The purge reports the unique vertices it freed");
    std::set<std::string> names;
    for (const auto value : operation["definitions"].toObject()["removed"].toArray())
        names.insert(value.toObject()["name"].toString().toStdString());
    check(names == std::set<std::string>{"Outer", "Inner"}, "The report names each removed definition");
    check(doc.definitions().size() == 2 && doc.definitions().contains(s.post) &&
              doc.definitions().contains(s.room),
          "Placed and library-assembly definitions survive");
    check(doc.materials().size() == 2 && doc.materials().contains(s.oak) && doc.materials().contains(s.lib),
          "Materials still named by a placed body or a kept definition survive");
    check(doc.assets().size() == 1 && doc.assets().contains(s.oakAsset),
          "The asset of a kept material survives");
    check(doc.instances().size() == 2, "The placements are untouched");
    const auto purged = content(doc);
    doc.undo();
    check(content(doc) == beforeContent, "One undo restores every purged record");
    doc.redo();
    check(content(doc) == purged, "Redo removes the same records again");
    const auto reopened = decodeContainer(encodeContainer(doc));
    check(encodeDocument(reopened) == encodeDocument(doc),
          "A saved and reopened purge matches the purged model");
    check(reopened.definitions().size() == 2 && reopened.materials().size() == 2 &&
              reopened.assets().size() == 1,
          "The reopened model keeps the purged counts");
}
void purgeNothingToRemove() {
    auto s = build();
    auto &doc = s.doc;
    const auto before = content(doc);
    run(doc, purge());
    const auto after = encodeDocument(doc);
    const auto revision = doc.revision();
    const auto result = run(doc, purge());
    check(removedIds(result, "definitions").empty() && removedIds(result, "materials").empty() &&
              removedIds(result, "assets").empty(),
          "A purge with nothing to remove reports empty lists");
    check(doc.revision() == revision && encodeDocument(doc) == after,
          "A purge with nothing to remove changes nothing");
    // No history item: one undo returns to before the first purge, not to the purged state.
    doc.undo();
    check(content(doc) == before,
          "The empty purge added no history item: undo returns to before the first purge");
}
void categoryToggles() {
    {
        auto s = build();
        const auto result = run(s.doc, purge({{"materials", false}}));
        check(removedIds(result, "definitions") == ids({s.outer, s.inner}) &&
                  removedIds(result, "materials").empty() &&
                  removedIds(result, "assets") == ids({s.looseAsset}),
              "materials:false keeps the materials and the assets they name");
        check(s.doc.materials().contains(s.tile) && s.doc.assets().contains(s.tileAsset),
              "A kept material keeps its asset");
    }
    {
        auto s = build();
        const auto result = run(s.doc, purge({{"definitions", false}}));
        check(removedIds(result, "definitions").empty() &&
                  removedIds(result, "materials") == ids({s.spare}) &&
                  removedIds(result, "assets") == ids({s.looseAsset}),
              "definitions:false keeps the unused definitions and what they use");
        check(s.doc.definitions().contains(s.outer) && s.doc.definitions().contains(s.inner),
              "definitions:false keeps Outer and Inner");
        // The rule is material.delete's: a material that a kept definition names is refused.
        rejected(s.doc, {{"command", "material.delete"}, {"material", id(s.tile)}},
                 "material.delete refuses a material that a kept definition names");
        rejected(s.doc, {{"command", "material.delete"}, {"material", id(s.oak)}},
                 "material.delete refuses a material that a placed body names");
    }
    {
        auto s = build();
        const auto result = run(s.doc, purge({{"assets", false}}));
        check(removedIds(result, "definitions") == ids({s.outer, s.inner}) &&
                  removedIds(result, "materials") == ids({s.tile, s.glass, s.spare}) &&
                  removedIds(result, "assets").empty(),
              "assets:false keeps every asset");
        check(s.doc.assets().size() == 3, "Three assets remain");
    }
}
void keepsAssetsThatOtherRecordsName() {
    auto s = build();
    auto style = s.doc.style();
    EdgeStroke stroke;
    stroke.texture = s.looseAsset;
    style.edgeStroke = stroke;
    s.doc.setStyle(style);
    const auto result = run(s.doc, purge());
    check(removedIds(result, "assets") == ids({s.tileAsset}),
          "An edge stroke texture keeps its asset; validation would refuse to drop it");
    check(s.doc.assets().contains(s.looseAsset), "The edge stroke asset remains");
}
void boundsNames() {
    Document doc;
    const auto material = createMaterial(doc, std::string(100, 'x'), {.1f, .1f, .1f});
    const auto result = run(doc, purge());
    const auto name = result["purgeOperations"]
                          .toArray()
                          .at(0)
                          .toObject()["materials"]
                          .toObject()["removed"]
                          .toArray()
                          .at(0)
                          .toObject()["name"]
                          .toString();
    check(name.toUtf8().size() <= 64 && name.endsWith(QString::fromUtf8("\xE2\x80\xA6")),
          "A long name is truncated in the result");
    check(!doc.materials().contains(material), "The unused material is removed");
}
void rejectsBadOptions() {
    auto s = build();
    rejected(s.doc, purge({{"definitions", "yes"}}), "a non-boolean option");
}
void sessionRoundTrip() {
    QTemporaryDir files;
    check(files.isValid(), "Temporary session directory");
    const auto input = files.path() + "/input.sketchyup";
    const auto output = files.path() + "/output.sketchyup";
    const auto outcomes = files.path() + "/outcomes";
    auto s = build();
    saveDocument(s.doc, input);
    AutomationSession session({input, output, outcomes});
    const auto info = session.execute(
        QJsonObject{{"apiVersion", 1}, {"operation", "session.describe"}});
    const auto begun = session.execute(
        QJsonObject{{"apiVersion", 1},
                    {"operation", "transaction.begin"},
                    {"documentId", info["documentId"]},
                    {"expectedRevision", info["revision"]}});
    session.execute(QJsonObject{
        {"apiVersion", 1},
        {"operation", "transaction.apply"},
        {"documentId", info["documentId"]},
        {"transactionId", begun["transactionId"]},
        {"expectedVersion", 0},
        {"operationId", "remove-unused"},
        {"commands",
         QJsonArray{QJsonObject{{"command", "component.delete_definition"},
                                {"definition", id(s.outer)}},
                    QJsonObject{{"command", "document.purge_unused"}}}}});
    const auto sealed = session.execute(QJsonObject{{"apiVersion", 1},
                                                    {"operation", "transaction.preview"},
                                                    {"documentId", info["documentId"]},
                                                    {"transactionId", begun["transactionId"]},
                                                    {"expectedVersion", 1}});
    const auto receipt = session.execute(QJsonObject{{"apiVersion", 1},
                                                     {"operation", "transaction.commit"},
                                                     {"documentId", info["documentId"]},
                                                     {"requestId", sealed["requestId"]},
                                                     {"payloadHash", sealed["payloadHash"]}});
    check(receipt["status"] == "committed", "The session commits both commands");
    const auto next = QString::number(info["revision"].toString().toULongLong() + 1);
    const auto saved = session.execute(QJsonObject{{"apiVersion", 1},
                                                   {"operation", "document.save"},
                                                   {"documentId", info["documentId"]},
                                                   {"expectedRevision", next}});
    check(saved["status"] == "saved", "The session saves the purged model");
    const auto reopened = loadDocument(output);
    check(reopened.definitions().size() == 2 && reopened.definitions().contains(s.post) &&
              reopened.definitions().contains(s.room),
          "The saved model keeps the placed and assembly definitions only");
    check(reopened.materials().size() == 2 && reopened.assets().size() == 1,
          "The saved model keeps the materials and asset that remain in use");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        deleteUnusedDefinition();
        deleteRefusals();
        purgeMixedDocument();
        purgeNothingToRemove();
        categoryToggles();
        keepsAssetsThatOtherRecordsName();
        boundsNames();
        rejectsBadOptions();
        sessionRoundTrip();
        std::cout << "purge_unused_tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "purge_unused_tests failed: " << error.what() << '\n';
        return 1;
    }
}
