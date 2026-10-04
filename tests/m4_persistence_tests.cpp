#include "core/components.hpp"
#include "core/entity_measure.hpp"
#include "core/groups.hpp"
#include "core/materials.hpp"
#include "core/tags.hpp"
#include "io/assets.hpp"
#include "io/document_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Document fixture() {
    Document doc;
    const auto image = createAsset(doc, "Pattern.bin", "application/octet-stream",
                                   assetPayload("owned pattern bytes"));
    const auto missing = createAsset(doc, "Missing image.png", "image/png");
    const auto tile = createMaterial(doc, "Tile", {.7f, .3f, .2f}, 1, image);
    const auto glass = createMaterial(doc, "Glass", {.2f, .4f, .8f}, .35f, missing);
    const auto folder = createTag(doc, "Architecture", 0, true);
    const auto tag = createTag(doc, "Windows", folder);
    const auto panel = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}}, "Panel");
    doc.extrude(panel, doc.bodies().at(panel)->surface.faces.begin()->first, .25);
    assignMaterial(doc, panel, {}, tile);
    const auto face = doc.bodies().at(panel)->surface.faces.rbegin()->first;
    assignMaterial(doc, panel, face, glass, false, true);
    assignTag(doc, panel, tag);
    setEntityProperties(doc, panel,
                        {{"serial", 42.}, {"operable", true}, {"purpose", std::string("window")}});
    doc.addGuide(panel, guidePoint({1, 1.5, .25}));
    const auto circleChanges =
        doc.addCurve(0, centerCurve(CurveKind::Circle, {}, .5, 0, 2 * std::numbers::pi, 24));
    const auto circle = circleChanges.begin()->first;
    renameEntity(doc, circle, "Round opening");
    doc.transform(circle, Transform::translation({1, 1.5, .5}));
    const auto group = createGroup(doc, {panel, circle}, "Window assembly");
    const auto component = createComponent(doc, group, "Window");
    const auto mirror = placeComponent(
        doc, component.definition,
        Transform::translation({8, 0, 1}) * Transform::scaling({-2, 1, .5}), 0, "Mirrored window");
    const auto room = createGroup(doc, {component.instance, mirror.instance}, "Room");
    doc.transform(room, Transform::translation({10, 20, 0}) * Transform::rotation({0, 0, 1}, .25));
    setEntityState(doc, mirror.instance, true, {});
    // An unused canonical definition still owns its material references and geometry.
    const auto unused = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, "Unused sample");
    assignMaterial(doc, unused, {}, glass);
    const auto stored = createComponent(doc, unused, "Unplaced sample");
    Selection selection;
    selection.apply(doc, {{stored.instance, SelectionKind::Body, 0}}, SelectionMode::Replace);
    eraseSelected(doc, selection);
    // Retired identities must remain retired through serialization.
    const auto retiredBody = doc.addWire(0, {20, 0, 0}, {21, 0, 0});
    doc.erase(retiredBody);
    const auto retiredMaterial = createMaterial(doc, "Retired", {1, 1, 1});
    eraseMaterial(doc, retiredMaterial);
    const auto retiredAsset = createAsset(doc, "Retired", "image/png");
    eraseAsset(doc, retiredAsset);
    const auto retiredTag = createTag(doc, "Retired");
    eraseTag(doc, retiredTag);
    editTag(doc, folder, {}, {}, false);
    setEntityState(doc, room, {}, true);
    return doc;
}
void verify(const Document &doc) {
    check(doc.definitions().size() == 2 && doc.instances().size() == 2,
          "Placed and unplaced canonical definitions survive");
    check(doc.materials().size() == 2 && doc.assets().size() == 2 && !doc.assets().at(2)->payload &&
              assetByteArray(doc.assets().at(1)->payload) == "owned pattern bytes",
          "Embedded/missing assets and bindings survive");
    check(doc.materials().at(1)->asset == 1 && doc.materials().at(2)->asset == 2 &&
              doc.materials().at(2)->opacity == .35f,
          "Swatch opacity and asset ownership survive");
    check(doc.tags().size() == 2 && !doc.tags().at(1)->visible && doc.tags().at(2)->parent == 1,
          "Tag folder ownership and visibility survive");
    check(doc.nextMaterialId() == 4 && doc.nextAssetId() == 4 && doc.nextTagId() == 4 &&
              doc.nextId() > doc.bodies().rbegin()->first + 1,
          "Retired scene/metadata IDs do not collapse on save");
    size_t curves = 0, guides = 0, panels = 0, mirrors = 0, locks = 0;
    for (const auto &[id, body] : doc.bodies()) {
        curves += body->curves.size();
        guides += body->guides.size();
        locks += body->locked;
        if (body->name == "Panel") {
            ++panels;
            mirrors += doc.worldTransform(id).determinant() < 0;
            check(std::get<double>(body->properties.at("serial")) == 42 &&
                      std::get<bool>(body->properties.at("operable")) && body->tag == 2 &&
                      body->materials == MaterialSides{1, 1} && body->faceMaterials.size() == 1,
                  "Component member semantic fields, tags and oriented overrides survive");
        }
    }
    check(curves == 2 && guides == 2 && panels == 2 && mirrors == 1 && locks == 1,
          "Curve/guide records, mirrors, shared geometry and persistent locks survive");
    auto first = doc.instances().begin(), second = std::next(first);
    check(first->second->definition == second->second->definition,
          "Placements retain shared definition identity");
    for (const auto &[canonical, scene] : first->second->members)
        check(second->second->members.contains(canonical) && doc.bodies().contains(scene),
              "Canonical-to-scene member identity maps remain complete");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        if (app.arguments().size() == 3 && app.arguments()[1] == "--write-fixture") {
            auto doc = fixture();
            verify(doc);
            saveDocument(doc, app.arguments()[2]);
            return 0;
        }
        auto doc = loadDocument(QString(SOURCE_DIR) + "/tests/fixtures/m4-complete-v11.sketchyup");
        verify(doc);
        const auto golden = encodeContainer(doc);
        QTemporaryDir files;
        QDir(files.path()).mkdir("moved");
        saveDocument(doc, files.filePath("original.sketchyup"));
        check(QFile::copy(files.filePath("original.sketchyup"),
                          files.filePath("moved/copy.sketchyup")) &&
                  QFile::remove(files.filePath("original.sketchyup")),
              "Relocate complete M4 file and remove original");
        auto relocated = loadDocument(files.filePath("moved/copy.sketchyup"));
        verify(relocated);
        check(encodeContainer(relocated) == golden && !relocated.dirty() && !relocated.canUndo(),
              "Relocated complete M4 snapshot roundtrips exactly without invented history");
        const auto captured = captureSave(relocated);
        replaceAsset(relocated, 1, assetPayload("replacement"));
        saveSnapshot(relocated, captured, files.filePath("snapshot.sketchyup"));
        check(relocated.dirty(), "Saving captured graph does not mark a newer edit saved");
        verify(loadDocument(files.filePath("snapshot.sketchyup")));
        relocated.undo();
        for (const auto &[id, body] : relocated.bodies())
            if (body->locked) {
                setEntityState(relocated, id, {}, false);
                break;
            }
        const auto definition = relocated.instances().begin()->second->definition;
        Id panel = 0;
        for (const auto &[id, body] : relocated.definitions().at(definition)->members)
            if (body->name == "Panel")
                panel = id;
        editComponentDefinition(relocated, definition, [&](Document &draft) {
            return assignMaterial(draft, panel, {}, 2);
        });
        for (const auto &[id, body] : relocated.bodies())
            if (body->name == "Panel")
                check(body->materials == MaterialSides{2, 2} && body->faceMaterials.empty(),
                      "Reopened component remains shared and editable");
        relocated.undo();
        saveDocument(relocated, files.filePath("edited.sketchyup"));
        check(encodeDocument(loadDocument(files.filePath("edited.sketchyup"))) ==
                  encodeDocument(relocated),
              "Post-import shared edit and undo remain persistable");
        std::cout << "Complete M4 hierarchy, canonical identities, tags, curves, guides, "
                     "materials, assets, allocator floors, snapshots and shared edits passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
