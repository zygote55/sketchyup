#include "automation/commands.hpp"
#include "automation/drawing_sheet_commands.hpp"
#include "automation/inspection.hpp"
#include "core/annotations.hpp"
#include "core/components.hpp"
#include "core/groups.hpp"
#include "core/drawing_item_records.hpp"
#include "core/scenes.hpp"
#include "core/transform.hpp"
#include "io/document_io.hpp"
#include "io/drawing_items.hpp"
#include "io/drawing_items_io.hpp"
#include "io/drawing_view.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <string>
using namespace sketchy;
// Referenced paper dimensions (ADR 0164 §3, R091.c): lazy resolution by ID, a
// derived broken state, Reattach, and the lineage table of common model edits.
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
QJsonObject inspect(const Document &doc, const char *name, QJsonObject extra = {}) {
    extra["apiVersion"] = 1;
    extra["documentId"] = QString::fromStdString(doc.identity());
    extra["expectedRevision"] = QString::number(doc.revision());
    extra["query"] = name;
    return inspectDocument(doc, extra)["data"].toObject();
}
QString text(Id id) { return QString::number(id); }
QJsonObject frame(double x, double y, double width, double height) {
    return {{"xMm", x}, {"yMm", y}, {"widthMm", width}, {"heightMm", height}};
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
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
// A 2 m x 1 m slab, a 1:50 orthographic plan view on an A3 sheet, and two
// dimensions: item 1 between the corner vertices (0,0,0) and (2,0,0), item 2 between
// points on the bottom and top edges (the edge midpoints).
struct Fixture {
    Document doc;
    Id body{}, plan{}, face{};
    Fixture() {
        body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        face = doc.bodies().at(body)->surface.faces.begin()->first;
        plan = createScene(doc, "Plan", planScene());
        run(doc, {QJsonObject{{"command", "drawing_sheet.create"},
                              {"name", "Plan"},
                              {"paper", QJsonObject{{"widthMm", 420}, {"heightMm", 297}}}},
                  QJsonObject{{"command", "drawing_view.create"},
                              {"name", "Plan"},
                              {"sheet", "1"},
                              {"source", QJsonObject{{"kind", "scene"}, {"scene", text(plan)}}},
                              {"frame", frame(20, 20, 200, 150)},
                              {"scaleDenominator", 50}}});
        run(doc, {QJsonObject{{"command", "drawing_view.accept"}, {"view", "1"}}});
        run(doc, {dimension({vertex(body, vertexAt(doc, body, {0, 0, 0})),
                             vertex(body, vertexAt(doc, body, {2, 0, 0}))}),
                  dimension({edge(body, edgeBetween(doc, body, {0, 0, 0}, {2, 0, 0}), .5),
                             edge(body, edgeBetween(doc, body, {0, 1, 0}, {2, 1, 0}), .5)})});
    }
    static QJsonObject dimension(QJsonArray anchors, QJsonObject extra = {}) {
        extra["view"] = "1";
        extra["anchors"] = anchors;
        return {{"command", "drawing_item.create"},
                {"sheet", "1"},
                {"kind", "dimension"},
                {"data", extra}};
    }
};
DrawingItemEvaluation eval(const Document &doc, Id item) { return evaluateDrawingItem(doc, item); }
std::string describe(const DrawingItemEvaluation &e) {
    std::string result = drawingItemStateName(e.state);
    for (const auto &reason : e.reasons)
        result += " " + reason;
    return result;
}

// value = paper length x scale / 1000; the projection, orientation and offset place
// the dimension line; moving geometry updates the value while the declared scale
// is unchanged; view scale, offset, frame and scene camera changes update the paper
// geometry at once.
void dimensionsFollowTheModel() {
    Fixture f;
    auto &doc = f.doc;
    auto first = eval(doc, 1);
    check(first.state == DrawingItemState::Current && near(*first.paperLengthMm, 40) &&
              near(*first.valueMetres, 2) && first.text == "2 m",
          "A 2 m edge at 1:50 measures 40 mm and reads 2 m: " + describe(first));
    const auto p0 = (*first.measuredMm)[0], p1 = (*first.measuredMm)[1];
    // The plan maps world x to paper x and world y up the paper.
    check(near(p1[0] - p0[0], 40) && near(p1[1], p0[1]), "Plan x runs along paper x");
    const auto second = eval(doc, 2);
    check(near(*second.valueMetres, 1) && near(*second.paperLengthMm, 20),
          "Edge-point anchors measure the 1 m depth");
    // Orientation and offset place the line; a positive offset is above.
    run(doc, {QJsonObject{{"command", "drawing_item.update"}, {"item", "1"}, {"kind", "dimension"},
                          {"data", QJsonObject{{"offsetMm", 8}, {"prefix", "W"}}}}});
    first = eval(doc, 1);
    check(near((*first.lineMm)[0][1], p0[1] - 8) && near((*first.lineMm)[1][0], p1[0]) &&
              near((*first.textMm)[0], (p0[0] + p1[0]) / 2) && first.text == "W 2 m",
          "Offset moves the line and the prefix leads the text");
    run(doc, {QJsonObject{{"command", "drawing_item.update"}, {"item", "2"}, {"kind", "dimension"},
                          {"data", QJsonObject{{"orientation", "horizontal"}}}}});
    check(near(*eval(doc, 2).paperLengthMm, 0) && near(*eval(doc, 2).valueMetres, 0),
          "A horizontal dimension of a vertical pair measures zero");
    run(doc, {QJsonObject{{"command", "drawing_item.update"}, {"item", "2"}, {"kind", "dimension"},
                          {"data", QJsonObject{{"orientation", "vertical"}, {"offsetMm", -5}}}}});
    const auto vertical = eval(doc, 2);
    check(near(*vertical.valueMetres, 1) &&
              near((*vertical.lineMm)[0][0], (*vertical.measuredMm)[0][0] - 5),
          "A vertical dimension measures paper y; a negative offset is left");
    // Moving geometry updates the value; the scale never changes with the model.
    const auto items = doc.drawingItems();
    QJsonArray matrix;
    for (auto value : Transform::translation({1, 0, 0}).m)
        matrix.append(value);
    run(doc, {QJsonObject{
                 {"command", "geometry.transform_selection"},
                 {"matrix", matrix},
                 {"entities", QJsonArray{QJsonObject{
                                  {"body", text(f.body)},
                                  {"kind", "vertex"},
                                  {"entity", text(vertexAt(doc, f.body, {2, 0, 0}))}}}}}});
    check(doc.drawingItems() == items, "A model edit never writes item records");
    first = eval(doc, 1);
    check(first.state == DrawingItemState::Current && near(*first.valueMetres, 3) &&
              near(*first.paperLengthMm, 60) && first.text == "W 3 m" &&
              doc.drawingViews().at(1)->scaleDenominator == 50,
          "Moving a vertex updates the measured value; the scale is unchanged");
    doc.undo();
    check(near(*eval(doc, 1).valueMetres, 2), "Undo of the model edit restores the value");
    doc.redo();
    // View changes: scale halves the paper length but keeps the value.
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"scaleDenominator", 100}}});
    check(near(*eval(doc, 1).paperLengthMm, 30) && near(*eval(doc, 1).valueMetres, 3),
          "A view scale change keeps the value");
    const auto before = (*eval(doc, 1).measuredMm)[0];
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"offsetMm", QJsonArray{10, 4}}}});
    const auto panned = (*eval(doc, 1).measuredMm)[0];
    check(near(panned[0], before[0] + 10) && near(panned[1], before[1] + 4),
          "A view offset moves the dimension on paper");
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"frame", frame(30, 25, 200, 150)}}});
    const auto moved = (*eval(doc, 1).measuredMm)[0];
    check(near(moved[0], panned[0] + 10) && near(moved[1], panned[1] + 5),
          "A frame move moves the dimension on paper");
    // A scene camera edit (pan the plan target 1 m along x) re-projects at once:
    // at 1:100 the drawing moves 10 mm left on paper.
    const auto framed = *eval(doc, 1).measuredMm;
    auto shifted = planScene();
    shifted.camera->target = {2, .5, 0};
    updateScene(doc, f.plan, shifted);
    first = eval(doc, 1);
    check(near(*first.valueMetres, 3) && near((*first.measuredMm)[0][0], framed[0][0] - 10) &&
              near((*first.measuredMm)[1][1], framed[1][1]),
          "A scene camera change re-projects the dimension");
    std::cout << "dimensions follow the model, the view and the scene camera: ok\n";
}

