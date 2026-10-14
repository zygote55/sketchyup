#include "automation/commands.hpp"
#include "automation/component_scope.hpp"
#include "automation/drawing_sheet_commands.hpp"
#include "automation/inspection.hpp"
#include "automation/staging.hpp"
#include "core/components.hpp"
#include "core/scenes.hpp"
#include "io/document_io.hpp"
#include "io/drawing_view.hpp"
#include "io/scenes_io.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <functional>
#include <type_traits>
#include <iostream>
#include <string>
using namespace sketchy;
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
QJsonObject source(Id scene) { return {{"kind", "scene"}, {"scene", text(scene)}}; }
QJsonObject createView(const char *name, Id sheet, Id scene) {
    return {{"command", "drawing_view.create"},
            {"name", name},
            {"sheet", text(sheet)},
            {"source", source(scene)},
            {"frame", frame(20, 20, 200, 150)},
            {"offsetMm", QJsonArray{10, -5}},
            {"scaleDenominator", 50}};
}
QJsonObject viewCommand(const char *name, Id view, QJsonObject extra = {}) {
    extra["command"] = name;
    extra["view"] = text(view);
    return extra;
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
DrawingViewStatus status(const Document &doc, Id view) { return evaluateDrawingView(doc, view); }
// Every model record table, compared by pointer: sheet edits must leave each
// shared immutable record exactly where it was.
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
// A 2 m x 1 m slab with an orthographic plan scene and a perspective scene.
struct Fixture {
    Document doc;
    Id body{}, plan{}, perspective{};
    Fixture() {
        body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        plan = createScene(doc, "Plan", planScene());
        perspective = createScene(doc, "Perspective", planScene(false));
    }
};

// Each operation is one labeled, sheet-only history entry that leaves every model
// record pointer-identical.
void everyOperationIsSheetOnly() {
    Fixture f;
    auto &doc = f.doc;
    const auto model = ModelRecords(doc);
    const auto style = doc.style();
    const auto solar = doc.solar();
    const auto units = doc.displayUnits();
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
        check(ModelRecords(doc) == model && doc.style() == style && doc.solar() == solar &&
                  doc.displayUnits() == units && doc.nextId() == nextId,
              label + " leaves every model record pointer-identical");
        covered.insert(command["command"].toString());
        return result;
    };
    const auto a3 = step({{"command", "drawing_sheet.create"}, {"name", "A3"},
                          {"paper", paper(420, 297)}},
                         "create sheet")["createdDrawingSheets"]
                        .toArray()[0]
                        .toString()
                        .toULongLong();
    const auto a4 = step({{"command", "drawing_sheet.create"}, {"name", "A4"},
                          {"paper", paper(297, 210)}},
                         "create second sheet")["createdDrawingSheets"]
                        .toArray()[0]
                        .toString()
                        .toULongLong();
    check(a3 && a4 && a4 > a3, "Created sheet IDs are reported");
    step({{"command", "drawing_sheet.rename"}, {"sheet", text(a3)}, {"name", "Plans"}}, "rename");
    step({{"command", "drawing_sheet.resize"}, {"sheet", text(a3)}, {"paper", paper(594, 420)}},
         "resize");
    step({{"command", "drawing_sheet.reorder"}, {"order", QJsonArray{text(a4), text(a3)}}},
         "reorder");
    check(orderedDrawingSheets(doc) == std::vector<Id>{a4, a3}, "Reorder publishes the order");
    const auto view = step(createView("Ground plan", a3, f.plan), "create view")
                          ["createdDrawingViews"]
                              .toArray()[0]
                              .toString()
                              .toULongLong();
    check(!doc.drawingViews().at(view)->accepted, "A new view starts unaccepted");
    step(viewCommand("drawing_view.accept", view), "accept");
    step(viewCommand("drawing_view.update", view, {{"frame", frame(30, 30, 180, 140)}}),
         "update");
    step(viewCommand("drawing_view.move", view, {{"sheet", text(a4)}}), "move");
    check(doc.drawingViews().at(view)->sheet == a4, "Move keeps the view ID");
    // Accept by sheet and accept all stamp only views that are not yet current.
    step(createView("Section", a4, f.plan), "create second view");
    step({{"command", "drawing_view.accept"}, {"sheet", text(a4)}}, "accept sheet");
    step(createView("Elevation", a3, f.plan), "create third view");
    step({{"command", "drawing_view.accept"}, {"all", true}}, "accept all");
    for (const auto &[id, record] : doc.drawingViews())
        check(status(doc, id).state == DrawingViewState::Current, "Every view is accepted");
    step(viewCommand("drawing_view.delete", view), "delete view");
    step({{"command", "drawing_sheet.delete"}, {"sheet", text(a3)}}, "delete sheet");
    check(!doc.drawingSheets().contains(a3) && doc.drawingViews().size() == 1,
          "Deleting a sheet deletes its views in the same entry");
    for (const auto &entry : commandCatalog()) {
        const auto name = entry.toObject()["name"].toString();
        if (isDrawingSheetCommand(name))
            check(covered.contains(name), "Covered sheet operation " + name.toStdString());
        check(isDrawingSheetCommand(name) ==
                  (name.startsWith("drawing_sheet.") || name.startsWith("drawing_view.")),
              "Sheet command predicate matches the catalogue");
    }
    check(covered.size() == 10, "Ten sheet operations are registered");
    std::cout << "every operation is one sheet-only entry with pointer-identical model: ok\n";
}

