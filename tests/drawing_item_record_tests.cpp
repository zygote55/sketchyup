// R091.a: drawing-sheet page items and the sheet number and margin (ADR 0164 §3).
#include "core/annotation_anchors.hpp"
#include "core/annotations.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/tags.hpp"
#include <algorithm>
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
    throw std::runtime_error("Expected drawing-item rejection: " + what);
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
const double nan = std::numeric_limits<double>::quiet_NaN();
const double inf = std::numeric_limits<double>::infinity();
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
    record.scaleDenominator = 50;
    return record;
}
// A model with one face, for vertex and edge anchors.
struct Fixture {
    Document doc;
    Id body{}, scene{}, sheet{}, plan{};
    AnnotationAnchor vertex, edge;
    Fixture() {
        body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        const auto &record = *doc.bodies().at(body);
        vertex = vertexAnchor(doc, body, record.surface.vertices.begin()->first);
        edge = edgeAnchor(doc, body, record.topology.edges.begin()->first, .5);
        scene = createScene(doc, "Plan", cameraScene());
        sheet = createDrawingSheet(doc, "Ground floor", {420, 297});
        plan = createDrawingView(doc, view(sheet, scene));
    }
};
DrawingTextItem text() {
    DrawingTextItem item;
    item.frame = {10, 10, 80, 20};
    item.text = "General notes";
    return item;
}
DrawingLabelItem paperLabel() {
    DrawingLabelItem item;
    item.text = "North";
    item.textAt = {300, 30};
    item.target.pointMm = {320, 40};
    return item;
}
DrawingLabelItem viewLabel(Id view, const AnnotationAnchor &anchor) {
    DrawingLabelItem item;
    item.text = "W2";
    item.textAt = {250, 60};
    item.target.kind = DrawingLabelTargetKind::View;
    item.target.view = view;
    item.target.anchor = anchor;
    item.target.lastMm = {120, 90};
    return item;
}
DrawingDimensionItem dimension(Id view, const AnnotationAnchor &a, const AnnotationAnchor &b) {
    DrawingDimensionItem item;
    item.view = view;
    item.anchors = {a, b};
    item.offsetMm = -8;
    item.lastMm = {DrawingPointMm{60, 120}, DrawingPointMm{100, 120}};
    return item;
}
DrawingLineItem line() {
    DrawingLineItem item;
    item.from = {10, 280};
    item.to = {410, 280};
    return item;
}
DrawingRectangleItem rectangle() {
    DrawingRectangleItem item;
    item.frame = {10, 10, 400, 277};
    item.dash = DrawingStrokeDash::Dashed;
    return item;
}
DrawingTitleBlockItem titleBlock() {
    DrawingTitleBlockItem item;
    item.frame = {250, 240, 160, 47};
    item.fields = {"House", "Ground floor plan", "2026-10-14", "B"};
    return item;
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
    DrawingItemRecords items;
    Id nextSheet{}, nextView{}, nextItem{};
    explicit SheetState(const Document &doc)
        : sheets(doc.drawingSheets()), views(doc.drawingViews()), items(doc.drawingItems()),
          nextSheet(doc.nextDrawingSheetId()), nextView(doc.nextDrawingViewId()),
          nextItem(doc.nextDrawingItemId()) {}
    bool operator==(const SheetState &) const = default;
};
// A sheet edit leaves every model record pointer-identical, through its own Undo
// and Redo, and publishes exactly one labeled history entry.
void sheetOnly(Document &doc, const std::string &label, const std::function<void()> &operation) {
    const ModelState model(doc);
    const auto position = doc.history().position;
    const auto revision = doc.revision();
    operation();
    check(doc.revision() == revision + 1, label + ": one revision");
    const auto page = doc.history(0, 1000);
    check(page.position == position + 1 && page.total == page.position &&
              page.entries.back().label == label,
          label + ": one labeled history entry");
    check(ModelState(doc) == model, label + ": model records are pointer-identical");
    const SheetState after(doc);
    doc.undo();
    check(ModelState(doc) == model, label + ": Undo keeps model records");
    doc.redo();
    check(ModelState(doc) == model && SheetState(doc) == after,
          label + ": Redo republishes the same records");
}
// A model edit leaves the drawing-sheet tables, items included, pointer-identical.
void modelOnly(Document &doc, const std::string &label, const std::function<void()> &operation) {
    const SheetState sheets(doc);
    operation();
    check(SheetState(doc) == sheets, label + ": sheet tables are unchanged");
    doc.undo();
    check(SheetState(doc) == sheets, label + ": Undo keeps sheet tables");
    doc.redo();
    check(SheetState(doc) == sheets, label + ": Redo keeps sheet tables");
}
std::vector<std::uint32_t> zOrder(const Document &doc, Id sheet) {
    std::vector<std::uint32_t> result;
    for (const auto id : drawingItemsOnSheet(doc, sheet))
        result.push_back(doc.drawingItems().at(id)->z);
    return result;
}
bool contiguous(const Document &doc, Id sheet) {
    const auto z = zOrder(doc, sheet);
    for (size_t i = 0; i < z.size(); ++i)
        if (z[i] != i)
            return false;
    return true;
}

