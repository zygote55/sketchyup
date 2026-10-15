#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/drawing_sheet_commands.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "core/components.hpp"
#include "core/drawing_item_records.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include "io/drawing_items.hpp"
#include "io/scenes_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <type_traits>
using namespace sketchy;
// Page-item commands and queries (ADR 0164 §§3, 5; R091.c).
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> std::string rejects(F operation, const std::string &what) {
    try {
        operation();
    } catch (const ValidationOracleMismatch &) {
        throw;
    } catch (const InspectionError &error) {
        return error.code() + ": " + error.what();
    } catch (const std::exception &error) {
        return error.what();
    }
    throw std::runtime_error("Expected rejection: " + what);
}
QJsonObject batch(const Document &doc, QJsonArray commands) {
    return {{"apiVersion", 1},
            {"documentId", QString::fromStdString(doc.identity())},
            {"expectedRevision", QString::number(doc.revision())},
            {"commands", commands}};
}
QJsonObject run(Document &doc, QJsonArray commands) {
    return executeBatch(doc, batch(doc, std::move(commands)));
}
QJsonObject query(const Document &doc, const char *name, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return extra;
}
QJsonObject inspect(const Document &doc, const char *name, QJsonObject extra = {}) {
    return inspectDocument(doc, query(doc, name, std::move(extra)))["data"].toObject();
}
QString text(Id id) { return QString::number(id); }
QJsonObject paper(double width, double height) {
    return {{"widthMm", width}, {"heightMm", height}};
}
QJsonObject frame(double x, double y, double width, double height) {
    return {{"xMm", x}, {"yMm", y}, {"widthMm", width}, {"heightMm", height}};
}
QJsonObject createSheet(const char *name, QJsonObject extra = {}) {
    extra["command"] = "drawing_sheet.create";
    extra["name"] = name;
    if (!extra.contains("paper"))
        extra["paper"] = paper(420, 297);
    return extra;
}
QJsonObject createView(const char *name, Id sheet, Id scene) {
    return {{"command", "drawing_view.create"},
            {"name", name},
            {"sheet", text(sheet)},
            {"source", QJsonObject{{"kind", "scene"}, {"scene", text(scene)}}},
            {"frame", frame(20, 20, 200, 150)},
            {"offsetMm", QJsonArray{10, -5}},
            {"scaleDenominator", 50}};
}
QJsonObject createItem(Id sheet, const char *kind, QJsonObject data) {
    return {{"command", "drawing_item.create"},
            {"sheet", text(sheet)},
            {"kind", kind},
            {"data", data}};
}
QJsonObject updateItem(Id item, const char *kind, QJsonObject data) {
    return {{"command", "drawing_item.update"}, {"item", text(item)}, {"kind", kind}, {"data", data}};
}
QJsonObject vertex(Id body, Id id) {
    return {{"kind", "vertex"}, {"body", text(body)}, {"vertex", text(id)}};
}
QJsonObject edge(Id body, Id id, double fraction) {
    return {{"kind", "edge"}, {"body", text(body)}, {"edge", text(id)}, {"fraction", fraction}};
}
SceneSnapshot planScene(bool orthographic = true) {
    SceneSnapshot snapshot;
    snapshot.camera = SceneCamera{};
    snapshot.camera->target = {1, .5, 0};
    snapshot.camera->yaw = 0;
    snapshot.camera->pitch = 90;
    snapshot.camera->distance = 20;
    snapshot.camera->orthographic = orthographic;
    return snapshot;
}
std::string lastLabel(const Document &doc) {
    const auto page = doc.history();
    check(page.position > 0 && page.position <= page.entries.size(), "History has an entry");
    return page.entries[page.position - 1].label;
}
// Every model record table, compared by pointer.
struct ModelRecords {
    std::remove_cvref_t<decltype(std::declval<Document>().bodies())> bodies;
    std::remove_cvref_t<decltype(std::declval<Document>().definitions())> definitions;
    std::remove_cvref_t<decltype(std::declval<Document>().instances())> instances;
    std::remove_cvref_t<decltype(std::declval<Document>().tags())> tags;
    std::remove_cvref_t<decltype(std::declval<Document>().materials())> materials;
    std::remove_cvref_t<decltype(std::declval<Document>().assets())> assets;
    std::remove_cvref_t<decltype(std::declval<Document>().scenes())> scenes;
    std::remove_cvref_t<decltype(std::declval<Document>().sections())> sections;
    std::remove_cvref_t<decltype(std::declval<Document>().annotations())> annotations;
    explicit ModelRecords(const Document &doc)
        : bodies(doc.bodies()), definitions(doc.definitions()), instances(doc.instances()),
          tags(doc.tags()), materials(doc.materials()), assets(doc.assets()),
          scenes(doc.scenes()), sections(doc.sections()), annotations(doc.annotations()) {}
    bool operator==(const ModelRecords &) const = default;
};
Id vertexAt(const Document &doc, Id body, Vec3 point) {
    for (const auto &[id, p] : doc.bodies().at(body)->surface.vertices)
        if (length(p - point) < 1e-9)
            return id;
    throw std::runtime_error("No vertex at the point");
}
Id edgeBetween(const Document &doc, Id body, Vec3 a, Vec3 b) {
    const auto va = vertexAt(doc, body, a), vb = vertexAt(doc, body, b);
    for (const auto &[id, e] : doc.bodies().at(body)->topology.edges)
        if ((e.a == va && e.b == vb) || (e.a == vb && e.b == va))
            return id;
    throw std::runtime_error("No edge between the points");
}
// A 2 m x 1 m slab with an orthographic plan scene and a perspective scene, one A3
// sheet ("A-101") and one 1:50 plan view on it.
struct Fixture {
    Document doc;
    Id body{}, plan{}, perspective{}, sheet{1}, view{1}, perspectiveView{2};
    Fixture() {
        body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        plan = createScene(doc, "Plan", planScene());
        perspective = createScene(doc, "Perspective", planScene(false));
        run(doc, {createSheet("Ground floor", {{"number", "A-101"}}),
                  createView("Plan", sheet, plan), createView("Sketch", sheet, perspective)});
        run(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}});
    }
    QJsonObject dimension() const {
        return {{"view", text(view)},
                {"anchors", QJsonArray{vertex(body, vertexAt(doc, body, {0, 0, 0})),
                                       vertex(body, vertexAt(doc, body, {2, 0, 0}))}}};
    }
};
Id created(const QJsonObject &result) {
    return result["createdDrawingItems"].toArray()[0].toString().toULongLong();
}