// Atomic batches, no-ops, stale revisions and the sheet-only batch rule.
void batchesAreAtomic() {
    Fixture f;
    auto &doc = f.doc;
    run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                          {"paper", paper(420, 297)}}});
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
        run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "B"},
                              {"paper", paper(297, 210)}},
                  QJsonObject{{"command", "drawing_sheet.rename"}, {"sheet", "999"},
                              {"name", "Missing"}}});
    }, "batch with a failing second operation");
    unchanged("A failed batch");
    rejects([&] {
        run(doc, {createView("Plan", 1, f.plan),
                  QJsonObject{{"command", "drawing_view.create"}, {"name", "Plan"},
                              {"sheet", "1"}, {"source", source(f.plan)},
                              {"frame", frame(0, 0, 50, 50)}, {"scaleDenominator", 10}}});
    }, "duplicate view names in one batch");
    unchanged("A batch that fails final validation");
    const auto mixed = rejects([&] {
        run(doc, {QJsonObject{{"command", "drawing_sheet.rename"}, {"sheet", "1"},
                              {"name", "Renamed"}},
                  QJsonObject{{"command", "geometry.translate"}, {"body", text(f.body)},
                              {"delta", QJsonArray{1, 0, 0}}}});
    }, "mixed sheet and model batch");
    check(mixed.find("cannot share a batch") != std::string::npos, "Mixed batch is named");
    unchanged("A mixed batch");
    const auto noop = rejects([&] {
        run(doc, {QJsonObject{{"command", "drawing_sheet.rename"}, {"sheet", "1"},
                              {"name", "A3"}}});
    }, "rename to the same name");
    check(noop == "Batch has no committed changes", "No-op is reported: " + noop);
    rejects([&] {
        run(doc, {QJsonObject{{"command", "drawing_sheet.resize"}, {"sheet", "1"},
                              {"paper", paper(420, 297)}}});
    }, "resize to the same paper");
    unchanged("A no-op");
    auto stale = batch(doc, {QJsonObject{{"command", "drawing_sheet.rename"}, {"sheet", "1"},
                                         {"name", "Later"}}});
    stale["expectedRevision"] = QString::number(revision - 1);
    check(rejects([&] { executeBatch(doc, stale); }, "stale revision") == "STALE_REVISION",
          "Stale expectedRevision is rejected");
    unchanged("A stale request");
    for (const auto &bad :
         {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "Tiny"}, {"paper", paper(5, 5)}},
          QJsonObject{{"command", "drawing_sheet.create"}, {"name", " Space"},
                      {"paper", paper(100, 100)}},
          QJsonObject{{"command", "drawing_sheet.create"}, {"name", "Extra"},
                      {"paper", QJsonObject{{"widthMm", 100}, {"heightMm", 100}, {"x", 1}}}},
          QJsonObject{{"command", "drawing_sheet.reorder"}, {"order", QJsonArray{}}},
          QJsonObject{{"command", "drawing_view.create"}, {"name", "Outside"}, {"sheet", "1"},
                      {"source", source(f.plan)}, {"frame", frame(400, 0, 100, 100)},
                      {"scaleDenominator", 50}},
          QJsonObject{{"command", "drawing_view.create"}, {"name", "No scene"}, {"sheet", "1"},
                      {"source", source(999)}, {"frame", frame(0, 0, 100, 100)},
                      {"scaleDenominator", 50}},
          QJsonObject{{"command", "drawing_view.create"}, {"name", "No sheet"}, {"sheet", "9"},
                      {"source", source(f.plan)}, {"frame", frame(0, 0, 100, 100)},
                      {"scaleDenominator", 50}},
          QJsonObject{{"command", "drawing_view.create"}, {"name", "Zero scale"}, {"sheet", "1"},
                      {"source", source(f.plan)}, {"frame", frame(0, 0, 100, 100)},
                      {"scaleDenominator", 0}},
          QJsonObject{{"command", "drawing_view.accept"}},
          QJsonObject{{"command", "drawing_view.accept"}, {"all", false}},
          QJsonObject{{"command", "drawing_view.accept"}, {"all", true}, {"sheet", "1"}},
          QJsonObject{{"command", "drawing_view.update"}, {"view", "1"}}}) {
        rejects([&] { run(doc, {bad}); }, "invalid " + QJsonDocument(bad).toJson().toStdString());
        unchanged("An invalid command");
    }
    std::cout << "atomic batches, no-ops, stale revisions and validation: ok\n";
}