// Deleting the anchor target gives broken with a Reattach candidate; reattaching
// restores it; Undo works throughout and model edits never write item records.
void brokenAndReattach() {
    Fixture f;
    auto &doc = f.doc;
    const auto items = doc.drawingItems();
    const auto bottom = edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0});
    const auto lastMm = std::get<DrawingDimensionItem>(items.at(1)->data).lastMm;
    // Splitting the bottom edge keeps the corner vertices but retires the edge ID.
    doc.splitEdge(f.body, bottom, .25);
    check(doc.drawingItems() == items, "A model edit never writes item records");
    check(eval(doc, 1).state == DrawingItemState::Current,
          "Vertex anchors survive an edge split");
    const auto broken = eval(doc, 2);
    check(broken.state == DrawingItemState::Broken &&
              broken.reasons == std::vector<std::string>{"referenceMissing"} &&
              broken.text == "[Missing reference]" && !broken.valueMetres &&
              broken.anchors[0].state == AnchorState::Missing &&
              broken.anchors[1].state == AnchorState::Resolved,
          "A split edge breaks its edge anchor: " + describe(broken));
    const auto rows = inspect(doc, "drawing_items.query")["items"].toArray();
    check(rows[1].toObject()["state"] == "broken" &&
              rows[1].toObject()["reasons"].toArray() == QJsonArray{"referenceMissing"},
          "drawing_items.query lists the broken item with its reason");
    // Reattach proposes the nearest edge point at the old paper position.
    const auto proposal = inspect(doc, "drawing_item.reattach_candidates", {{"item", "2"}});
    const auto anchors = proposal["anchors"].toArray();
    const auto candidate = anchors[0].toObject()["candidate"].toObject();
    check(proposal["state"] == "broken" && anchors.size() == 2 &&
              anchors[0].toObject()["state"] == "missing" &&
              anchors[1].toObject()["candidate"].isNull() && candidate["kind"] == "edge" &&
              anchors[0].toObject()["distanceMm"].toDouble() < 1e-6,
          "The missing anchor gets the nearest drawn edge point: " +
              QJsonDocument(proposal).toJson().toStdString());
    const auto items2 = doc.drawingItems();
    check(doc.drawingItems() == items2, "Proposals never write records");
    run(doc, {QJsonObject{{"command", "drawing_item.reattach"},
                          {"item", "2"},
                          {"anchors", QJsonArray{candidate,
                                                 edge(f.body,
                                                      edgeBetween(doc, f.body, {0, 1, 0},
                                                                  {2, 1, 0}),
                                                      .5)}}}});
    const auto repaired = eval(doc, 2);
    check(repaired.state == DrawingItemState::Current && near(*repaired.valueMetres, 1),
          "Reattaching restores the dimension");
    doc.undo();
    check(eval(doc, 2).state == DrawingItemState::Broken, "Undo of the reattach is broken again");
    doc.undo();
    check(eval(doc, 2).state == DrawingItemState::Current && doc.drawingItems() == items,
          "Undo of the split restores the original binding");
    doc.redo();
    doc.redo();
    check(eval(doc, 2).state == DrawingItemState::Current, "Redo restores the reattach");
    // Deleting a vertex-anchored target: erase the body.
    const auto model = doc.drawingItems();
    doc.erase(f.body);
    check(doc.drawingItems() == model, "Erasing geometry never writes item records");
    const auto gone = eval(doc, 1);
    check(gone.state == DrawingItemState::Broken &&
              gone.reasons == std::vector<std::string>{"referenceMissing"},
          "Erasing the body breaks the dimension");
    const auto nothing = inspect(doc, "drawing_item.reattach_candidates", {{"item", "1"}});
    check(nothing["anchors"].toArray()[0].toObject()["candidate"].isNull(),
          "With no geometry in the view there is no candidate");
    doc.undo();
    check(eval(doc, 1).state == DrawingItemState::Current, "Undo of the erase restores it");
    // The lastMm cache is the paper position of the last sheet edit that wrote it.
    check(std::get<DrawingDimensionItem>(doc.drawingItems().at(1)->data).lastMm == lastMm,
          "lastMm is written only by sheet edits");
    std::cout << "broken, reattach candidates, reattach and undo: ok\n";
}