// Each operation is one labeled, sheet-only history entry that leaves every model
// record pointer-identical.
void everyOperationIsSheetOnly() {
    Fixture f;
    auto &doc = f.doc;
    const auto model = ModelRecords(doc);
    const auto nextId = doc.nextId();
    std::set<QString> covered;
    auto step = [&](QJsonObject command, const std::string &label) {
        const auto revision = doc.revision();
        const auto total = doc.history().total;
        const auto result = run(doc, {command});
        check(doc.revision() == revision + 1 && doc.history().total == total + 1,
              label + " publishes one history entry");
        check(lastLabel(doc) == commandDescription(command["command"].toString())["label"]
                                    .toString()
                                    .toStdString(),
              label + " uses its catalogue label");
        check(ModelRecords(doc) == model && doc.nextId() == nextId,
              label + " leaves every model record pointer-identical");
        covered.insert(command["command"].toString());
        return result;
    };
    const auto second = step(createSheet("Details", {{"number", "A-102"}, {"marginMm", 15}}),
                             "create sheet with number and margin")["createdDrawingSheets"]
                            .toArray()[0]
                            .toString()
                            .toULongLong();
    check(doc.drawingSheets().at(second)->number == "A-102" &&
              doc.drawingSheets().at(second)->marginMm == 15,
          "Create accepts number and margin");
    check(doc.drawingSheets().at(f.sheet)->marginMm == 10, "The margin defaults to 10 mm");
    step({{"command", "drawing_sheet.update"}, {"sheet", text(second)}, {"marginMm", 12}},
         "update margin");
    step({{"command", "drawing_sheet.update"}, {"sheet", text(second)}, {"number", "A-201"}},
         "update number");
    check(doc.drawingSheets().at(second)->number == "A-201" &&
              doc.drawingSheets().at(second)->marginMm == 12,
          "Update keeps the field it does not name");
    // R091.g: the dimension format merges field by field; the default is whole millimetres.
    check(doc.drawingSheets().at(second)->dimensionFormat == DrawingDimensionFormat{},
          "The dimension format defaults to whole millimetres without a unit");
    step({{"command", "drawing_sheet.update"},
          {"sheet", text(second)},
          {"dimensionFormat", QJsonObject{{"unit", "m"}, {"decimals", 2}}}},
         "update dimension format");
    step({{"command", "drawing_sheet.update"},
          {"sheet", text(second)},
          {"dimensionFormat", QJsonObject{{"showUnit", true}}}},
         "update dimension format again");
    check(doc.drawingSheets().at(second)->dimensionFormat ==
                  DrawingDimensionFormat{DrawingDimensionUnit::Metres, 2, true} &&
              doc.drawingSheets().at(second)->number == "A-201",
          "Dimension format fields merge and the other sheet fields stay");
    for (const auto &bad : {QJsonObject{{"unit", "yd"}}, QJsonObject{{"decimals", 5}},
                            QJsonObject{{"precision", 1}}, QJsonObject{}})
        rejects(
            [&] {
                run(doc, {QJsonObject{{"command", "drawing_sheet.update"},
                                      {"sheet", text(second)},
                                      {"dimensionFormat", bad}}});
            },
            "invalid dimension format");
    const auto note = created(step(
        createItem(f.sheet, "text", {{"frame", frame(300, 20, 80, 20)}, {"text", "General notes"}}),
        "create text"));
    const auto callout = created(step(
        createItem(f.sheet, "label",
                   {{"text", "Slab edge"},
                    {"textAt", QJsonObject{{"xMm", 250}, {"yMm", 60}}},
                    {"target", QJsonObject{{"kind", "view"},
                                           {"view", text(f.view)},
                                           {"anchor", edge(f.body,
                                                           edgeBetween(doc, f.body, {0, 0, 0},
                                                                       {2, 0, 0}),
                                                           .5)}}}}),
        "create view label"));
    const auto dimension =
        created(step(createItem(f.sheet, "dimension", f.dimension()), "create dimension"));
    const auto rule = created(step(createItem(f.sheet, "line",
                                              {{"from", QJsonArray{20, 200}},
                                               {"to", QJsonArray{220, 200}},
                                               {"dash", "dashed"}}),
                                   "create line"));
    const auto box = created(step(createItem(f.sheet, "rectangle", {{"frame", frame(10, 10, 400, 277)}}),
                                  "create rectangle"));
    const auto title = created(step(createItem(f.sheet, "titleBlock",
                                               {{"frame", frame(290, 240, 120, 47)},
                                                {"fields", QJsonObject{{"project", "House"}}}}),
                                    "create title block"));
    check(doc.drawingItems().size() == 6 && doc.drawingItems().at(title)->z == 5,
          "Items stack in creation order");
    const auto &stored = std::get<DrawingDimensionItem>(doc.drawingItems().at(dimension)->data);
    check(stored.anchors[0].state == AnchorState::Resolved &&
              stored.lastMm[0] != DrawingPointMm{} && stored.lastMm[1] != DrawingPointMm{},
          "Create stores resolved anchors and their paper positions");
    step(updateItem(note, "text", {{"text", "Revised notes"}, {"align", "center"}}), "update text");
    step(updateItem(title, "titleBlock", {{"fields", QJsonObject{{"revision", "B"}}}}),
         "update title block");
    const auto &block = std::get<DrawingTitleBlockItem>(doc.drawingItems().at(title)->data);
    check(block.fields.project == "House" && block.fields.revision == "B",
          "Title block fields merge one by one");
    step(updateItem(dimension, "dimension", {{"offsetMm", -8}, {"prefix", "W"}}),
         "update dimension");
    step({{"command", "drawing_item.move"}, {"item", text(rule)}, {"sheet", text(second)}},
         "move line");
    check(doc.drawingItems().at(rule)->sheet == second && doc.drawingItems().at(rule)->z == 0 &&
              doc.drawingItems().at(title)->z == 4,
          "Move keeps the ID and restacks both sheets");
    step({{"command", "drawing_item.reorder"},
          {"sheet", text(f.sheet)},
          {"order", QJsonArray{text(box), text(note), text(callout), text(dimension),
                               text(title)}}},
         "reorder");
    check(doc.drawingItems().at(box)->z == 0 && doc.drawingItems().at(title)->z == 4,
          "Reorder sets the z order");
    step({{"command", "drawing_item.reattach"},
          {"item", text(dimension)},
          {"anchors", QJsonArray{vertex(f.body, vertexAt(doc, f.body, {0, 0, 0})),
                                 vertex(f.body, vertexAt(doc, f.body, {0, 1, 0}))}}},
         "reattach");
    step({{"command", "drawing_item.delete"}, {"item", text(callout)}}, "delete");
    check(!doc.drawingItems().contains(callout) && doc.drawingItems().at(title)->z == 3,
          "Delete restacks the sheet");
    for (const auto &entry : commandCatalog()) {
        const auto name = entry.toObject()["name"].toString();
        if (isDrawingItemCommand(name) || name == "drawing_sheet.update")
            check(covered.contains(name), "Covered page operation " + name.toStdString());
        check(isDrawingSheetCommand(name) ==
                  (name.startsWith("drawing_sheet.") || name.startsWith("drawing_view.") ||
                   name.startsWith("drawing_item.")),
              "Sheet command predicate matches the catalogue");
        check(isDrawingItemCommand(name) == name.startsWith("drawing_item."),
              "Item command predicate matches the catalogue");
    }
    check(covered.size() == 8, "Seven item operations and the sheet update are covered");
    std::cout << "every page operation is one sheet-only entry with pointer-identical model: ok\n";
}