void sheetNumberAndMargin() {
    Document doc;
    const auto a = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101");
    check(doc.drawingSheets().at(a)->number == "A-101" && doc.drawingSheets().at(a)->marginMm == 10,
          "Sheet number is stored and the margin defaults to 10 mm");
    // The default margin shrinks to a quarter of the shorter side on small paper.
    const auto small = createDrawingSheet(doc, "Small", {10, 30});
    check(doc.drawingSheets().at(small)->marginMm == 2.5, "Small paper default margin");
    rejectsWith([&] { createDrawingSheet(doc, "Duplicate", {}, "A-101"); },
                "Drawing sheet numbers must be unique");
    // Empty numbers are not unique keys.
    const auto b = createDrawingSheet(doc, "First floor", {});
    const auto c = createDrawingSheet(doc, "Roof", {});
    check(doc.drawingSheets().at(b)->number.empty() && doc.drawingSheets().at(c)->number.empty(),
          "Several sheets may have no number");
    rejects([&] { updateDrawingSheetPage(doc, b, std::string(33, 'n'), 10); }, "number length");
    rejects([&] { updateDrawingSheetPage(doc, b, "A\n1", 10); }, "number control");
    rejects([&] { updateDrawingSheetPage(doc, b, "A-1", -.001); }, "negative margin");
    rejects([&] { updateDrawingSheetPage(doc, b, "A-1", 50.001); }, "margin over 50");
    rejects([&] { updateDrawingSheetPage(doc, b, "A-1", nan); }, "margin finite");
    rejects([&] { updateDrawingSheetPage(doc, small, "", 2.501); }, "margin over a quarter");
    rejects([&] { createDrawingSheet(doc, "Tight", {100, 100}, "", 25.001); }, "create margin");
    sheetOnly(doc, "Update sheet",
              [&] { updateDrawingSheetPage(doc, b, std::string(32, 'n'), 50); });
    check(doc.drawingSheets().at(b)->marginMm == 50, "50 mm margin is inclusive");
    sheetOnly(doc, "Update sheet", [&] { updateDrawingSheetPage(doc, c, "", 0); });
    const auto revision = doc.revision();
    updateDrawingSheetPage(doc, c, "", 0);
    check(doc.revision() == revision, "An equal page update is a no-op");
    // Resizing keeps a margin that fits and reduces one that does not.
    resizeDrawingSheet(doc, a, {297, 210});
    check(doc.drawingSheets().at(a)->marginMm == 10, "Resizing keeps a fitting margin");
    resizeDrawingSheet(doc, b, {100, 120});
    check(doc.drawingSheets().at(b)->marginMm == 25,
          "Resizing reduces a margin that no longer fits");
    doc.undo();
    check(doc.drawingSheets().at(b)->marginMm == 50, "Undo restores the margin");
    check(drawingSheetBytes(doc.drawingSheets().at(b)) >
              drawingSheetBytes(doc.drawingSheets().at(c)),
          "Sheet numbers are charged");
}