// Accept stamps {base revision, fingerprint}; Undo and Redo restore exactly.
void acceptUndoRedo() {
    Fixture f;
    auto &doc = f.doc;
    run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                          {"paper", paper(420, 297)}},
              createView("Plan", 1, f.plan)});
    const Id view = 1;
    check(status(doc, view).reasons == std::vector<std::string>{"notAccepted"},
          "Unaccepted view is stale");
    const auto base = doc.revision();
    const auto fingerprint = status(doc, view).fingerprint;
    run(doc, {viewCommand("drawing_view.accept", view)});
    const auto stamp = doc.drawingViews().at(view)->accepted;
    check(stamp && stamp->revision == base && stamp->fingerprint == fingerprint,
          "Accept stamps the evaluated revision and fingerprint");
    check(status(doc, view).state == DrawingViewState::Current, "Accepted view is current");
    check(rejects([&] { run(doc, {viewCommand("drawing_view.accept", view)}); },
                  "accept a current view") == "Batch has no committed changes",
          "Accepting a current view is a reported no-op");
    doc.undo();
    check(!doc.drawingViews().at(view)->accepted &&
              status(doc, view).state == DrawingViewState::Stale,
          "Undo removes the stamp");
    doc.redo();
    check(doc.drawingViews().at(view)->accepted == stamp &&
              status(doc, view).state == DrawingViewState::Current,
          "Redo restores the same stamp");
    // A model edit makes it stale; accepting again re-stamps at the new revision.
    run(doc, {QJsonObject{{"command", "geometry.translate"}, {"body", text(f.body)},
                          {"delta", QJsonArray{.25, 0, 0}}}});
    check(status(doc, view).reasons == std::vector<std::string>{"changedSinceAccepted"} &&
              doc.drawingViews().at(view)->accepted == stamp,
          "A model edit never re-stamps");
    const auto moved = doc.revision();
    run(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}});
    check(doc.drawingViews().at(view)->accepted->revision == moved &&
              status(doc, view).state == DrawingViewState::Current,
          "Accept all re-stamps a changed view");
    std::cout << "accept, undo and redo: ok\n";
}