// Atomic batches, no-ops, stale revisions, refusals and the sheet-only batch rule.
void batchesAreAtomic() {
    Fixture f;
    auto &doc = f.doc;
    run(doc, {createItem(f.sheet, "text", {{"frame", frame(300, 20, 80, 20)}, {"text", "Notes"}}),
              createItem(f.sheet, "dimension", f.dimension())});
    doc.markSaved();
    const auto bytes = encodeContainer(doc);
    const auto revision = doc.revision();
    const auto total = doc.history().total;
    auto unchanged = [&](const std::string &what) {
        check(encodeContainer(doc) == bytes && doc.revision() == revision &&
                  doc.history().total == total && !doc.dirty(),
              what + " leaves the document untouched");
    };
    rejects([&] {
        run(doc, {createItem(f.sheet, "text", {{"frame", frame(0, 0, 10, 10)}, {"text", "A"}}),
                  QJsonObject{{"command", "drawing_item.delete"}, {"item", "99"}}});
    }, "batch with a failing second operation");
    unchanged("A failed batch");
    const auto mixed = rejects([&] {
        run(doc, {updateItem(1, "text", {{"text", "Mixed"}}),
                  QJsonObject{{"command", "geometry.translate"}, {"body", text(f.body)},
                              {"delta", QJsonArray{1, 0, 0}}}});
    }, "mixed item and model batch");
    check(mixed.find("cannot share a batch") != std::string::npos, "Mixed batch is named");
    unchanged("A mixed batch");
    const auto noop =
        rejects([&] { run(doc, {updateItem(1, "text", {{"text", "Notes"}})}); }, "same text");
    check(noop == "Batch has no committed changes", "No-op is reported: " + noop);
    rejects([&] {
        run(doc, {QJsonObject{{"command", "drawing_sheet.update"}, {"sheet", "1"},
                              {"number", "A-101"}}});
    }, "same sheet number");
    rejects([&] {
        run(doc, {QJsonObject{{"command", "drawing_item.reorder"}, {"sheet", "1"},
                              {"order", QJsonArray{"1", "2"}}}});
    }, "same z order");
    unchanged("A no-op");
    auto stale = batch(doc, {updateItem(1, "text", {{"text", "Later"}})});
    stale["expectedRevision"] = QString::number(revision - 1);
    check(rejects([&] { executeBatch(doc, stale); }, "stale revision") == "STALE_REVISION",
          "Stale expectedRevision is rejected");
    unchanged("A stale request");
    const auto perspective = rejects([&] {
        auto data = f.dimension();
        data["view"] = text(f.perspectiveView);
        run(doc, {createItem(f.sheet, "dimension", data)});
    }, "dimension on a perspective view");
    check(perspective.rfind("DRAWING_ITEM_NOT_MEASURABLE: ", 0) == 0,
          "A perspective view is not measurable: " + perspective);
    unchanged("A refused dimension");
    for (const auto &bad : {
             createItem(f.sheet, "text", {{"frame", frame(0, 0, 10, 10)}}),
             createItem(f.sheet, "text", {{"frame", frame(0, 0, 10, 10)}, {"text", "A"}, {"x", 1}}),
             createItem(f.sheet, "text", {{"frame", frame(415, 0, 10, 10)}, {"text", "Off"}}),
             createItem(f.sheet, "text", {{"frame", frame(0, 0, 10, 10)}, {"text", "A"},
                                          {"font", QJsonObject{{"family", "/etc/font.ttf"},
                                                               {"sizePt", 10}}}}),
             createItem(f.sheet, "line", {{"from", QJsonArray{0, 0}}, {"to", QJsonArray{0, 0}}}),
             createItem(f.sheet, "line", {{"from", QJsonArray{0, 0}}, {"to", QJsonArray{10, 0}},
                                          {"strokeMm", 5}}),
             createItem(f.sheet, "sticker", {{"frame", frame(0, 0, 10, 10)}}),
             createItem(9, "rectangle", {{"frame", frame(0, 0, 10, 10)}}),
             createItem(f.sheet, "dimension",
                        {{"view", "1"},
                         {"anchors", QJsonArray{vertex(f.body, 1)}}}),
             createItem(f.sheet, "dimension",
                        {{"view", "1"},
                         {"anchors", QJsonArray{vertex(f.body, 1), vertex(f.body, 999)}}}),
             createItem(f.sheet, "dimension",
                        {{"view", "1"},
                         {"anchors",
                          QJsonArray{vertex(f.body, 1),
                                     QJsonObject{{"kind", "point"}, {"space", "world"},
                                                 {"point", QJsonArray{0, 0, 0}}}}}}),
             createItem(f.sheet, "dimension", {{"view", "1"}, {"anchors", f.dimension()["anchors"]},
                                               {"lastMm", QJsonArray{}}}),
             createItem(f.sheet, "titleBlock", {{"frame", frame(0, 0, 50, 20)},
                                                {"fields", QJsonObject{{"sheetNumber", "X"}}}}),
             updateItem(1, "rectangle", {{"frame", frame(0, 0, 10, 10)}}),
             updateItem(2, "dimension", {{"view", "1"}}),
             updateItem(2, "dimension", {{"anchors", f.dimension()["anchors"]}}),
             QJsonObject{{"command", "drawing_item.move"}, {"item", "2"}, {"sheet", "1"}},
             QJsonObject{{"command", "drawing_item.reorder"}, {"sheet", "1"},
                         {"order", QJsonArray{"1"}}},
             QJsonObject{{"command", "drawing_item.reattach"}, {"item", "1"},
                         {"anchors", f.dimension()["anchors"]}},
             QJsonObject{{"command", "drawing_item.reattach"}, {"item", "2"},
                         {"anchors", QJsonArray{vertex(f.body, 1)}}},
             QJsonObject{{"command", "drawing_sheet.update"}, {"sheet", "1"}},
             QJsonObject{{"command", "drawing_sheet.update"}, {"sheet", "1"}, {"marginMm", 80}},
             createSheet("Twin", {{"number", "A-101"}})}) {
        rejects([&] { run(doc, {bad}); }, "invalid " + QJsonDocument(bad).toJson().toStdString());
        unchanged("An invalid command");
    }
    std::cout << "atomic batches, no-ops, stale revisions and refusals: ok\n";
}