void kindsAndBounds() {
    Fixture f;
    auto &doc = f.doc;
    const auto other = createDrawingSheet(doc, "Details", {297, 210});
    const auto detail = createDrawingView(doc, view(other, f.scene, "Detail"));
    // Every kind is accepted, and the variant index is the kind.
    const std::vector<std::pair<DrawingItemData, DrawingItemKind>> kinds{
        {text(), DrawingItemKind::Text},
        {paperLabel(), DrawingItemKind::Label},
        {viewLabel(f.plan, f.vertex), DrawingItemKind::Label},
        {dimension(f.plan, f.vertex, f.edge), DrawingItemKind::Dimension},
        {line(), DrawingItemKind::Line},
        {rectangle(), DrawingItemKind::Rectangle},
        {titleBlock(), DrawingItemKind::TitleBlock}};
    for (const auto &[data, kind] : kinds) {
        const auto id = createDrawingItem(doc, f.sheet, data);
        check(doc.drawingItems().at(id)->kind() == kind && doc.drawingItems().at(id)->data == data,
              "Item kind round trip");
    }
    check(zOrder(doc, f.sheet) == std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5, 6},
          "Created items stack bottom to top");
    check(drawingItemView(*doc.drawingItems().at(3)) == f.plan &&
              drawingItemView(*doc.drawingItems().at(4)) == f.plan &&
              drawingItemView(*doc.drawingItems().at(2)) == 0,
          "View binding is derived from the dimension and view label");
    check(DrawingFont{}.family == "DejaVu Sans" && DrawingFont{}.sizePt == 10,
          "Default font matches R078 export");

    auto variant = [&](DrawingItemData data, const std::string &what) {
        rejects([&] { createDrawingItem(doc, f.sheet, data); }, what);
    };
    auto textVariant = [&](const std::function<void(DrawingTextItem &)> &mutate,
                           const std::string &what) {
        auto item = text();
        mutate(item);
        variant(item, what);
    };
    textVariant([](auto &i) { i.text = ""; }, "empty text");
    textVariant([](auto &i) { i.text = std::string(4097, 't'); }, "text length");
    textVariant([](auto &i) { i.text = "bell\a"; }, "text control");
    textVariant([](auto &i) { i.text = "del\x7f"; }, "text delete");
    textVariant([](auto &i) { i.font.family = ""; }, "empty family");
    textVariant([](auto &i) { i.font.family = std::string(129, 'f'); }, "family length");
    textVariant([](auto &i) { i.font.family = "/usr/share/fonts/x.ttf"; }, "family path");
    textVariant([](auto &i) { i.font.family = "C:\\Fonts"; }, "family backslash path");
    textVariant([](auto &i) { i.font.family = " Lead"; }, "family leading space");
    textVariant([](auto &i) { i.font.sizePt = 3.99; }, "font minimum");
    textVariant([](auto &i) { i.font.sizePt = 72.01; }, "font maximum");
    textVariant([](auto &i) { i.font.sizePt = nan; }, "font finite");
    textVariant([](auto &i) { i.align = static_cast<DrawingTextAlign>(9); }, "align");
    textVariant([](auto &i) { i.frame.widthMm = 0; }, "zero width");
    textVariant([](auto &i) { i.frame.heightMm = -1; }, "negative height");
    textVariant([](auto &i) { i.frame.xMm = -.001; }, "frame left of paper");
    textVariant([](auto &i) { i.frame = {400, 10, 20.001, 10}; }, "frame right of paper");
    textVariant([](auto &i) { i.frame = {10, 290, 10, 7.001}; }, "frame below paper");
    textVariant([](auto &i) { i.frame.yMm = inf; }, "frame finite");

    auto labelVariant = [&](const std::function<void(DrawingLabelItem &)> &mutate,
                            const std::string &what, bool viewTarget = false) {
        auto item = viewTarget ? viewLabel(f.plan, f.edge) : paperLabel();
        mutate(item);
        variant(item, what);
    };
    labelVariant([](auto &i) { i.text = std::string(4097, 'l'); }, "label length");
    labelVariant([](auto &i) { i.textAt = {420.001, 10}; }, "label text outside paper");
    labelVariant([](auto &i) { i.textAt = {nan, 10}; }, "label text finite");
    labelVariant([](auto &i) { i.target.pointMm = {10, 297.001}; }, "paper target outside");
    labelVariant([&](auto &i) { i.target.view = f.plan; }, "paper target with view");
    labelVariant([&](auto &i) { i.target.anchor = f.vertex; }, "paper target with anchor");
    labelVariant([](auto &i) { i.target.lastMm = {1, 1}; }, "paper target with last position");
    labelVariant([](auto &i) { i.target.kind = static_cast<DrawingLabelTargetKind>(5); },
                 "target kind");
    labelVariant([](auto &i) { i.target.view = 999; }, "missing target view", true);
    labelVariant([&](auto &i) { i.target.view = detail; }, "target view on another sheet", true);
    labelVariant([](auto &i) { i.target.pointMm = {1, 1}; }, "view target with point", true);
    labelVariant([](auto &i) { i.target.lastMm = {nan, 0}; }, "view target last finite", true);
    labelVariant([](auto &i) { i.target.anchor = pointAnchor({1, 2, 3}); }, "point anchor", true);
    labelVariant([](auto &i) { i.target.anchor.state = AnchorState::Missing; },
                 "stored broken anchor", true);

    auto dimensionVariant = [&](const std::function<void(DrawingDimensionItem &)> &mutate,
                                const std::string &what) {
        auto item = dimension(f.plan, f.vertex, f.edge);
        mutate(item);
        variant(item, what);
    };
    dimensionVariant([](auto &i) { i.view = 999; }, "dimension missing view");
    dimensionVariant([&](auto &i) { i.view = detail; }, "dimension view on another sheet");
    dimensionVariant([](auto &i) { i.anchors[1] = pointAnchor({0, 0, 0}); }, "paper-only point");
    dimensionVariant(
        [&](auto &i) {
            const auto &record = *doc.bodies().at(f.body);
            i.anchors[0] = faceAnchor(doc, f.body, record.surface.faces.begin()->first, {1, 1, 0});
        },
        "face anchor");
    dimensionVariant([](auto &i) { i.anchors[0].state = AnchorState::Ambiguous; },
                     "stored ambiguous anchor");
    dimensionVariant([](auto &i) { i.anchors[0].entity = 0; }, "anchor without entity");
    dimensionVariant([](auto &i) { i.orientation = static_cast<DrawingDimensionOrientation>(4); },
                     "orientation");
    dimensionVariant([](auto &i) { i.offsetMm = nan; }, "offset finite");
    dimensionVariant([](auto &i) { i.offsetMm = -1e6 - 1; }, "offset bound");
    dimensionVariant([](auto &i) { i.prefix = std::string(257, 'p'); }, "prefix length");
    dimensionVariant([](auto &i) { i.prefix = "a\tb"; }, "prefix control");
    dimensionVariant([](auto &i) { i.lastMm[1] = {inf, 0}; }, "last position finite");
    dimensionVariant([](auto &i) { i.font.sizePt = 100; }, "dimension font");

    auto lineVariant = [&](const std::function<void(DrawingLineItem &)> &mutate,
                           const std::string &what) {
        auto item = line();
        mutate(item);
        variant(item, what);
    };
    lineVariant([](auto &i) { i.to = i.from; }, "zero-length line");
    lineVariant([](auto &i) { i.to = {420.001, 0}; }, "line outside paper");
    lineVariant([](auto &i) { i.from = {-1, 0}; }, "line before paper");
    lineVariant([](auto &i) { i.strokeMm = .099; }, "stroke minimum");
    lineVariant([](auto &i) { i.strokeMm = 2.001; }, "stroke maximum");
    lineVariant([](auto &i) { i.dash = static_cast<DrawingStrokeDash>(3); }, "dash");
    auto box = rectangle();
    box.frame.widthMm = 410.001;
    variant(box, "rectangle outside paper");
    box = rectangle();
    box.strokeMm = 0;
    variant(box, "rectangle stroke");
    auto block = titleBlock();
    block.fields.revision = std::string(257, 'r');
    variant(block, "title field length");
    block = titleBlock();
    block.fields.project = "Line\nbreak";
    variant(block, "title field control");
    block = titleBlock();
    block.frame = {300, 250, 160, 47};
    variant(block, "title block outside paper");

    // Exact bounds are accepted.
    auto edgeText = text();
    edgeText.text = std::string(4096, 't');
    edgeText.text[10] = '\n';
    edgeText.text[11] = '\t';
    edgeText.font = {std::string(128, 'f'), 4};
    edgeText.frame = {0, 0, 420, 297};
    edgeText.align = DrawingTextAlign::Right;
    const auto bounded = createDrawingItem(doc, f.sheet, edgeText);
    edgeText.font.sizePt = 72;
    updateDrawingItem(doc, bounded, edgeText);
    auto edgeLine = line();
    edgeLine.from = {0, 0};
    edgeLine.to = {420, 297};
    edgeLine.strokeMm = .1;
    createDrawingItem(doc, f.sheet, edgeLine);
    edgeLine.strokeMm = 2;
    createDrawingItem(doc, f.sheet, edgeLine);
    auto edgeDimension = dimension(f.plan, f.edge, f.edge);
    edgeDimension.prefix = std::string(256, 'p');
    edgeDimension.offsetMm = 1e6;
    edgeDimension.orientation = DrawingDimensionOrientation::Vertical;
    // Derived positions are bounded, not required to lie on the paper.
    edgeDimension.lastMm = {DrawingPointMm{-1e6, 1e6}, DrawingPointMm{5000, -20}};
    createDrawingItem(doc, f.sheet, edgeDimension);
    auto edgeBlock = titleBlock();
    edgeBlock.fields = {std::string(256, 'a'), std::string(256, 'b'), "", ""};
    createDrawingItem(doc, f.sheet, edgeBlock);
    // Item geometry must still fit after a resize.
    rejects([&] { resizeDrawingSheet(doc, f.sheet, {297, 210}); }, "resize must fit items");
    rejects([&] { createDrawingItem(doc, 999, text()); }, "item on missing sheet");
    rejects([&] { updateDrawingItem(doc, 999, text()); }, "update missing item");
    // Updating an item may change its kind; its ID, sheet and z are kept.
    const auto before = *doc.drawingItems().at(bounded);
    updateDrawingItem(doc, bounded, line());
    const auto &after = *doc.drawingItems().at(bounded);
    check(after.kind() == DrawingItemKind::Line && after.id == before.id &&
              after.sheet == before.sheet && after.z == before.z,
          "Updating replaces only the data");
    const auto revision = doc.revision();
    updateDrawingItem(doc, bounded, line());
    check(doc.revision() == revision, "An equal item update is a no-op");
}