// A dimension is refused on a perspective view; an existing one becomes broken
// (notToScale) when its scene turns perspective, and recovers on undo. A missing
// scene breaks it with sceneMissing.
void notToScale() {
    Fixture f;
    auto &doc = f.doc;
    updateScene(doc, f.plan, planScene(false));
    const auto perspective = eval(doc, 1);
    check(perspective.state == DrawingItemState::Broken &&
              perspective.reasons == std::vector<std::string>{"notToScale"} &&
              !perspective.measuredMm,
          "A perspective scene breaks its dimensions: " + describe(perspective));
    const auto refused = rejects([&] {
        run(doc, {Fixture::dimension({vertex(f.body, vertexAt(doc, f.body, {0, 0, 0})),
                                      vertex(f.body, vertexAt(doc, f.body, {0, 1, 0}))})});
    }, "dimension on a perspective view");
    check(refused.rfind("DRAWING_ITEM_NOT_MEASURABLE: ", 0) == 0, "Refusal is named: " + refused);
    check(inspect(doc, "drawing_item.reattach_candidates", {{"item", "1"}})["anchors"]
              .toArray()[0]
              .toObject()["candidate"]
              .isNull(),
          "Resolved anchors need no candidate");
    doc.undo();
    check(eval(doc, 1).state == DrawingItemState::Current, "Undo restores the orthographic view");
    eraseScene(doc, f.plan);
    check(describe(eval(doc, 1)) == "broken sceneMissing", "A deleted scene breaks dimensions");
    const auto unavailable = inspect(doc, "drawing_item.reattach_candidates", {{"item", "1"}});
    check(unavailable["unavailable"].toArray().isEmpty(),
          "Anchors that resolve need no candidate even when the view is broken");
    doc.undo();
    check(eval(doc, 1).state == DrawingItemState::Current, "Undo restores the scene");
    std::cout << "not-to-scale and missing-scene states: ok\n";
}