// Undo and Redo restore item records exactly; a multi-item batch is one entry, and
// a labelled re-entry amends the last sheet entry (ToolSession's amend stamp).
void undoRedoAndAmend() {
    Fixture f;
    auto &doc = f.doc;
    const auto before = doc.drawingItems();
    run(doc, {createItem(f.sheet, "line", {{"from", QJsonArray{20, 200}}, {"to", QJsonArray{60, 200}}}),
              createItem(f.sheet, "line", {{"from", QJsonArray{20, 210}}, {"to", QJsonArray{60, 210}}}),
              createItem(f.sheet, "dimension", f.dimension())});
    const auto after = doc.drawingItems();
    check(after.size() == 3 && lastLabel(doc) == "Command batch (3 operations)",
          "A multi-item batch is one entry");
    doc.undo();
    check(doc.drawingItems() == before, "Undo removes every item of the batch");
    doc.redo();
    check(doc.drawingItems() == after, "Redo restores the same records");
    // Nudge: the first move is an entry; re-entries amend it.
    auto nudge = [&](double y) {
        auto request = batch(doc, {updateItem(1, "line", {{"from", QJsonArray{20, y}},
                                                          {"to", QJsonArray{60, y}}}),
                                   updateItem(2, "line", {{"from", QJsonArray{20, y + 10}},
                                                          {"to", QJsonArray{60, y + 10}}})});
        request["history"] = QJsonObject{{"label", "Nudge"}};
        return request;
    };
    executeBatch(doc, nudge(201));
    const auto total = doc.history().total;
    const auto model = ModelRecords(doc);
    for (double y : {202., 203., 204.}) {
        const auto stamp = doc.amendmentStamp();
        const auto result = executeAmend(doc, stamp, nudge(y));
        check(result["amended"] == true, "Amend reports the amendment");
    }
    check(doc.history().total == total && lastLabel(doc) == "Nudge" &&
              std::get<DrawingLineItem>(doc.drawingItems().at(1)->data).from[1] == 204 &&
              std::get<DrawingLineItem>(doc.drawingItems().at(2)->data).from[1] == 214 &&
              ModelRecords(doc) == model,
          "Consecutive nudges amend one sheet-only entry");
    doc.undo();
    check(std::get<DrawingLineItem>(doc.drawingItems().at(1)->data).from[1] == 200,
          "One undo reverts every amended nudge");
    doc.redo();
    // An amendment cannot reach outside the entry's records.
    const auto stamp = doc.amendmentStamp();
    rejects([&] {
        executeAmend(doc, stamp, batch(doc, {updateItem(3, "dimension", {{"prefix", "X"}})}));
    }, "amend another item");
    rejects([&] {
        executeAmend(doc, stamp,
                     batch(doc, {QJsonObject{{"command", "geometry.translate"},
                                             {"body", text(f.body)},
                                             {"delta", QJsonArray{1, 0, 0}}}}));
    }, "amend a sheet entry into a model edit");
    check(doc.history().total == total, "Refused amendments leave the history");
    std::cout << "undo, redo and amend of sheet entries: ok\n";
}