// The R090.d re-stamp rule: a change to what a current view draws re-stamps it;
// stale and never-accepted views keep their stamp; a name change keeps it.
void restampRule() {
    Fixture f;
    auto &doc = f.doc;
    run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                          {"paper", paper(420, 297)}},
              QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A4"},
                          {"paper", paper(297, 210)}},
              createView("Plan", 1, f.plan), createView("Draft", 1, f.plan)});
    const Id view = 1, draft = 2;
    run(doc, {viewCommand("drawing_view.accept", view)});
    const auto first = *doc.drawingViews().at(view)->accepted;
    const auto base = doc.revision();
    run(doc, {viewCommand("drawing_view.update", view, {{"frame", frame(40, 40, 160, 120)}})});
    const auto second = *doc.drawingViews().at(view)->accepted;
    check(second.fingerprint != first.fingerprint && second.revision == base &&
              second.fingerprint == status(doc, view).fingerprint &&
              status(doc, view).state == DrawingViewState::Current,
          "A frame change re-stamps a current view");
    for (const auto &[field, value] :
         std::vector<std::pair<const char *, QJsonValue>>{
             {"offsetMm", QJsonArray{0, 0}},
             {"scaleDenominator", 100},
             {"content", "rasterAppearance"},
             {"hiddenLines", true},
             {"source", source(f.perspective)}}) {
        const auto before = *doc.drawingViews().at(view)->accepted;
        run(doc, {viewCommand("drawing_view.update", view, {{field, value}})});
        check(status(doc, view).state == DrawingViewState::Current &&
                  doc.drawingViews().at(view)->accepted->fingerprint != before.fingerprint,
              std::string("A change to ") + field + " re-stamps a current view");
    }
    check(status(doc, view).notToScale, "A perspective source is not to scale");
    const auto named = doc.drawingViews().at(view)->accepted;
    run(doc, {viewCommand("drawing_view.update", view, {{"name", "Renamed"}})});
    check(doc.drawingViews().at(view)->accepted == named, "A name change keeps the stamp");
    run(doc, {viewCommand("drawing_view.update", view,
                          {{"source", source(f.plan)}, {"content", "technicalLines"},
                           {"hiddenLines", false}})});
    // A move keeps what the view draws, so it keeps the stamp, even with a new frame
    // that redraws a current view (re-stamped).
    const auto beforeMove = doc.drawingViews().at(view)->accepted;
    run(doc, {viewCommand("drawing_view.move", view, {{"sheet", "2"}})});
    check(doc.drawingViews().at(view)->accepted == beforeMove &&
              status(doc, view).state == DrawingViewState::Current,
          "A move keeps the stamp");
    run(doc, {viewCommand("drawing_view.move", view,
                          {{"sheet", "1"}, {"frame", frame(0, 0, 100, 80)}})});
    check(doc.drawingViews().at(view)->accepted != beforeMove &&
              status(doc, view).state == DrawingViewState::Current,
          "A move with a new frame re-stamps a current view");
    rejects([&] {
        run(doc, {viewCommand("drawing_view.move", view, {{"sheet", "2"},
                                                          {"frame", frame(250, 0, 100, 80)}})});
    }, "move to a frame outside the paper");
    // Stale view: a frame change never accepts the unreviewed model edit.
    run(doc, {QJsonObject{{"command", "geometry.translate"}, {"body", text(f.body)},
                          {"delta", QJsonArray{.25, 0, 0}}}});
    const auto staleStamp = doc.drawingViews().at(view)->accepted;
    run(doc, {viewCommand("drawing_view.update", view, {{"frame", frame(10, 10, 150, 100)}})});
    check(doc.drawingViews().at(view)->accepted == staleStamp &&
              status(doc, view).reasons == std::vector<std::string>{"changedSinceAccepted"},
          "A stale view keeps its stamp and stays stale");
    // Never accepted: stays unaccepted.
    run(doc, {viewCommand("drawing_view.update", draft, {{"scaleDenominator", 20}})});
    check(!doc.drawingViews().at(draft)->accepted &&
              status(doc, draft).reasons == std::vector<std::string>{"notAccepted"},
          "A never-accepted view stays unaccepted");
    std::cout << "re-stamp rule: ok\n";
}