// Fills a sheet with items in one direct edit.
void fill(Document &doc, Id sheet, size_t count, const DrawingItemData &data = line()) {
    Edit edit{"Fill", {}};
    edit.drawingSheetScope = true;
    auto z = static_cast<std::uint32_t>(drawingItemsOnSheet(doc, sheet).size());
    auto id = doc.nextDrawingItemId();
    for (size_t i = 0; i < count; ++i, ++id)
        edit.drawingItems.push_back(
            {id, nullptr,
             std::make_shared<DrawingItemRecord>(DrawingItemRecord{id, sheet, z++, data})});
    doc.apply(std::move(edit), doc.revision());
}
void limits() {
    Document doc;
    std::vector<Id> sheets;
    for (size_t i = 0; i < drawingItemCountLimit / drawingItemsPerSheetLimit; ++i)
        sheets.push_back(createDrawingSheet(doc, "Sheet " + std::to_string(i), {}));
    const auto spare = createDrawingSheet(doc, "Spare", {});
    fill(doc, sheets[0], drawingItemsPerSheetLimit - 1);
    createDrawingItem(doc, sheets[0], text());
    rejectsWith([&] { createDrawingItem(doc, sheets[0], text()); },
                "A drawing sheet allows at most 512 items");
    for (size_t i = 1; i < sheets.size(); ++i)
        fill(doc, sheets[i], drawingItemsPerSheetLimit);
    check(doc.drawingItems().size() == drawingItemCountLimit, "Item count limit is inclusive");
    rejectsWith([&] { createDrawingItem(doc, spare, text()); },
                "Invalid drawing item allocator or count");
    rejects([&] { moveDrawingItem(doc, doc.drawingItems().begin()->first, sheets[1]); },
            "move into a full sheet");
    // Retained-memory accounting charges every item.
    size_t charged = 0;
    for (const auto &[id, item] : doc.drawingItems())
        charged += drawingItemBytes(item);
    check(charged < drawingSheetBytesLimit, "A full item table fits the eight MiB budget");
    Document empty;
    check(doc.readSnapshotBytes() >= empty.readSnapshotBytes() + charged,
          "Snapshot accounting charges drawing items");
    // Large items count toward the shared eight MiB budget.
    Document heavy;
    auto big = text();
    big.text = std::string(drawingItemTextLimit, 'x');
    // About 1,900 items of 4,096 bytes fill the budget, well under the count limit.
    const auto perItem =
        drawingItemBytes(std::make_shared<DrawingItemRecord>(DrawingItemRecord{1, 1, 0, big}));
    const auto fits = drawingSheetBytesLimit / perItem - 64;
    for (size_t placed = 0, s = 0; placed < fits; ++s) {
        const auto sheet = createDrawingSheet(heavy, "Heavy " + std::to_string(s), {});
        const auto count = std::min(drawingItemsPerSheetLimit, fits - placed);
        fill(heavy, sheet, count, big);
        placed += count;
    }
    const auto last = createDrawingSheet(heavy, "Last", {});
    rejectsWith([&] { fill(heavy, last, 128, big); },
                "Drawing sheets exceed the eight MiB record budget");
    check(heavy.drawingItems().size() < drawingItemCountLimit,
          "The byte budget binds before the count limit for long text");
}