// Paged queries with an optional sheet filter, descriptions with evaluated state,
// title-block fields, and the reattach-candidate query.
void inspectionQueries() {
    Fixture f;
    auto &doc = f.doc;
    run(doc, {createSheet("Details", {{"number", "A-102"}}),
              createItem(f.sheet, "titleBlock", {{"frame", frame(290, 240, 120, 47)}}),
              createItem(f.sheet, "dimension", f.dimension()),
              createItem(2, "text", {{"frame", frame(10, 10, 50, 10)}, {"text", "Details"}}),
              createItem(f.sheet, "label",
                         {{"text", "Corner"},
                          {"textAt", QJsonObject{{"xMm", 250}, {"yMm", 60}}},
                          {"target", QJsonObject{{"kind", "paper"}, {"xMm", 200}, {"yMm", 80}}}})});
    auto request = query(doc, "drawing_items.query");
    request["limit"] = 1;
    QJsonArray rows;
    for (;;) {
        const auto page = inspectDocument(doc, request)["data"].toObject();
        check(page["items"].toArray().size() <= 1 && page["total"] == 4 &&
                  page["revision"] == QString::number(doc.revision()),
              "Item pages are bounded and revision-bound");
        for (const auto &row : page["items"].toArray())
            rows.append(row);
        if (page["nextCursor"].isNull())
            break;
        request["cursor"] = page["nextCursor"];
    }
    check(rows.size() == 4 && rows[0].toObject()["id"] == "1" && rows[2].toObject()["id"] == "4" &&
              rows[3].toObject()["id"] == "3",
          "Paging visits items in sheet order, bottom first");
    check(rows[1].toObject()["kind"] == "dimension" && rows[1].toObject()["state"] == "current" &&
              rows[1].toObject()["reasons"].toArray().isEmpty(),
          "Rows carry the derived state");
    check(inspect(doc, "drawing_items.query", {{"sheet", "2"}})["total"] == 1,
          "Items can be limited to one sheet");
    rejects([&] { inspect(doc, "drawing_items.query", {{"sheet", "9"}}); }, "unknown sheet");
    check(inspect(doc, "drawing_sheets.query")["items"].toArray()[0].toObject()["items"] == 3,
          "Sheet summaries count items");
    check(inspect(doc, "drawing_sheet.describe", {{"sheet", "1"}})["items"] == 3,
          "Sheet descriptions count items");
    const auto dimension = inspect(doc, "drawing_item.describe", {{"item", "2"}});
    const auto evaluation = dimension["evaluation"].toObject();
    check(evaluation["state"] == "current" && evaluation["valueMetres"] == 2 &&
              std::abs(evaluation["paperLengthMm"].toDouble() - 40) < 1e-9 &&
              evaluation["text"] == "2000" && evaluation["lineMm"].toArray().size() == 2 &&
              dimension["data"].toObject()["view"] == "1",
          "Describe returns the measured value and paper geometry");
    const auto block = inspect(doc, "drawing_item.describe", {{"item", "1"}})["evaluation"]
                           .toObject()["titleBlock"]
                           .toObject();
    check(block["sheetNumber"] == "A-101" && block["page"] == "1 of 2" &&
              block["scale"] == "1:50 @ A3",
          "Title-block fields are derived");
    run(doc, {createView("Detail", f.sheet, f.plan),
              QJsonObject{{"command", "drawing_view.update"}, {"view", "3"},
                          {"frame", frame(230, 20, 100, 100)}, {"scaleDenominator", 20}}});
    check(evaluateDrawingItem(doc, 1).titleBlock->scale == "As indicated",
          "Mixed scales read As indicated");
    run(doc, {QJsonObject{{"command", "drawing_view.delete"}, {"view", "3"}},
              QJsonObject{{"command", "drawing_view.delete"}, {"view", "1"}}});
    check(!doc.drawingItems().contains(2), "Deleting a view deletes its dimensions");
    check(evaluateDrawingItem(doc, 1).titleBlock->scale == "Not to scale",
          "A perspective-only page is not to scale");
    run(doc, {QJsonObject{{"command", "drawing_view.delete"}, {"view", "2"}}});
    check(evaluateDrawingItem(doc, 1).titleBlock->scale == "—", "A page without views reads —");
    const auto label = inspect(doc, "drawing_item.describe", {{"item", "4"}})["evaluation"].toObject();
    check(label["tipMm"].toArray() == QJsonArray{200, 80} && label["text"] == "Corner",
          "A paper label's tip is its target");
    rejects([&] { inspect(doc, "drawing_item.describe", {{"item", "99"}}); }, "unknown item");
    check(rejects([&] { inspect(doc, "drawing_item.reattach_candidates", {{"item", "4"}}); },
                  "paper label reattach")
                  .rfind("INVALID_REQUEST", 0) == 0,
          "Only view-bound items have reattach candidates");
    std::cout << "paged item queries, descriptions and title-block fields: ok\n";
}