// Accepting a broken view is refused; Accept all skips it.
void brokenViewsAreNotAccepted() {
    Fixture f;
    auto &doc = f.doc;
    const auto doomed = createScene(doc, "Doomed", planScene());
    run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                          {"paper", paper(420, 297)}},
              createView("Plan", 1, f.plan), createView("Doomed view", 1, doomed)});
    eraseScene(doc, doomed);
    const Id healthy = 1, broken = 2;
    check(status(doc, broken).state == DrawingViewState::Broken &&
              status(doc, broken).reasons == std::vector<std::string>{"sceneMissing"},
          "Deleting the scene breaks the view");
    const auto bytes = encodeContainer(doc);
    const auto refused =
        rejects([&] { run(doc, {viewCommand("drawing_view.accept", broken)}); }, "accept broken");
    check(refused.rfind("DRAWING_VIEW_NOT_ACCEPTABLE: ", 0) == 0 &&
              refused.find("sceneMissing") != std::string::npos,
          "Broken accept is a structured refusal: " + refused);
    check(encodeContainer(doc) == bytes, "A refused accept changes nothing");
    rejects([&] {
        run(doc, {viewCommand("drawing_view.accept", healthy),
                  viewCommand("drawing_view.accept", broken)});
    }, "batch with a broken accept");
    check(encodeContainer(doc) == bytes, "A refused accept rolls back its batch");
    run(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"sheet", "1"}}});
    check(doc.drawingViews().at(healthy)->accepted && !doc.drawingViews().at(broken)->accepted,
          "Accepting a sheet skips its broken view");
    rejects([&] { run(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}}); },
            "accept all with nothing acceptable");
    // A broken view stays editable, but its source can only move to an existing scene.
    run(doc, {viewCommand("drawing_view.update", broken, {{"name", "Kept"}})});
    check(rejects([&] {
              run(doc, {viewCommand("drawing_view.update", broken, {{"source", source(999)}})});
          }, "retarget to a missing scene").find("existing scene") != std::string::npos,
          "Changing a source requires an existing scene");
    std::cout << "broken views are refused: ok\n";
}