void zOrderAndMoves() {
    Fixture f;
    auto &doc = f.doc;
    const auto second = createDrawingSheet(doc, "Second", {});
    const auto a = createDrawingItem(doc, f.sheet, text());
    const auto b = createDrawingItem(doc, f.sheet, line());
    const auto c = createDrawingItem(doc, f.sheet, rectangle());
    const auto d = createDrawingItem(doc, second, titleBlock());
    check(drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{a, b, c} &&
              drawingItemsOnSheet(doc, second) == std::vector<Id>{d},
          "Items by sheet, bottom first");
    reorderDrawingItems(doc, f.sheet, {c, a, b});
    check(drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{c, a, b} && contiguous(doc, f.sheet),
          "Explicit z order");
    check(doc.history().entries.back().label == "Reorder sheet items", "Reorder is one entry");
    const auto revision = doc.revision();
    reorderDrawingItems(doc, f.sheet, {c, a, b});
    check(doc.revision() == revision, "An equal reorder is a no-op");
    rejects([&] { reorderDrawingItems(doc, f.sheet, {c, a}); }, "short order");
    rejects([&] { reorderDrawingItems(doc, f.sheet, {c, a, a}); }, "duplicate order");
    rejects([&] { reorderDrawingItems(doc, f.sheet, {c, a, d}); }, "foreign order");
    rejects([&] { reorderDrawingItems(doc, 999, {}); }, "missing sheet order");
    // Moving an item puts it on top of the target and restacks the source.
    moveDrawingItem(doc, c, second);
    check(drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{a, b} &&
              drawingItemsOnSheet(doc, second) == std::vector<Id>{d, c} &&
              contiguous(doc, f.sheet) && contiguous(doc, second),
          "Move keeps both stacks contiguous");
    doc.undo();
    check(drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{c, a, b}, "Undo restores the move");
    doc.redo();
    // Deleting restacks the rest.
    eraseDrawingItem(doc, a);
    check(drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{b} && contiguous(doc, f.sheet),
          "Delete restacks");
    rejects([&] { eraseDrawingItem(doc, a); }, "deleted item");
    rejects([&] { moveDrawingItem(doc, b, 999); }, "move to missing sheet");
    // A moved bound item must keep its view on its own sheet.
    const auto bound = createDrawingItem(doc, f.sheet, dimension(f.plan, f.vertex, f.edge));
    rejectsWith([&] { moveDrawingItem(doc, bound, second); },
                "Drawing item must reference a drawing view on its own sheet");
    // Direct edits must keep z contiguous.
    auto gap = std::make_shared<DrawingItemRecord>(*doc.drawingItems().at(b));
    gap->z = 5;
    Edit gapped{"Gap", {}};
    gapped.drawingItems.push_back({b, doc.drawingItems().at(b), gap});
    rejectsWith([&] { doc.apply(gapped, doc.revision()); },
                "Drawing item order must contain each position exactly once per sheet");
    auto clash = std::make_shared<DrawingItemRecord>(*doc.drawingItems().at(bound));
    clash->z = doc.drawingItems().at(b)->z;
    Edit clashed{"Clash", {}};
    clashed.drawingItems.push_back({bound, doc.drawingItems().at(bound), clash});
    rejectsWith([&] { doc.apply(clashed, doc.revision()); },
                "Drawing item order must contain each position exactly once per sheet");
    auto lost = std::make_shared<DrawingItemRecord>(*doc.drawingItems().at(b));
    lost->sheet = 999;
    Edit orphan{"Orphan", {}};
    orphan.drawingItems.push_back({b, doc.drawingItems().at(b), lost});
    rejectsWith([&] { doc.apply(orphan, doc.revision()); },
                "Drawing item must belong to an existing drawing sheet");
}