void componentScopeAndStaging() {
    Fixture f;
    auto &doc = f.doc;
    run(doc, {createItem(f.sheet, "text", {{"frame", frame(10, 10, 50, 10)}, {"text", "A"}})});
    const auto component = createComponent(doc, f.body, "Scoped");
    const auto bytes = encodeContainer(doc);
    for (const auto &nested :
         {createItem(f.sheet, "text", {{"frame", frame(10, 10, 50, 10)}, {"text", "B"}}),
          QJsonObject{{"command", "drawing_item.delete"}, {"item", "1"}},
          QJsonObject{{"command", "drawing_sheet.update"}, {"sheet", "1"}, {"marginMm", 5}}}) {
        const auto message = rejects([&] {
            run(doc, {componentScopeCommand(doc, component.instance, {nested})});
        }, "page operation inside a component scope");
        check(message.find("separate explicit scope") != std::string::npos,
              "Component scope rejects page operations: " + message);
    }
    check(encodeContainer(doc) == bytes, "Rejected scoped page edit changes nothing");
    StagingSession staging;
    const auto prepared = staging.prepare(
        doc, batch(doc, {createItem(f.sheet, "line", {{"from", QJsonArray{0, 0}},
                                                      {"to", QJsonArray{10, 0}}}),
                         updateItem(1, "text", {{"text", "Staged"}})}));
    const auto stage = prepared["stageId"].toString();
    const auto changes = staging.changes(doc, stage)["changes"].toArray();
    check(changes.size() == 2 && changes[0].toObject()["kind"] == "drawing_item" &&
              changes[1].toObject()["kind"] == "drawing_item" &&
              prepared["createdIds"].toObject()["createdDrawingItems"].toArray() == QJsonArray{"2"},
          "Staged item edits name their resources");
    check(encodeContainer(doc) == bytes, "Staging leaves the live document");
    doc.applyPrepared(*staging.proposal(doc, stage));
    check(doc.drawingItems().size() == 2, "A staged item batch publishes");
    std::cout << "component scope rejection and staging: ok\n";
}