// Paged, revision-bound status; sheet listing and descriptions.
void inspectionQueries() {
    Fixture f;
    auto &doc = f.doc;
    const auto doomed = createScene(doc, "Doomed", planScene());
    run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                          {"paper", paper(420, 297)}},
              QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A4"},
                          {"paper", paper(297, 210)}},
              createView("Plan", 2, f.plan), createView("Perspective", 1, f.perspective),
              createView("Doomed view", 1, doomed),
              QJsonObject{{"command", "drawing_sheet.reorder"}, {"order", QJsonArray{"2", "1"}}}});
    run(doc, {viewCommand("drawing_view.accept", 1)});
    eraseScene(doc, doomed);
    const auto sheets = inspect(doc, "drawing_sheets.query");
    check(sheets["total"] == 2 &&
              sheets["items"].toArray()[0].toObject()["id"] == "2" &&
              sheets["items"].toArray()[0].toObject()["views"] == 1 &&
              sheets["items"].toArray()[1].toObject()["paper"].toObject()["widthMm"] == 420,
          "Sheets are listed in order with view counts");
    const auto sheet = inspect(doc, "drawing_sheet.describe", {{"sheet", "1"}});
    check(sheet["views"].toArray().size() == 2 &&
              sheet["views"].toArray()[1].toObject()["sourceMissing"] == true &&
              sheet["views"].toArray()[0].toObject()["accepted"].isNull(),
          "Sheet description lists its view records");
    // Page one row at a time.
    auto request = query(doc, "drawing_views.status");
    request["limit"] = 1;
    QJsonArray rows;
    for (;;) {
        const auto page = inspectDocument(doc, request)["data"].toObject();
        check(page["items"].toArray().size() <= 1 && page["total"] == 3 &&
                  page["revision"] == QString::number(doc.revision()),
              "Status pages are bounded and revision-bound");
        for (const auto &row : page["items"].toArray())
            rows.append(row);
        if (page["nextCursor"].isNull())
            break;
        request["cursor"] = page["nextCursor"];
    }
    check(rows.size() == 3, "Paging visits every view once");
    const auto plan = rows[0].toObject(), perspective = rows[1].toObject(),
               broken = rows[2].toObject();
    check(plan["view"] == "1" && plan["state"] == "current" && plan["reasons"].toArray().isEmpty() &&
              plan["notToScale"] == false && plan["fingerprint"].toString().size() == 64,
          "Accepted plan is current");
    check(perspective["state"] == "stale" &&
              perspective["reasons"] == QJsonArray{"notAccepted"} &&
              perspective["notToScale"] == true,
          "Unaccepted perspective view is stale and not to scale");
    check(broken["state"] == "broken" && broken["reasons"] == QJsonArray{"sceneMissing"} &&
              broken["fingerprint"].isNull() && broken["warnings"] == 0,
          "Broken view names its reason");
    const auto filtered = inspect(doc, "drawing_views.status", {{"sheet", "1"}});
    check(filtered["total"] == 2, "Status can be limited to one sheet");
    rejects([&] { inspect(doc, "drawing_views.status", {{"sheet", "9"}}); }, "unknown sheet");
    // A cursor is bound to the revision.
    auto stale = query(doc, "drawing_views.status");
    stale["limit"] = 1;
    const QJsonValue cursor = inspectDocument(doc, stale)["data"].toObject().value("nextCursor");
    run(doc, {QJsonObject{{"command", "drawing_sheet.rename"}, {"sheet", "1"}, {"name", "B"}}});
    check(rejects([&] { inspectDocument(doc, stale); }, "stale status query")
                  .rfind("STALE_REVISION", 0) == 0,
          "Status queries are revision-bound");
    auto moved = query(doc, "drawing_views.status");
    moved["limit"] = 1;
    moved["cursor"] = cursor;
    check(rejects([&] { inspectDocument(doc, moved); }, "old cursor").rfind("INVALID_CURSOR", 0) == 0,
          "Cursors do not survive a revision change");
    // Describe: projection for a measurable view, none when not to scale.
    const auto described = inspect(doc, "drawing_view.describe", {{"view", "1"}});
    const auto projection = described["projection"].toObject();
    check(described["status"].toObject()["state"] == "current" &&
              projection["scaleDenominator"] == 50 &&
              projection["paperMillimetresPerMetre"] == 20 &&
              described["drawing"].toObject()["lines"] == 4,
          "Describe returns the measured projection");
    const auto origin = projection["origin"].toArray();
    // The target (1, .5) is drawn 10 mm right and 5 mm up of the frame centre.
    check(std::abs(origin[0].toDouble() - .5) < 1e-9 && std::abs(origin[1].toDouble() - .25) < 1e-9,
          "Projection origin honours the paper offset");
    const auto illustration = inspect(doc, "drawing_view.describe", {{"view", "2"}});
    check(illustration["projection"].isNull() && illustration["drawing"].isNull() &&
              illustration["status"].toObject()["notToScale"] == true,
          "A not-to-scale view has no projection");
    check(inspect(doc, "drawing_view.describe", {{"view", "3"}})["projection"].isNull(),
          "A broken view has no projection");
    rejects([&] { inspect(doc, "drawing_view.describe", {{"view", "9"}}); }, "unknown view");
    rejects([&] { inspect(doc, "drawing_sheet.describe", {{"sheet", "9"}}); }, "unknown sheet");
    std::cout << "paged status, listing and descriptions: ok\n";
}