// The pure-translation rule: moving a view's frame without resizing it keeps its
// fingerprint, so it stays current and keeps its stamp without a re-stamp.
void pureTranslation() {
    Fixture f;
    auto &doc = f.doc;
    const auto stamp = doc.drawingViews().at(1)->accepted;
    const auto fingerprint = evaluateDrawingView(doc, 1).fingerprint;
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"frame", frame(120, 100, 200, 150)}}});
    const auto status = evaluateDrawingView(doc, 1);
    check(status.fingerprint == fingerprint && status.state == DrawingViewState::Current &&
              doc.drawingViews().at(1)->accepted == stamp,
          "A frame translation keeps the fingerprint and the stamp");
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"frame", frame(120, 100, 180, 150)}}});
    check(evaluateDrawingView(doc, 1).fingerprint != fingerprint &&
              doc.drawingViews().at(1)->accepted != stamp &&
              evaluateDrawingView(doc, 1).state == DrawingViewState::Current,
          "A resize still re-stamps a current view");
    std::cout << "pure frame translation keeps the fingerprint: ok\n";
}

// View edits that change the world <-> sheet mapping carry the bound items' lastMm
// caches in the same sheet-only edit, so Reattach searches where the broken anchor
// is now drawn; Undo restores the view and the caches together.
void lastMmFollowsTheView() {
    Fixture f;
    auto &doc = f.doc;
    doc.splitEdge(f.body, edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0}), .25);
    check(eval(doc, 2).state == DrawingItemState::Broken, "The edge dimension is broken");
    auto last = [&](Id item) { return std::get<DrawingDimensionItem>(doc.drawingItems().at(item)->data).lastMm; };
    const auto before = last(2), resolvedBefore = last(1);
    const auto total = doc.history().total;
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"frame", frame(50, 40, 200, 150)}}});
    check(doc.history().total == total + 1, "A frame move with bound items is one entry");
    check(near(last(2)[0][0], before[0][0] + 30) && near(last(2)[0][1], before[0][1] + 20) &&
              near(last(2)[1][0], before[1][0] + 30) && near(last(1)[0][0], resolvedBefore[0][0] + 30),
          "A frame move carries lastMm");
    auto candidateDistance = [&] {
        const auto proposal = inspect(doc, "drawing_item.reattach_candidates", {{"item", "2"}});
        const auto anchor = proposal["anchors"].toArray()[0].toObject();
        check(anchor["candidate"].toObject()["kind"] == "edge",
              "A candidate is found: " + QJsonDocument(proposal).toJson().toStdString());
        return anchor["distanceMm"].toDouble();
    };
    check(candidateDistance() < 1e-6, "The candidate lies at the anchor's new paper position");
    // Pan and rescale: the cache follows the mapping.
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"offsetMm", QJsonArray{-12, 7}}, {"scaleDenominator", 25}}});
    check(candidateDistance() < 1e-6, "Pan and scale changes carry lastMm");
    // Move to another sheet with a new frame.
    run(doc, {QJsonObject{{"command", "drawing_sheet.create"}, {"name", "Second"},
                          {"paper", QJsonObject{{"widthMm", 420}, {"heightMm", 297}}}}});
    const auto moved = last(2);
    run(doc, {QJsonObject{{"command", "drawing_view.move"}, {"view", "1"}, {"sheet", "2"},
                          {"frame", frame(10, 10, 200, 150)}}});
    check(doc.drawingItems().at(2)->sheet == 2 && near(last(2)[0][0], moved[0][0] - 40) &&
              candidateDistance() < 1e-6,
          "A move to another sheet carries lastMm");
    // A rename does not touch the cache.
    const auto items = doc.drawingItems();
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"}, {"name", "Renamed"}}});
    check(doc.drawingItems() == items, "A rename writes no item records");
    // Undo restores the view and the cache together.
    doc.undo();
    doc.undo();
    check(doc.drawingViews().at(1)->sheet == 1 && doc.drawingItems().at(2)->sheet == 1 &&
              last(2) == moved,
          "Undo restores the view and its items together");
    doc.undo();
    doc.undo();
    doc.undo();
    check(last(2) == before && doc.drawingViews().at(1)->frame.xMm == 20,
          "Undo of every view edit restores the original cache");
    // An unavailable mapping (perspective) leaves the cache as it is.
    doc.redo();
    updateScene(doc, f.plan, planScene(false));
    const auto kept = last(2);
    run(doc, {QJsonObject{{"command", "drawing_view.update"}, {"view", "1"},
                          {"frame", frame(60, 60, 200, 150)}}});
    check(last(2) == kept, "A view without a mapping keeps lastMm");
    std::cout << "lastMm follows view mapping changes: ok\n";
}
// The lineage table (ADR 0164 §3): which common model edits keep a paper
// dimension associated. Vertex anchors (item 1) and edge anchors (item 2) are
// recorded separately. Each row starts from a fresh fixture.
void lineageTable() {
    struct Row {
        const char *edit;
        std::function<void(Fixture &)> apply;
        DrawingItemState vertex, edge;
        bool annotation{true}; // Whether a model annotation on the edge points survives.
    };
    const auto current = DrawingItemState::Current, broken = DrawingItemState::Broken;
    QJsonArray matrix;
    for (auto value : Transform::translation({0, .5, 0}).m)
        matrix.append(value);
    const std::vector<Row> rows{
        {"move body", [](Fixture &f) { f.doc.move(f.body, {1, 2, 0}); }, current, current},
        {"rotate body",
         [](Fixture &f) {
             f.doc.transform(f.body, Transform::rotation({0, 0, 1}, std::numbers::pi / 6));
         },
         current, current},
        {"scale body",
         [](Fixture &f) { f.doc.transform(f.body, Transform::scaling({2, 2, 1})); }, current,
         current},
        {"move a vertex",
         [](Fixture &f) {
             QJsonArray move;
             for (auto value : Transform::translation({0, -.25, 0}).m)
                 move.append(value);
             run(f.doc, {QJsonObject{{"command", "geometry.transform_selection"},
                                     {"matrix", move},
                                     {"entities", QJsonArray{QJsonObject{
                                                      {"body", text(f.body)},
                                                      {"kind", "vertex"},
                                                      {"entity", text(vertexAt(
                                                                     f.doc, f.body, {2, 0, 0}))}}}}}});
         },
         current, current},
        {"move the top edge",
         [matrix](Fixture &f) {
             run(f.doc, {QJsonObject{
                            {"command", "geometry.transform_selection"},
                            {"matrix", matrix},
                            {"entities",
                             QJsonArray{QJsonObject{
                                 {"body", text(f.body)},
                                 {"kind", "edge"},
                                 {"entity", text(edgeBetween(f.doc, f.body, {0, 1, 0},
                                                             {2, 1, 0}))}}}}}});
         },
         current, current},
        {"push/pull the face", [](Fixture &f) { f.doc.pushPull(f.body, f.face, .5); }, current,
         current},
        {"split the bottom edge",
         [](Fixture &f) {
             f.doc.splitEdge(f.body, edgeBetween(f.doc, f.body, {0, 0, 0}, {2, 0, 0}), .25);
         },
         current, broken, true},
        {"erase the bottom edge",
         [](Fixture &f) {
             f.doc.eraseEdge(f.body, edgeBetween(f.doc, f.body, {0, 0, 0}, {2, 0, 0}));
         },
         current, broken, false},
        {"erase the body", [](Fixture &f) { f.doc.erase(f.body); }, broken, broken, false},
        {"group the body", [](Fixture &f) { createGroup(f.doc, {f.body}); }, current, current},
        {"make a component", [](Fixture &f) { createComponent(f.doc, f.body, "Slab"); }, broken,
         broken, false},
    };
    std::cout << "lineage table (vertex anchors | edge anchors):\n";
    for (const auto &row : rows) {
        Fixture f;
        // A model annotation (ADR 0080) on the same edge points, for comparison: model
        // edits remap annotation records inside the edit through topology lineage.
        AnnotationRecord annotation;
        annotation.name = "Model depth";
        annotation.anchors = {
            edgeAnchor(f.doc, f.body, edgeBetween(f.doc, f.body, {0, 0, 0}, {2, 0, 0}), .5),
            edgeAnchor(f.doc, f.body, edgeBetween(f.doc, f.body, {0, 1, 0}, {2, 1, 0}), .5)};
        const auto model = createAnnotation(f.doc, annotation);
        const auto items = f.doc.drawingItems();
        row.apply(f);
        const auto annotationState =
            measureAnnotation(f.doc, *f.doc.annotations().at(model)).state;
        check(f.doc.drawingItems() == items,
              std::string(row.edit) + " never writes item records");
        const auto vertexState = eval(f.doc, 1), edgeState = eval(f.doc, 2);
        std::cout << "  " << std::left << std::setw(24) << row.edit << describe(vertexState)
                  << " | " << std::setw(26) << describe(edgeState) << " | model annotation "
                  << (annotationState == AnchorState::Resolved ? "kept" : "broken") << '\n';
        check(vertexState.state == row.vertex && edgeState.state == row.edge,
              std::string("Lineage row: ") + row.edit);
        if (vertexState.state == DrawingItemState::Broken)
            check(vertexState.reasons == std::vector<std::string>{"referenceMissing"},
                  std::string(row.edit) + " names the missing reference");
        check((annotationState == AnchorState::Resolved) == row.annotation,
              std::string("Model annotation row: ") + row.edit);
        f.doc.undo();
        check(eval(f.doc, 1).state == DrawingItemState::Current &&
                  eval(f.doc, 2).state == DrawingItemState::Current,
              std::string(row.edit) + " is undone");
    }
    std::cout << "lineage table: ok\n";
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        dimensionsFollowTheModel();
        brokenAndReattach();
        notToScale();
        pureTranslation();
        lastMmFollowsTheView();
        lineageTable();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
