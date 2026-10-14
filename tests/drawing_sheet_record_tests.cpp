#include "core/annotation_anchors.hpp"
#include "core/annotations.hpp"
#include "core/assets.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/materials.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/tags.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <string>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation, const std::string &what) {
    try {
        operation();
    } catch (const ValidationOracleMismatch &) {
        throw;
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected drawing-sheet rejection: " + what);
}
template <class F> void rejectsWith(F operation, const std::string &expected) {
    try {
        operation();
    } catch (const ValidationOracleMismatch &) {
        throw;
    } catch (const std::exception &error) {
        check(std::string(error.what()) == expected,
              "Expected \"" + expected + "\", got \"" + error.what() + "\"");
        return;
    }
    throw std::runtime_error("Expected rejection: " + expected);
}
const std::string fingerprint(64, 'a');
SceneSnapshot cameraScene(double yaw = -45) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->yaw = yaw;
    snapshot.camera->orthographic = true;
    return snapshot;
}
DrawingViewRecord view(Id sheet, Id scene, std::string name = "Plan") {
    DrawingViewRecord record;
    record.name = std::move(name);
    record.sheet = sheet;
    record.source = {DrawingViewSourceKind::Scene, scene};
    record.frame = {20, 20, 200, 150};
    record.offsetMm = {5, -5};
    record.scaleDenominator = 50;
    record.hiddenLines = true;
    return record;
}
// Every model record table and model value, captured by pointer or by value.
struct ModelState {
    std::map<Id, BodyPtr> bodies;
    ComponentDefinitions definitions;
    ComponentInstances instances;
    HostedPtr hosted;
    TagRecords tags;
    MaterialRecords materials;
    AssetRecords assets;
    SceneRecords scenes;
    SectionRecords sections;
    ActiveSections activeSections;
    AnnotationRecords annotations;
    ModelStyle style;
    SolarSettings solar;
    DisplayUnit units{};
    int precision{};
    std::vector<Id> allocators;
    explicit ModelState(const Document &doc)
        : bodies(doc.bodies()), definitions(doc.definitions()), instances(doc.instances()),
          hosted(doc.hostedRecords()), tags(doc.tags()), materials(doc.materials()),
          assets(doc.assets()), scenes(doc.scenes()), sections(doc.sections()),
          activeSections(doc.activeSections()), annotations(doc.annotations()), style(doc.style()),
          solar(doc.solar()), units(doc.displayUnits()), precision(doc.displayPrecision()),
          allocators{doc.nextId(),         doc.nextDefinitionId(), doc.nextTagId(),
                     doc.nextMaterialId(), doc.nextAssetId(),      doc.nextSceneId(),
                     doc.nextSectionId(),  doc.nextAnnotationId()} {}
    // std::map<Id, shared_ptr> equality compares the pointers, so this is pointer identity.
    bool operator==(const ModelState &) const = default;
};
struct SheetState {
    DrawingSheetRecords sheets;
    DrawingViewRecords views;
    Id nextSheet{}, nextView{};
    explicit SheetState(const Document &doc)
        : sheets(doc.drawingSheets()), views(doc.drawingViews()),
          nextSheet(doc.nextDrawingSheetId()), nextView(doc.nextDrawingViewId()) {}
    bool operator==(const SheetState &) const = default;
};
// A sheet edit must leave every model record pointer-identical, including through
// its own Undo and Redo, and must publish exactly one labeled history entry.
void sheetOnly(Document &doc, const std::string &label, const std::function<void()> &operation) {
    const ModelState model(doc);
    const auto entries = doc.history().total;
    const auto revision = doc.revision();
    operation();
    check(doc.revision() == revision + 1, label + ": one revision");
    const auto page = doc.history(0, 1000);
    check(page.total == entries + 1 && page.entries.back().label == label,
          label + ": one labeled history entry");
    check(ModelState(doc) == model, label + ": model records are pointer-identical");
    const SheetState after(doc);
    doc.undo();
    check(ModelState(doc) == model, label + ": Undo keeps model records");
    doc.redo();
    check(ModelState(doc) == model && SheetState(doc) == after,
          label + ": Redo republishes the same records");
}
// A model edit must leave the drawing-sheet tables pointer-identical.
void modelOnly(Document &doc, const std::string &label, const std::function<void()> &operation) {
    const SheetState sheets(doc);
    operation();
    check(SheetState(doc) == sheets, label + ": sheet tables are unchanged");
    doc.undo();
    check(SheetState(doc) == sheets, label + ": Undo keeps sheet tables");
    doc.redo();
    check(SheetState(doc) == sheets, label + ": Redo keeps sheet tables");
}
void boundsAndNames() {
    Document doc;
    const auto scene = createScene(doc, "Plan", cameraScene());
    for (const auto paper :
         {DrawingPaper{9.999, 297}, DrawingPaper{420, 2000.001}, DrawingPaper{std::nan(""), 297},
          DrawingPaper{std::numeric_limits<double>::infinity(), 297}})
        rejects([&] { createDrawingSheet(doc, "Bad paper", paper); }, "paper bounds");
    const auto small = createDrawingSheet(doc, "Small", {10, 10});
    const auto large = createDrawingSheet(doc, "Large", {2000, 2000});
    check(orderedDrawingSheets(doc) == std::vector<Id>{small, large}, "Paper bounds inclusive");
    for (const auto *name : {"", " Lead", "Trail ", "Large", "bad\nname", "del\x7f"})
        rejects([&] { renameDrawingSheet(doc, small, name); }, "sheet name");
    rejects([&] { renameDrawingSheet(doc, small, std::string(1025, 'n')); }, "name length");
    renameDrawingSheet(doc, small, std::string(1024, 'n'));
    check(doc.drawingSheets().at(small)->name.size() == 1024, "1024-byte names are accepted");
    renameDrawingSheet(doc, small, "Small");

    auto good = view(large, scene);
    // A view may share a sheet's name: names are unique per table.
    good.name = "Large";
    const auto first = createDrawingView(doc, good);
    rejectsWith([&] { createDrawingView(doc, good); }, "Drawing view names must be unique");
    auto bad = view(large, scene, "Bad");
    auto variant = [&](const std::function<void(DrawingViewRecord &)> &mutate,
                       const std::string &what) {
        auto record = bad;
        mutate(record);
        rejects([&] { createDrawingView(doc, record); }, what);
    };
    variant([](auto &r) { r.name = ""; }, "empty view name");
    variant([](auto &r) { r.name = "x\t"; }, "control view name");
    variant([](auto &r) { r.frame.widthMm = 9.99; }, "frame minimum width");
    variant([](auto &r) { r.frame.heightMm = 9.99; }, "frame minimum height");
    variant([](auto &r) { r.frame.xMm = -0.001; }, "frame left of paper");
    variant([](auto &r) { r.frame.yMm = -0.001; }, "frame above paper");
    variant([](auto &r) { r.frame = {1900, 0, 100.001, 100}; }, "frame right of paper");
    variant([](auto &r) { r.frame = {0, 1900, 100, 100.001}; }, "frame below paper");
    variant([](auto &r) { r.frame.xMm = std::nan(""); }, "frame not finite");
    variant([](auto &r) { r.offsetMm[0] = 1e6 + 1; }, "offset bound");
    variant([](auto &r) { r.offsetMm[1] = std::nan(""); }, "offset finite");
    variant([](auto &r) { r.scaleDenominator = .0009; }, "scale lower bound");
    variant([](auto &r) { r.scaleDenominator = 1.1e9; }, "scale upper bound");
    variant([](auto &r) { r.scaleDenominator = std::nan(""); }, "scale finite");
    variant([](auto &r) { r.content = static_cast<DrawingViewContent>(7); }, "content mode");
    variant([](auto &r) { r.source.kind = static_cast<DrawingViewSourceKind>(3); }, "source kind");
    variant([](auto &r) { r.source.scene = 0; }, "source scene zero");
    variant([](auto &r) { r.sheet = 999; }, "missing sheet");
    variant([](auto &r) { r.accepted = DrawingViewAcceptance{1, std::string(63, 'a')}; },
            "short fingerprint");
    variant([](auto &r) { r.accepted = DrawingViewAcceptance{1, std::string(64, 'A')}; },
            "uppercase fingerprint");
    variant([](auto &r) { r.accepted = DrawingViewAcceptance{1, std::string(64, 'g')}; },
            "non-hex fingerprint");
    // Exact bounds are accepted.
    auto edge = view(large, scene, "Edge");
    edge.frame = {0, 0, 2000, 2000};
    edge.offsetMm = {-1e6, 1e6};
    edge.scaleDenominator = .001;
    edge.content = DrawingViewContent::RasterAppearance;
    edge.accepted = DrawingViewAcceptance{doc.revision(), fingerprint};
    const auto bounded = createDrawingView(doc, edge);
    edge.scaleDenominator = 1e9;
    updateDrawingView(doc, bounded, edge);
    // Frame inside paper is enforced against the view's own sheet.
    auto onSmall = view(small, scene, "Tiny");
    onSmall.frame = {0, 0, 10, 10};
    const auto tiny = createDrawingView(doc, onSmall);
    rejects([&] { moveDrawingView(doc, first, small); }, "moved frame must fit");
    rejects([&] { resizeDrawingSheet(doc, large, {1000, 1000}); }, "resize must fit views");
    resizeDrawingSheet(doc, small, {20, 10});
    rejects([&] { resizeDrawingSheet(doc, small, {20, 9}); }, "paper bound on resize");
    check(drawingViewsOnSheet(doc, small) == std::vector<Id>{tiny} &&
              drawingViewsOnSheet(doc, large) == std::vector<Id>{first, bounded},
          "Views by sheet");
}
void limits() {
    Document doc;
    const auto scene = createScene(doc, "Plan", cameraScene());
    std::vector<Id> sheets;
    for (size_t i = 0; i < drawingSheetCountLimit; ++i)
        sheets.push_back(createDrawingSheet(doc, "Sheet " + std::to_string(i), {}));
    rejectsWith([&] { createDrawingSheet(doc, "One too many", {}); },
                "Invalid drawing sheet allocator or count");
    size_t views = 0;
    for (size_t s = 0; views < drawingViewCountLimit; ++s)
        for (size_t i = 0; i < drawingViewsPerSheetLimit; ++i) {
            auto record = view(sheets[s], scene, "View " + std::to_string(views++));
            createDrawingView(doc, record);
        }
    rejectsWith([&] { createDrawingView(doc, view(sheets.back(), scene, "Extra")); },
                "Invalid drawing sheet allocator or count");
    eraseDrawingView(doc, doc.drawingViews().rbegin()->first);
    rejectsWith([&] { createDrawingView(doc, view(sheets.front(), scene, "Crowded")); },
                "A drawing sheet allows at most 64 views");
    createDrawingView(doc, view(sheets.back(), scene, "Fits"));
    check(doc.drawingSheets().size() == 256 && doc.drawingViews().size() == 1024,
          "Count limits are inclusive");
    // Retained-memory accounting charges every record.
    size_t charged = 0;
    for (const auto &[id, sheet] : doc.drawingSheets())
        charged += drawingSheetBytes(sheet);
    for (const auto &[id, record] : doc.drawingViews())
        charged += drawingViewBytes(record);
    check(charged < drawingSheetBytesLimit, "Full tables fit the eight MiB budget");
    Document empty;
    check(doc.readSnapshotBytes() >= empty.readSnapshotBytes() + charged,
          "Snapshot accounting charges drawing sheets");
}
void identitiesHistoryAndAllocators() {
    Document doc;
    const auto scene = createScene(doc, "Plan", cameraScene());
    doc.markSaved();
    const auto saved = doc.saveStamp();
    const auto historyBytes = doc.historyBytes();
    const auto a = createDrawingSheet(doc, "A1", {841, 594});
    check(doc.dirty() && doc.historyBytes() > historyBytes + sizeof(DrawingSheetRecord),
          "Sheet edits dirty the document and are charged to history");
    doc.undo();
    check(doc.drawingSheets().empty() && doc.isCurrentSnapshot(saved),
          "Undo returns to the saved state");
    check(doc.nextDrawingSheetId() == a + 1, "Undo does not rewind the sheet allocator");
    const auto b = createDrawingSheet(doc, "A1", {841, 594});
    check(b == a + 1, "A retired sheet ID is never reused");
    check(!doc.canRedo(), "A new edit clears Redo");
    const auto c = createDrawingSheet(doc, "A3", {420, 297});
    const auto v1 = createDrawingView(doc, view(b, scene, "Plan"));
    const auto v2 = createDrawingView(doc, view(c, scene, "Elevation"));
    check(v2 == v1 + 1, "View IDs are document-global across sheets");
    doc.undo();
    check(doc.nextDrawingViewId() == v2 + 1, "Undo does not rewind the view allocator");
    const auto v3 = createDrawingView(doc, view(c, scene, "Elevation"));
    check(v3 == v2 + 1, "A retired view ID is never reused");
    // Direct edits cannot reuse retired IDs either.
    auto reuse = std::make_shared<DrawingViewRecord>(view(c, scene, "Reuse"));
    reuse->id = v2;
    Edit reused{"Reuse", {}};
    reused.drawingSheetScope = true;
    reused.drawingViews.push_back({v2, nullptr, reuse});
    rejectsWith([&] { doc.apply(reused, doc.revision()); },
                "Retired drawing view ID cannot be reused");
    auto reuseSheet = std::make_shared<DrawingSheetRecord>(*doc.drawingSheets().at(b));
    reuseSheet->id = a;
    reuseSheet->name = "Reuse";
    reuseSheet->position = 2;
    Edit reusedSheet{"Reuse", {}};
    reusedSheet.drawingSheets.push_back({a, nullptr, reuseSheet});
    rejectsWith([&] { doc.apply(reusedSheet, doc.revision()); },
                "Retired drawing sheet ID cannot be reused");
    // Moving a view between sheets keeps its identity; Undo moves it back.
    const auto before = doc.drawingViews().at(v1);
    moveDrawingView(doc, v1, c);
    check(doc.drawingViews().at(v1)->sheet == c && doc.drawingViews().at(v1)->name == "Plan" &&
              drawingViewsOnSheet(doc, c) == std::vector<Id>{v1, v3} &&
              drawingViewsOnSheet(doc, b).empty(),
          "Moving keeps the view ID and record");
    doc.undo();
    check(doc.drawingViews().at(v1) == before, "Undo restores the original record");
    doc.redo();
    // Equal operations are no-ops.
    const auto revision = doc.revision();
    renameDrawingSheet(doc, b, "A1");
    resizeDrawingSheet(doc, b, {841, 594});
    reorderDrawingSheets(doc, orderedDrawingSheets(doc));
    moveDrawingView(doc, v1, c);
    updateDrawingView(doc, v1, *doc.drawingViews().at(v1));
    acceptDrawingViews(doc, {});
    check(doc.revision() == revision, "Equal operations do not publish history");
    rejects([&] { reorderDrawingSheets(doc, {b, b}); }, "duplicate order");
    rejects([&] { reorderDrawingSheets(doc, {b}); }, "short order");
    rejects([&] { reorderDrawingSheets(doc, {b, 999}); }, "unknown order");
    rejects([&] { renameDrawingSheet(doc, 999, "Missing"); }, "missing sheet");
    rejects([&] { moveDrawingView(doc, v1, 999); }, "move to missing sheet");
    rejects([&] { eraseDrawingView(doc, 999); }, "missing view");
    check(doc.revision() == revision, "Rejected operations leave state untouched");
    // Reorder and delete keep contiguous order; deleting a sheet deletes its views.
    reorderDrawingSheets(doc, {c, b});
    check(orderedDrawingSheets(doc) == std::vector<Id>{c, b}, "Explicit sheet order");
    eraseDrawingSheet(doc, c);
    check(orderedDrawingSheets(doc) == std::vector<Id>{b} && doc.drawingViews().empty(),
          "Deleting a sheet compacts order and removes its views");
    doc.undo();
    check(orderedDrawingSheets(doc) == std::vector<Id>{c, b} && doc.drawingViews().size() == 2,
          "Undo restores the sheet and its views");
    // Accepting stamps views in one entry.
    acceptDrawingViews(doc,
                       {{v1, {doc.revision(), fingerprint}}, {v3, {doc.revision(), fingerprint}}});
    check(doc.history().entries.back().label == "Accept sheet views" &&
              doc.drawingViews().at(v1)->accepted->fingerprint == fingerprint,
          "Accept all is one labeled entry");
    // readSnapshot shares records and allocators without history.
    const auto snapshot = doc.readSnapshot();
    check(snapshot.drawingSheets() == doc.drawingSheets() &&
              snapshot.drawingViews() == doc.drawingViews() &&
              snapshot.nextDrawingSheetId() == doc.nextDrawingSheetId() &&
              snapshot.nextDrawingViewId() == doc.nextDrawingViewId() && !snapshot.canUndo() &&
              snapshot.readSnapshotBytes() == doc.readSnapshotBytes(),
          "readSnapshot shares drawing-sheet records");
    // Records are frozen copies, so aliases cannot mutate the document.
    auto alias = std::make_shared<DrawingSheetRecord>(*doc.drawingSheets().at(b));
    alias->name = "Frozen";
    Edit frozen{"Freeze sheet", {}};
    frozen.drawingSheets.push_back({b, doc.drawingSheets().at(b), alias});
    doc.apply(frozen, doc.revision());
    alias->name = "Mutated alias";
    check(doc.drawingSheets().at(b)->name == "Frozen", "Applied records are frozen copies");
    // Restore replaces the whole document; sheets are not persisted until schema 27.
    Document restored = doc;
    restored.restore(doc.identity(), doc.nextId(), doc.bodies());
    check(restored.drawingSheets().empty() && restored.drawingViews().empty() &&
              restored.nextDrawingSheetId() == 1 && restored.nextDrawingViewId() == 1,
          "Restore starts with empty drawing-sheet tables");
}
void sceneLinks() {
    Document doc;
    const auto scene = createScene(doc, "Plan", cameraScene());
    const auto other = createScene(doc, "Front", cameraScene(0));
    const auto sheet = createDrawingSheet(doc, "Sheet", {});
    rejectsWith([&] { createDrawingView(doc, view(sheet, 999)); },
                "New drawing view sources require an existing scene");
    const auto linked = createDrawingView(doc, view(sheet, scene));
    const auto record = doc.drawingViews().at(linked);
    eraseScene(doc, scene);
    check(doc.drawingViews().at(linked) == record &&
              drawingViewsWithMissingSource(doc) == std::set<Id>{linked},
          "Deleting a scene keeps the view record as a diagnosed broken view");
    // A broken view can still be renamed, moved and accepted.
    auto renamed = *record;
    renamed.name = "Broken plan";
    updateDrawingView(doc, linked, renamed);
    const auto second = createDrawingSheet(doc, "Second", {});
    moveDrawingView(doc, linked, second);
    acceptDrawingViews(doc, {{linked, {doc.revision(), fingerprint}}});
    // Relinking requires an existing scene.
    auto relinked = *doc.drawingViews().at(linked);
    auto missing = relinked;
    missing.source.scene = 999;
    rejectsWith([&] { updateDrawingView(doc, linked, missing); },
                "New drawing view sources require an existing scene");
    relinked.source.scene = other;
    updateDrawingView(doc, linked, relinked);
    check(drawingViewsWithMissingSource(doc).empty(), "Relinking repairs the view");
    doc.undo();
    // Undoing the scene deletion relinks the view by its stable scene ID.
    while (doc.scenes().size() < 2 && doc.canUndo())
        doc.undo();
    check(doc.scenes().contains(scene) && doc.drawingViews().contains(linked) &&
              drawingViewsWithMissingSource(doc).empty(),
          "Undoing the scene deletion relinks the view");
    // A single edit cannot delete a scene and link a view to it.
    doc.redo();
    check(!doc.scenes().contains(scene), "Scene deleted again");
    rejects([&] { createDrawingView(doc, view(sheet, scene, "Late")); }, "deleted scene source");
}
void enforcement() {
    Document doc;
    // A model with every record family.
    const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
    const auto wire = doc.addWire(0, {0, 5, 0}, {4, 5, 0});
    const auto edge = doc.bodies().at(wire)->topology.edges.begin()->first;
    const auto component =
        createComponent(doc, doc.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}}), "Panel");
    const auto tag = createTag(doc, "Walls");
    assignTag(doc, body, tag);
    const auto asset = createAsset(doc, "Texture", "image/png",
                                   std::make_shared<AssetPayload>(std::vector<std::uint8_t>{1, 2}));
    const auto material = createMaterial(doc, "Paint", {1, 0, 0}, 1, asset);
    assignMaterial(doc, body, {}, material);
    const auto section = createSection(doc, "Cut", 0, {{1, 0, 0}, -1});
    setActiveSection(doc, 0, section);
    AnnotationRecord note;
    note.name = "Width";
    note.text = "Width";
    note.anchors = {edgeAnchor(doc, wire, edge, .25), edgeAnchor(doc, wire, edge, .75)};
    note.offset = {0, 1, 0};
    createAnnotation(doc, note);
    const auto scene = createScene(doc, "Plan", cameraScene());
    check(!doc.definitions().empty() && !doc.instances().empty() && !doc.tags().empty() &&
              !doc.materials().empty() && !doc.assets().empty() && !doc.sections().empty() &&
              !doc.annotations().empty() && !doc.activeSections().empty(),
          "Fixture covers every model record family");

    // Pointer identity of model records under every sheet edit.
    Id sheet{}, second{}, linked{};
    sheetOnly(doc, "Create sheet", [&] { sheet = createDrawingSheet(doc, "Sheet", {}); });
    sheetOnly(doc, "Create sheet", [&] { second = createDrawingSheet(doc, "Second", {}); });
    sheetOnly(doc, "Rename sheet", [&] { renameDrawingSheet(doc, second, "Details"); });
    sheetOnly(doc, "Resize sheet", [&] { resizeDrawingSheet(doc, second, {594, 420}); });
    sheetOnly(doc, "Reorder sheets", [&] { reorderDrawingSheets(doc, {second, sheet}); });
    sheetOnly(doc, "Create sheet view",
              [&] { linked = createDrawingView(doc, view(sheet, scene)); });
    sheetOnly(doc, "Update sheet view", [&] {
        auto changed = *doc.drawingViews().at(linked);
        changed.scaleDenominator = 20;
        changed.content = DrawingViewContent::RasterAppearance;
        updateDrawingView(doc, linked, changed);
    });
    sheetOnly(doc, "Move sheet view", [&] { moveDrawingView(doc, linked, second); });
    sheetOnly(doc, "Accept sheet view",
              [&] { acceptDrawingViews(doc, {{linked, {doc.revision(), fingerprint}}}); });
    sheetOnly(doc, "Delete sheet view", [&] { eraseDrawingView(doc, linked); });
    sheetOnly(doc, "Delete sheet", [&] { eraseDrawingSheet(doc, second); });
    doc.undo();
    doc.undo();
    check(doc.drawingViews().contains(linked) && doc.drawingSheets().contains(second),
          "Fixture views restored");

    // The reverse: geometry, scene and other model edits leave the sheet tables identical.
    modelOnly(doc, "Move body", [&] { doc.move(body, {1, 0, 0}); });
    modelOnly(doc, "Add face", [&] { doc.addFace({{{9, 9, 0}, {10, 9, 0}, {10, 10, 0}}}); });
    modelOnly(doc, "Update scene", [&] { updateScene(doc, scene, cameraScene(30)); });
    modelOnly(doc, "Rename scene", [&] { renameScene(doc, scene, "Ground plan"); });
    modelOnly(doc, "Edit tag", [&] { editTag(doc, tag, "Partitions"); });
    modelOnly(doc, "Section", [&] { setActiveSection(doc, 0, std::nullopt); });
    modelOnly(doc, "Component", [&] {
        editComponentDefinition(doc, component.definition, [&](Document &draft) {
            const auto member = component.movedGeometry.at(component.instance);
            draft.transform(member, Transform::scaling({2, 1, 1}),
                            draft.bodies().at(member)->parent);
            return ChangeReport{};
        });
    });
    modelOnly(doc, "Delete scene", [&] { eraseScene(doc, scene); });
    doc.undo();

    // The sheet-only predicate.
    Edit pure{"Pure", {}};
    check(!isDrawingSheetOnlyEdit(pure), "An empty edit is not a sheet edit");
    auto renamed = std::make_shared<DrawingSheetRecord>(*doc.drawingSheets().at(sheet));
    renamed->name = "Renamed";
    pure.drawingSheets.push_back({sheet, doc.drawingSheets().at(sheet), renamed});
    check(isDrawingSheetOnlyEdit(pure), "Sheet changes alone are sheet-only");
    auto withModel = [&](const std::function<void(Edit &)> &mutate, const std::string &what) {
        auto mixed = pure;
        mutate(mixed);
        check(!isDrawingSheetOnlyEdit(mixed), what + " is not sheet-only");
        rejectsWith([&] { doc.apply(mixed, doc.revision()); },
                    "Drawing sheet edits cannot change model records");
    };
    const auto oldBody = doc.bodies().at(body);
    auto movedBody = std::make_shared<Body>(*oldBody);
    movedBody->hidden = !movedBody->hidden;
    withModel([&](Edit &e) { e.changes.push_back({body, oldBody, movedBody}); }, "Body change");
    withModel(
        [&](Edit &e) {
            auto t = std::make_shared<TagRecord>(*doc.tags().at(tag));
            t->visible = !t->visible;
            e.tags.push_back({tag, doc.tags().at(tag), t});
        },
        "Tag change");
    withModel(
        [&](Edit &e) {
            auto s = std::make_shared<SceneRecord>(*doc.scenes().at(scene));
            s->name = "Other";
            e.scenes.push_back({scene, doc.scenes().at(scene), s});
        },
        "Scene change");
    withModel(
        [&](Edit &e) {
            auto style = doc.style();
            style.profiles = !style.profiles;
            e.style = std::pair{doc.style(), style};
        },
        "Style change");
    withModel([&](Edit &e) { e.displayPrecision = std::pair{doc.displayPrecision(), 2}; },
              "Precision change");
    withModel(
        [&](Edit &e) {
            e.activeSections = std::pair{doc.activeSections(), ActiveSections{{0, section}}};
        },
        "Section activation");
    // Floors may restate but not raise model allocators.
    auto restated = pure;
    restated.nextIdFloor = doc.nextId();
    restated.nextSceneFloor = doc.nextSceneId();
    restated.drawingSheetScope = true;
    check(isDrawingSheetOnlyEdit(restated), "Floors are not change vectors");
    auto raised = restated;
    raised.nextIdFloor = doc.nextId() + 1;
    rejectsWith([&] { doc.apply(raised, doc.revision()); },
                "Drawing sheet edits cannot change model records");
    raised = restated;
    raised.nextSceneFloor = doc.nextSceneId() + 1;
    rejectsWith([&] { doc.apply(raised, doc.revision()); },
                "Drawing sheet edits cannot change model records");
    // An edit tagged with the sheet scope cannot carry only model changes.
    Edit tagged{"Tagged", {}};
    tagged.drawingSheetScope = true;
    tagged.changes.push_back({body, oldBody, movedBody});
    rejectsWith([&] { doc.apply(tagged, doc.revision()); },
                "Drawing sheet edits cannot change model records");
    const ModelState model(doc);
    doc.apply(restated, doc.revision());
    check(ModelState(doc) == model && doc.drawingSheets().at(sheet)->name == "Renamed",
          "A restated-floor sheet edit applies");

    // Component edit scopes reject sheet operations, as they reject scene operations.
    const auto revision = doc.revision();
    rejectsWith(
        [&] {
            editComponentDefinition(doc, component.definition, [&](Document &draft) {
                createDrawingSheet(draft, "Inside component", {});
                return ChangeReport{};
            });
        },
        "Shared component edits cannot modify drawing sheets");
    rejectsWith(
        [&] {
            editComponentDefinition(doc, component.definition, [&](Document &draft) {
                createScene(draft, "Inside component", cameraScene());
                return ChangeReport{};
            });
        },
        "Shared component edits cannot modify document scenes");
    check(doc.revision() == revision, "Rejected component scopes publish nothing");
}
void preparedAndAmend() {
    Document doc;
    const auto scene = createScene(doc, "Plan", cameraScene());
    const auto sheet = createDrawingSheet(doc, "Sheet", {});
    const auto other = createDrawingSheet(doc, "Other", {});
    // Prepared edits stage a sheet edit on a private copy and publish it once.
    const auto prepared = doc.prepareEdit(
        [&](Document &candidate) { createDrawingView(candidate, view(sheet, scene)); });
    check(doc.drawingViews().empty() && prepared.snapshot().drawingViews().size() == 1 &&
              prepared.retainedBytes() > prepared.snapshot().readSnapshotBytes(),
          "Prepared sheet edits stay private and are charged");
    const ModelState model(doc);
    doc.applyPrepared(prepared);
    check(doc.drawingViews().size() == 1 && ModelState(doc) == model &&
              doc.history().entries.back().label == "Create sheet view",
          "Prepared sheet edits publish one entry");
    const auto stale = doc.prepareEdit(
        [&](Document &candidate) { renameDrawingSheet(candidate, sheet, "Stale"); });
    renameDrawingSheet(doc, other, "Moved on");
    rejects([&] { doc.applyPrepared(stale); }, "stale proposal");
    // Amending a sheet edit replaces it in place, within its own records.
    resizeDrawingSheet(doc, other, {594, 420});
    const auto entries = doc.history().total;
    const auto stamp = doc.amendmentStamp();
    doc.amendLast(stamp, [&](Document &staged) { resizeDrawingSheet(staged, other, {841, 594}); });
    check(doc.history().total == entries &&
              doc.drawingSheets().at(other)->paper == DrawingPaper{841, 594} &&
              doc.history().entries.back().label == "Resize sheet",
          "Amending revises the sheet edit in place");
    rejectsWith(
        [&] {
            doc.amendLast(doc.amendmentStamp(),
                          [&](Document &staged) { resizeDrawingSheet(staged, sheet, {841, 594}); });
        },
        "Replacement cannot change another drawing sheet");
    rejectsWith(
        [&] {
            doc.amendLast(doc.amendmentStamp(),
                          [&](Document &staged) { createDrawingSheet(staged, "Extra", {}); });
        },
        "Replacement must preserve drawing sheet creation count");
    const auto view2 = createDrawingView(doc, view(other, scene, "Detail"));
    rejectsWith(
        [&] {
            doc.amendLast(doc.amendmentStamp(), [&](Document &staged) {
                auto record = view(other, scene, "Detail");
                createDrawingView(staged, record);
                auto first = *staged.drawingViews().begin()->second;
                first.name = "Changed";
                updateDrawingView(staged, first.id, first);
            });
        },
        "Replacement must commit exactly one atomic operation");
    doc.amendLast(doc.amendmentStamp(), [&](Document &staged) {
        auto record = view(other, scene, "Detail");
        record.scaleDenominator = 25;
        createDrawingView(staged, record);
    });
    check(!doc.drawingViews().contains(view2) &&
              doc.drawingViews().rbegin()->second->scaleDenominator == 25 &&
              doc.nextDrawingViewId() > view2 + 1,
          "Amended creation keeps the allocator monotonic");
    // Shared history navigation crosses sheet and model entries.
    doc.navigateHistory(0, doc.revision());
    check(doc.drawingSheets().empty() && doc.scenes().empty(),
          "History navigation unwinds sheet and scene entries together");
    doc.navigateHistory(doc.history().total, doc.revision());
    check(doc.drawingSheets().size() == 2 && doc.drawingViews().size() == 2,
          "History navigation replays them");
}
} // namespace
int main() {
    try {
        Document::setFullValidationOracle(true);
        boundsAndNames();
        limits();
        identitiesHistoryAndAllocators();
        sceneLinks();
        enforcement();
        preparedAndAmend();
        check(Document::fullValidationOracle(), "Full validation oracle stayed on");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "drawing sheet record tests passed\n";
}