#ifdef CLI_PATH
struct CliResult {
    int code{};
    QJsonObject json;
    QString error;
};
CliResult cli(const QStringList &arguments) {
    QProcess process;
    process.start(QStringLiteral(CLI_PATH), arguments);
    check(process.waitForFinished(60000), "CLI finishes");
    CliResult result;
    result.code = process.exitCode();
    result.json = QJsonDocument::fromJson(process.readAllStandardOutput()).object();
    result.error = QString::fromUtf8(process.readAllStandardError());
    return result;
}
QString write(const QTemporaryDir &dir, const char *name, const QJsonDocument &json) {
    const auto path = dir.filePath(name);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(json.toJson()) > 0, "Write CLI input");
    return path;
}
// A script places items through the CLI; the saved file is schema 27 with the
// items, --inspect-native counts them and drawing_items.query reads them back.
void cliRoundTrip() {
    QTemporaryDir dir;
    check(dir.isValid(), "Temporary directory");
    const auto model = write(
        dir, "model.json",
        QJsonDocument(QJsonArray{
            QJsonObject{{"command", "geometry.face"},
                        {"loops", QJsonArray{QJsonArray{QJsonArray{0, 0, 0}, QJsonArray{2, 0, 0},
                                                        QJsonArray{2, 1, 0},
                                                        QJsonArray{0, 1, 0}}}}},
            QJsonObject{{"command", "saved_scene.create"},
                        {"name", "Plan"},
                        {"snapshot", encodeSceneSnapshot(planScene())}}}));
    const auto base = dir.filePath("base.sketchyup"), sheeted = dir.filePath("items.sketchyup");
    auto created = cli({"--script", model, "--output", base});
    check(created.code == 0, "CLI builds the model: " + created.error.toStdString());
    // Read the vertex IDs back from the file the CLI wrote.
    QFile baseFile(base);
    check(baseFile.open(QIODevice::ReadOnly), "Read the CLI output");
    const Document reopened = decodeContainer(baseFile.readAll());
    const Id body = reopened.bodies().begin()->first;
    const auto items = write(
        dir, "items.json",
        QJsonDocument(QJsonArray{
            createSheet("Ground floor", {{"number", "A-101"}}), createView("Plan view", 1, 1),
            createItem(1, "titleBlock", {{"frame", frame(290, 240, 120, 47)},
                                         {"fields", QJsonObject{{"project", "House"}}}}),
            createItem(1, "dimension",
                       {{"view", "1"},
                        {"anchors",
                         QJsonArray{vertex(body, vertexAt(reopened, body, {0, 0, 0})),
                                    vertex(body, vertexAt(reopened, body, {2, 0, 0}))}}})}));
    created = cli({"--input", base, "--script", items, "--output", sheeted});
    check(created.code == 0, "CLI adds page items: " + created.error.toStdString());
    const auto native = cli({"--inspect-native", sheeted}).json;
    check(native["documentVersion"] == 27 && native["drawingSheets"] == 1 &&
              native["drawingViews"] == 1 && native["drawingItems"] == 2,
          "--inspect-native counts the items in schema 27");
    const auto request = write(
        dir, "query.json",
        QJsonDocument(QJsonObject{{"apiVersion", 1},
                                  {"documentId", native["documentId"]},
                                  {"expectedRevision", native["revision"]},
                                  {"query", "drawing_items.query"}}));
    const auto inspected = cli({"--input", sheeted, "--inspect-file", request});
    const auto rows = inspected.json["data"].toObject()["items"].toArray();
    check(inspected.code == 0 && rows.size() == 2 &&
              rows[0].toObject()["kind"] == "titleBlock" &&
              rows[1].toObject()["state"] == "current",
          "The saved items reopen: " + inspected.error.toStdString());
    const auto perspective = write(
        dir, "perspective.json",
        QJsonDocument(QJsonArray{QJsonObject{{"command", "saved_scene.update"},
                                             {"scene", "1"},
                                             {"snapshot", encodeSceneSnapshot(planScene(false))}}}));
    const auto unscaled = dir.filePath("unscaled.sketchyup");
    check(cli({"--input", sheeted, "--script", perspective, "--output", unscaled}).code == 0,
          "The scene becomes perspective");
    const auto dimension = write(
        dir, "dimension.json",
        QJsonDocument(QJsonArray{createItem(
            1, "dimension",
            {{"view", "1"},
             {"anchors", QJsonArray{vertex(body, vertexAt(reopened, body, {0, 0, 0})),
                                    vertex(body, vertexAt(reopened, body, {0, 1, 0}))}}})}));
    const auto refused = cli({"--input", unscaled, "--script", dimension});
    check(refused.code == 1 && refused.error.contains("DRAWING_ITEM_NOT_MEASURABLE"),
          "The CLI reports the structured refusal: " + refused.error.toStdString());
    std::cout << "CLI script round trip with items to schema 27: ok\n";
}
#endif
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        everyOperationIsSheetOnly();
        batchesAreAtomic();
        undoRedoAndAmend();
        inspectionQueries();
        componentScopeAndStaging();
#ifdef CLI_PATH
        cliRoundTrip();
#endif
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