void componentScopeAndStaging() {
    Fixture f;
    auto &doc = f.doc;
    const auto component = createComponent(doc, f.body, "Scoped");
    const auto bytes = encodeContainer(doc);
    for (const auto &nested :
         {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                      {"paper", paper(420, 297)}},
          QJsonObject{{"command", "drawing_view.accept"}, {"all", true}}}) {
        const auto message = rejects([&] {
            run(doc, {componentScopeCommand(doc, component.instance, {nested})});
        }, "sheet operation inside a component scope");
        check(message.find("separate explicit scope") != std::string::npos,
              "Component scope rejects sheet operations: " + message);
    }
    check(encodeContainer(doc) == bytes, "Rejected scoped sheet edit changes nothing");
    StagingSession staging;
    const auto prepared = staging.prepare(
        doc, batch(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                                     {"paper", paper(420, 297)}},
                         createView("Plan", 1, f.plan)}));
    const auto stage = prepared["stageId"].toString();
    const auto changes = staging.changes(doc, stage)["changes"].toArray();
    check(changes.size() == 2 && changes[0].toObject()["kind"] == "drawing_sheet" &&
              changes[1].toObject()["kind"] == "drawing_view" &&
              prepared["createdIds"].toObject()["createdDrawingViews"].toArray() == QJsonArray{"1"},
          "Staged sheet edits name their resources");
    check(encodeContainer(doc) == bytes, "Staging leaves the live document");
    doc.applyPrepared(*staging.proposal(doc, stage));
    check(doc.drawingViews().size() == 1 && lastLabel(doc) == "Command batch (2 operations)",
          "A staged sheet batch publishes one entry");
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
// A script creates a sheet and a view through the CLI; --inspect-native shows them
// in a schema 27 file, and the bounded status query reads them back.
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
    const auto sheets = write(
        dir, "sheets.json",
        QJsonDocument(QJsonArray{QJsonObject{{"command", "drawing_sheet.create"}, {"name", "A3"},
                                             {"paper", paper(420, 297)}},
                                 createView("Plan view", 1, 1),
                                 viewCommand("drawing_view.accept", 1)}));
    const auto base = dir.filePath("base.sketchyup"), sheeted = dir.filePath("sheets.sketchyup");
    auto created = cli({"--script", model, "--output", base});
    check(created.code == 0, "CLI builds the model: " + created.error.toStdString());
    check(cli({"--inspect-native", base}).json["documentVersion"] == 26,
          "A sheet-free file stays schema 26");
    created = cli({"--input", base, "--script", sheets, "--output", sheeted});
    check(created.code == 0, "CLI adds a sheet and a view: " + created.error.toStdString());
    const auto native = cli({"--inspect-native", sheeted}).json;
    check(native["documentVersion"] == 27 && native["drawingSheets"] == 1 &&
              native["drawingViews"] == 1 &&
              native["requiredFeatures"].toArray().contains("drawing-sheets-v1"),
          "--inspect-native shows the sheet and view in schema 27");
    const auto request = write(
        dir, "status.json",
        QJsonDocument(QJsonObject{{"apiVersion", 1},
                                  {"documentId", native["documentId"]},
                                  {"expectedRevision", native["revision"]},
                                  {"query", "drawing_views.status"}}));
    const auto inspected = cli({"--input", sheeted, "--inspect-file", request});
    const auto row = inspected.json["data"].toObject()["items"].toArray()[0].toObject();
    check(inspected.code == 0 && row["name"] == "Plan view" && row["state"] == "current",
          "The saved view reopens accepted: " + inspected.error.toStdString());
    const auto broken = write(dir, "broken.json",
                              QJsonDocument(QJsonArray{QJsonObject{{"command", "saved_scene.delete"},
                                                                   {"scene", "1"}}}));
    const auto deleted = dir.filePath("deleted.sketchyup");
    check(cli({"--input", sheeted, "--script", broken, "--output", deleted}).code == 0,
          "Deleting the scene keeps the view");
    const auto accept = write(dir, "accept.json",
                              QJsonDocument(QJsonArray{viewCommand("drawing_view.accept", 1)}));
    const auto refused = cli({"--input", deleted, "--script", accept});
    check(refused.code == 1 && refused.error.contains("DRAWING_VIEW_NOT_ACCEPTABLE"),
          "The CLI reports the structured refusal: " + refused.error.toStdString());
    std::cout << "CLI script round trip to schema 27: ok\n";
}
#endif
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        everyOperationIsSheetOnly();
        batchesAreAtomic();
        acceptUndoRedo();
        restampRule();
        brokenViewsAreNotAccepted();
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