void cascades() {
    Fixture f;
    auto &doc = f.doc;
    const auto second = createDrawingSheet(doc, "Second", {});
    const auto elevation = createDrawingView(doc, view(f.sheet, f.scene, "Elevation"));
    const auto note = createDrawingItem(doc, f.sheet, text());
    const auto dim = createDrawingItem(doc, f.sheet, dimension(f.plan, f.vertex, f.edge));
    const auto paper = createDrawingItem(doc, f.sheet, paperLabel());
    const auto tag = createDrawingItem(doc, f.sheet, viewLabel(f.plan, f.edge));
    const auto otherDim = createDrawingItem(doc, f.sheet, dimension(elevation, f.edge, f.vertex));
    const auto block = createDrawingItem(doc, second, titleBlock());

    // Moving a view takes its dimensions and view labels to the top of the target.
    const SheetState start(doc);
    sheetOnly(doc, "Move sheet view", [&] { moveDrawingView(doc, f.plan, second); });
    check(drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{note, paper, otherDim} &&
              drawingItemsOnSheet(doc, second) == std::vector<Id>{block, dim, tag} &&
              contiguous(doc, f.sheet) && contiguous(doc, second),
          "Moving a view moves its bound items on top, keeping their order");
    check(doc.history().entries.back().label == "Move sheet view", "One entry for the cascade");
    doc.undo();
    check(SheetState(doc).items == start.items, "Undo restores every item in one step");
    // updateDrawingView with a new sheet (drawing_view.move) cascades the same way.
    auto moved = *doc.drawingViews().at(f.plan);
    moved.sheet = second;
    moved.frame = {10, 10, 100, 100};
    updateDrawingView(doc, f.plan, moved);
    check(drawingItemsOnSheet(doc, second) == std::vector<Id>{block, dim, tag},
          "Updating a view's sheet moves its bound items");
    doc.undo();
    check(SheetState(doc).items == start.items, "Undo restores the update cascade");

    // Deleting a view deletes its bound items and keeps paper items.
    sheetOnly(doc, "Delete sheet view", [&] { eraseDrawingView(doc, f.plan); });
    check(!doc.drawingItems().contains(dim) && !doc.drawingItems().contains(tag) &&
              drawingItemsOnSheet(doc, f.sheet) == std::vector<Id>{note, paper, otherDim} &&
              contiguous(doc, f.sheet),
          "Deleting a view deletes its dimensions and view labels");
    doc.undo();
    check(SheetState(doc).items == start.items && doc.drawingViews().contains(f.plan),
          "Undo restores the view and its items in one step");

    // Deleting a sheet deletes its views and items; other sheets keep theirs.
    sheetOnly(doc, "Delete sheet", [&] { eraseDrawingSheet(doc, f.sheet); });
    check(doc.drawingItems().size() == 1 && doc.drawingItems().contains(block),
          "Deleting a sheet deletes its items");
    doc.undo();
    check(SheetState(doc).items == start.items, "Undo restores the sheet's items");

    // Deleting a scene keeps the view and its items (diagnosed broken, ADR 0163).
    const auto items = doc.drawingItems();
    eraseScene(doc, f.scene);
    check(doc.drawingItems() == items, "Deleting a scene keeps bound items");
    doc.undo();

    // A direct edit that deletes a view without its items is refused.
    Edit dangling{"Dangling", {}};
    dangling.drawingViews.push_back({f.plan, doc.drawingViews().at(f.plan), nullptr});
    rejectsWith([&] { doc.apply(dangling, doc.revision()); },
                "Drawing item must reference an existing drawing view");
    Edit stray{"Stray", {}};
    auto strayed = std::make_shared<DrawingViewRecord>(*doc.drawingViews().at(f.plan));
    strayed->sheet = second;
    stray.drawingViews.push_back({f.plan, doc.drawingViews().at(f.plan), strayed});
    rejectsWith([&] { doc.apply(stray, doc.revision()); },
                "Drawing item must reference a drawing view on its own sheet");
    // The target sheet must have room for the moved items.
    fill(doc, second, drawingItemsPerSheetLimit - 2);
    rejectsWith([&] { moveDrawingView(doc, f.plan, second); },
                "A drawing sheet allows at most 512 items");
}

void identitiesHistoryAndAllocators() {
    Fixture f;
    auto &doc = f.doc;
    doc.markSaved();
    const auto saved = doc.saveStamp();
    const auto historyBytes = doc.historyBytes();
    const auto a = createDrawingItem(doc, f.sheet, text());
    check(doc.dirty() && doc.historyBytes() > historyBytes + sizeof(DrawingItemRecord),
          "Item edits dirty the document and are charged to history");
    doc.undo();
    check(doc.drawingItems().empty() && doc.isCurrentSnapshot(saved),
          "Undo returns to the saved state");
    check(doc.nextDrawingItemId() == a + 1, "Undo does not rewind the item allocator");
    const auto b = createDrawingItem(doc, f.sheet, text());
    check(b == a + 1, "A retired item ID is never reused");
    const auto other = createDrawingSheet(doc, "Other", {});
    const auto c = createDrawingItem(doc, other, line());
    check(c == b + 1, "Item IDs are document-global across sheets");
    eraseDrawingItem(doc, c);
    doc.undo();
    doc.undo();
    check(doc.nextDrawingItemId() == c + 1, "Undoing creation and deletion keeps the allocator");
    doc.redo();
    // Direct edits cannot reuse retired IDs.
    eraseDrawingItem(doc, c);
    auto reuse = std::make_shared<DrawingItemRecord>(DrawingItemRecord{a, f.sheet, 1, line()});
    Edit reused{"Reuse", {}};
    reused.drawingItems.push_back({a, nullptr, reuse});
    rejectsWith([&] { doc.apply(reused, doc.revision()); },
                "Retired drawing item ID cannot be reused");
    // Floors raise the item allocator without creating items.
    Edit floor{"Floor", {}};
    floor.drawingSheetScope = true;
    floor.nextDrawingItemFloor = doc.nextDrawingItemId() + 10;
    auto renamed = std::make_shared<DrawingSheetRecord>(*doc.drawingSheets().at(other));
    renamed->name = "Renamed";
    floor.drawingSheets.push_back({other, doc.drawingSheets().at(other), renamed});
    const auto raised = floor.nextDrawingItemFloor;
    doc.apply(floor, doc.revision());
    check(doc.nextDrawingItemId() == raised, "An item floor raises the allocator");
    doc.undo();
    check(doc.nextDrawingItemId() == raised, "Undo keeps a raised item allocator");
    check(createDrawingItem(doc, f.sheet, line()) == raised, "Allocation continues from the floor");
    // Stale and malformed changes are refused.
    Edit stale{"Stale", {}};
    auto changed = std::make_shared<DrawingItemRecord>(*doc.drawingItems().at(b));
    changed->data = line();
    stale.drawingItems.push_back({b, nullptr, changed});
    rejectsWith([&] { doc.apply(stale, doc.revision()); }, "Invalid or stale drawing item change");
    Edit same{"Same", {}};
    same.drawingItems.push_back({b, doc.drawingItems().at(b), doc.drawingItems().at(b)});
    rejectsWith([&] { doc.apply(same, doc.revision()); }, "Invalid or stale drawing item change");
    Edit twice{"Twice", {}};
    twice.drawingItems.push_back({b, doc.drawingItems().at(b), changed});
    twice.drawingItems.push_back({b, doc.drawingItems().at(b), nullptr});
    rejectsWith([&] { doc.apply(twice, doc.revision()); }, "Invalid or stale drawing item change");
    // readSnapshot shares records and allocators without history.
    const auto snapshot = doc.readSnapshot();
    check(snapshot.drawingItems() == doc.drawingItems() &&
              snapshot.nextDrawingItemId() == doc.nextDrawingItemId() && !snapshot.canUndo() &&
              snapshot.readSnapshotBytes() == doc.readSnapshotBytes(),
          "readSnapshot shares drawing items");
    // Records are frozen copies, so aliases cannot mutate the document.
    auto alias = std::make_shared<DrawingItemRecord>(*doc.drawingItems().at(b));
    std::get<DrawingTextItem>(alias->data).text = "Frozen";
    Edit frozen{"Freeze item", {}};
    frozen.drawingItems.push_back({b, doc.drawingItems().at(b), alias});
    doc.apply(frozen, doc.revision());
    std::get<DrawingTextItem>(alias->data).text = "Mutated alias";
    check(std::get<DrawingTextItem>(doc.drawingItems().at(b)->data).text == "Frozen",
          "Applied items are frozen copies");
    // History navigation crosses item, sheet and model entries.
    const auto total = doc.history().total;
    doc.navigateHistory(0, doc.revision());
    check(doc.drawingItems().empty() && doc.drawingSheets().empty() && doc.bodies().empty(),
          "History navigation unwinds items with everything else");
    doc.navigateHistory(total, doc.revision());
    check(doc.drawingItems().size() == 2, "History navigation replays items");
    // Restore replaces the item table; restoring without tables leaves it empty.
    Document restored = doc;
    restored.restore(doc.identity(), doc.nextId(), doc.bodies());
    check(restored.drawingItems().empty() && restored.nextDrawingItemId() == 1,
          "Restore starts with an empty item table");
}

void enforcement() {
    Fixture f;
    auto &doc = f.doc;
    const auto wire = doc.addWire(0, {0, 5, 0}, {4, 5, 0});
    const auto component =
        createComponent(doc, doc.addFace({{{5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}}}), "Panel");
    const auto tag = createTag(doc, "Walls");
    assignTag(doc, f.body, tag);
    AnnotationRecord note;
    note.name = "Width";
    note.text = "Width";
    const auto wireEdge = doc.bodies().at(wire)->topology.edges.begin()->first;
    note.anchors = {edgeAnchor(doc, wire, wireEdge, .25), edgeAnchor(doc, wire, wireEdge, .75)};
    note.offset = {0, 1, 0};
    createAnnotation(doc, note);
    const auto second = createDrawingSheet(doc, "Second", {});

    // Pointer identity of model records under every item edit.
    Id text1{}, dim{}, label{}, block{};
    sheetOnly(doc, "Create sheet item", [&] { text1 = createDrawingItem(doc, f.sheet, text()); });
    sheetOnly(doc, "Create sheet item",
              [&] { dim = createDrawingItem(doc, f.sheet, dimension(f.plan, f.vertex, f.edge)); });
    sheetOnly(doc, "Create sheet item",
              [&] { label = createDrawingItem(doc, f.sheet, viewLabel(f.plan, f.vertex)); });
    sheetOnly(doc, "Create sheet item",
              [&] { block = createDrawingItem(doc, f.sheet, titleBlock()); });
    sheetOnly(doc, "Update sheet item", [&] {
        auto data = dimension(f.plan, f.edge, f.vertex);
        data.orientation = DrawingDimensionOrientation::Horizontal;
        updateDrawingItem(doc, dim, data);
    });
    sheetOnly(doc, "Reorder sheet items",
              [&] { reorderDrawingItems(doc, f.sheet, {block, dim, label, text1}); });
    sheetOnly(doc, "Move sheet item", [&] { moveDrawingItem(doc, text1, second); });
    sheetOnly(doc, "Update sheet", [&] { updateDrawingSheetPage(doc, f.sheet, "A-101", 12); });
    sheetOnly(doc, "Move sheet view", [&] { moveDrawingView(doc, f.plan, second); });
    sheetOnly(doc, "Delete sheet item", [&] { eraseDrawingItem(doc, block); });
    sheetOnly(doc, "Delete sheet view", [&] { eraseDrawingView(doc, f.plan); });
    sheetOnly(doc, "Delete sheet", [&] { eraseDrawingSheet(doc, second); });
    doc.undo();
    doc.undo();
    check(doc.drawingItems().contains(dim) && doc.drawingItems().contains(label),
          "Fixture items restored");

    // The reverse: model edits leave items untouched, even when anchors stop resolving.
    modelOnly(doc, "Move body", [&] { doc.move(f.body, {1, 0, 0}); });
    modelOnly(doc, "Add face", [&] { doc.addFace({{{9, 9, 0}, {10, 9, 0}, {10, 10, 0}}}); });
    modelOnly(doc, "Update scene", [&] { updateScene(doc, f.scene, cameraScene(30)); });
    modelOnly(doc, "Edit tag", [&] { editTag(doc, tag, "Partitions"); });
    modelOnly(doc, "Component", [&] {
        editComponentDefinition(doc, component.definition, [&](Document &draft) {
            const auto member = component.movedGeometry.at(component.instance);
            draft.transform(member, Transform::scaling({2, 1, 1}),
                            draft.bodies().at(member)->parent);
            return ChangeReport{};
        });
    });
    modelOnly(doc, "Delete anchored body", [&] { doc.erase(f.body); });
    modelOnly(doc, "Delete scene", [&] { eraseScene(doc, f.scene); });

    // The sheet-only predicate covers items.
    Edit pure{"Pure", {}};
    auto changed = std::make_shared<DrawingItemRecord>(*doc.drawingItems().at(dim));
    std::get<DrawingDimensionItem>(changed->data).prefix = "Clear ";
    pure.drawingItems.push_back({dim, doc.drawingItems().at(dim), changed});
    check(isDrawingSheetOnlyEdit(pure), "Item changes alone are sheet-only");
    auto mixed = pure;
    const auto oldBody = doc.bodies().at(wire);
    auto hidden = std::make_shared<Body>(*oldBody);
    hidden->hidden = !hidden->hidden;
    mixed.changes.push_back({wire, oldBody, hidden});
    check(!isDrawingSheetOnlyEdit(mixed), "Items with a body change are not sheet-only");
    rejectsWith([&] { doc.apply(mixed, doc.revision()); },
                "Drawing sheet edits cannot change model records");
    auto tags = pure;
    auto renamedTag = std::make_shared<TagRecord>(*doc.tags().at(tag));
    renamedTag->visible = !renamedTag->visible;
    tags.tags.push_back({tag, doc.tags().at(tag), renamedTag});
    rejectsWith([&] { doc.apply(tags, doc.revision()); },
                "Drawing sheet edits cannot change model records");
    auto raised = pure;
    raised.nextAnnotationFloor = doc.nextAnnotationId() + 1;
    rejectsWith([&] { doc.apply(raised, doc.revision()); },
                "Drawing sheet edits cannot change model records");
    const ModelState model(doc);
    doc.apply(pure, doc.revision());
    check(ModelState(doc) == model, "An item-only direct edit applies without model changes");

    // Shared component scopes reject item edits.
    const auto revision = doc.revision();
    rejectsWith(
        [&] {
            editComponentDefinition(doc, component.definition, [&](Document &draft) {
                const auto sheet = createDrawingSheet(draft, "Inside component", {});
                createDrawingItem(draft, sheet, text());
                return ChangeReport{};
            });
        },
        "Shared component edits cannot modify drawing sheets");
    check(doc.revision() == revision, "Rejected component scopes publish nothing");
}

void preparedAndAmend() {
    Fixture f;
    auto &doc = f.doc;
    const auto first = createDrawingItem(doc, f.sheet, text());
    // Prepared edits stage an item edit on a private copy and publish it once.
    const auto prepared = doc.prepareEdit(
        [&](Document &candidate) { createDrawingItem(candidate, f.sheet, line()); });
    check(doc.drawingItems().size() == 1 && prepared.snapshot().drawingItems().size() == 2 &&
              prepared.retainedBytes() > prepared.snapshot().readSnapshotBytes(),
          "Prepared item edits stay private and are charged");
    const ModelState model(doc);
    doc.applyPrepared(prepared);
    check(doc.drawingItems().size() == 2 && ModelState(doc) == model &&
              doc.history().entries.back().label == "Create sheet item",
          "Prepared item edits publish one entry");
    const auto stale = doc.prepareEdit(
        [&](Document &candidate) { updateDrawingItem(candidate, first, paperLabel()); });
    updateDrawingSheetPage(doc, f.sheet, "A-1", 10);
    rejects([&] { doc.applyPrepared(stale); }, "stale proposal");
    // Amending an item edit replaces it in place, within its own records.
    auto nudged = text();
    nudged.frame.xMm = 11;
    updateDrawingItem(doc, first, nudged);
    const auto entries = doc.history().total;
    nudged.frame.xMm = 20;
    doc.amendLast(doc.amendmentStamp(),
                  [&](Document &staged) { updateDrawingItem(staged, first, nudged); });
    check(doc.history().total == entries &&
              std::get<DrawingTextItem>(doc.drawingItems().at(first)->data).frame.xMm == 20 &&
              doc.history().entries.back().label == "Update sheet item",
          "Ten nudges amend one entry");
    const auto second = doc.drawingItems().rbegin()->first;
    rejectsWith(
        [&] {
            doc.amendLast(doc.amendmentStamp(), [&](Document &staged) {
                updateDrawingItem(staged, second, rectangle());
            });
        },
        "Replacement cannot change another drawing item");
    rejectsWith(
        [&] {
            doc.amendLast(doc.amendmentStamp(), [&](Document &staged) {
                createDrawingItem(staged, f.sheet, rectangle());
            });
        },
        "Replacement must preserve drawing item creation count");
    // Amended creation keeps the allocator monotonic.
    const auto created = createDrawingItem(doc, f.sheet, rectangle());
    doc.amendLast(doc.amendmentStamp(), [&](Document &staged) {
        auto box = rectangle();
        box.strokeMm = 1;
        createDrawingItem(staged, f.sheet, box);
    });
    check(!doc.drawingItems().contains(created) &&
              std::get<DrawingRectangleItem>(doc.drawingItems().rbegin()->second->data).strokeMm ==
                  1 &&
              doc.nextDrawingItemId() > created + 1 && contiguous(doc, f.sheet),
          "Amended creation keeps the allocator monotonic");
}
} // namespace
int main() {
    try {
        Document::setFullValidationOracle(true);
        sheetNumberAndMargin();
        kindsAndBounds();
        limits();
        zOrderAndMoves();
        cascades();
        identitiesHistoryAndAllocators();
        enforcement();
        preparedAndAmend();
        check(Document::fullValidationOracle(), "Full validation oracle stayed on");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "drawing item record tests passed\n";
}
