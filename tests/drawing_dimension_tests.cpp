#include "automation/commands.hpp"
#include "automation/drawing_sheet_commands.hpp"
#include "automation/inspection.hpp"
#include "core/annotations.hpp"
#include "core/components.hpp"
#include "core/drawing_item_lineage.hpp"
#include "core/drawing_item_records.hpp"
#include "core/groups.hpp"
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
#include <set>
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
// An edit that gives the edge a new ID at the same place and names no lineage for
// it. A paper anchor must not follow it: anchors are never re-bound by geometry.
void reidentifyEdge(Document &doc, Id body, Id edge) {
    const auto old = doc.bodies().at(body);
    auto next = std::make_shared<Body>(*old);
    const auto record = next->topology.edges.at(edge);
    next->topology.edges.erase(edge);
    const auto id = next->topology.nextId++;
    next->topology.edges[id] = record;
    Change change{body, old, next};
    for (const auto &[kept, unused] : next->topology.edges)
        if (kept != id)
            change.edgeDescendants[kept] = {kept};
    doc.apply(Edit{"Re-identify edge", {change}}, doc.revision());
}
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
              near(*first.valueMetres, 2) && first.text == "2000",
          "A 2 m edge at 1:50 measures 40 mm and reads 2000 (mm): " + describe(first));
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
              near((*first.textMm)[0], (p0[0] + p1[0]) / 2) && first.text == "W 2000",
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
              near(*first.paperLengthMm, 60) && first.text == "W 3000" &&
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
    // Retiring the bottom edge ID without lineage keeps the corner vertices.
    reidentifyEdge(doc, f.body, bottom);
    check(doc.drawingItems() == items, "A model edit without lineage writes no item records");
    check(eval(doc, 1).state == DrawingItemState::Current, "Vertex anchors survive the edit");
    const auto broken = eval(doc, 2);
    check(broken.state == DrawingItemState::Broken &&
              broken.reasons == std::vector<std::string>{"referenceMissing"} &&
              broken.text == "[Missing reference]" && !broken.valueMetres &&
              broken.anchors[0].state == AnchorState::Missing &&
              broken.anchors[1].state == AnchorState::Resolved,
          "An edge without lineage breaks its edge anchor: " + describe(broken));
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
          "Undo of the edit restores the original binding");
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
// R091.g: the sheet's dimension format, rounded half away from zero.
void dimensionFormats() {
    using U = DrawingDimensionUnit;
    const std::vector<std::tuple<double, DrawingDimensionFormat, std::string>> cases{
        {6, {}, "6000"},
        {6, {U::Millimetres, 0, true}, "6000 mm"},
        {std::hypot(8.2, 3), {}, "8732"},
        {std::hypot(8.2, 3), {U::Centimetres, 1, false}, "873.2"},
        {std::hypot(8.2, 3), {U::Metres, 2, true}, "8.73 m"},
        {.0045, {}, "5"},
        {.0025, {}, "3"},
        {1.2345, {U::Metres, 3, false}, "1.235"},
        {2, {U::Metres, 4, true}, "2.0000 m"},
        {6, {U::FeetInches, 0, false}, "19'-8\""},
        {6, {U::FeetInches, 2, false}, "19'-8 1/4\""},
        {.3048, {U::FeetInches, 0, true}, "1'-0\""},
        {.0254 * (3 + 7. / 16), {U::FeetInches, 1, false}, "0'-3 7/16\""},
        {.0254 * (12 * 2 + 5.5), {U::FeetInches, 1, false}, "2'-5 1/2\""},
    };
    for (const auto &[metres, format, expected] : cases) {
        const auto written = formatDrawingDimension(metres, format);
        check(written == expected, "Dimension format: " + std::to_string(metres) + " m reads " +
                                       written + ", expected " + expected);
    }
    // The page's format applies to every dimension on it.
    Fixture f;
    auto &doc = f.doc;
    run(doc, {QJsonObject{{"command", "drawing_sheet.update"},
                          {"sheet", text(doc.drawingViews().at(1)->sheet)},
                          {"dimensionFormat",
                           QJsonObject{{"unit", "cm"}, {"decimals", 1}, {"showUnit", true}}}}});
    check(eval(doc, 1).text == "200.0 cm" && eval(doc, 2).text == "100.0 cm",
          "Every dimension on the page follows its format");
}
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
    reidentifyEdge(doc, f.body, edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0}));
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
const AnnotationAnchor &dimensionAnchor(const Document &doc, Id item, size_t index) {
    return std::get<DrawingDimensionItem>(doc.drawingItems().at(item)->data).anchors[index];
}
const AnnotationAnchor &labelAnchor(const Document &doc, Id item) {
    return std::get<DrawingLabelItem>(doc.drawingItems().at(item)->data).target.anchor;
}
// A leader label whose tip is a point on an edge, at the edge fraction.
Id addEdgeLabel(Fixture &f, Id edgeId, double fraction) {
    run(f.doc,
        {QJsonObject{{"command", "drawing_item.create"},
                     {"sheet", "1"},
                     {"kind", "label"},
                     {"data", QJsonObject{{"text", "Slab edge"},
                                          {"textAt", QJsonObject{{"xMm", 250}, {"yMm", 60}}},
                                          {"target", QJsonObject{{"kind", "view"},
                                                                 {"view", "1"},
                                                                 {"anchor", edge(f.body, edgeId,
                                                                                 fraction)}}}}}}});
    return f.doc.drawingItems().rbegin()->first;
}
Vec3 anchorWorld(const Document &doc, Id item, size_t index = 0) {
    const auto evaluation = eval(doc, item);
    check(evaluation.anchors.size() > index &&
              evaluation.anchors[index].state == AnchorState::Resolved,
          "The anchor resolves");
    return evaluation.anchors[index].world;
}
bool same(Vec3 a, Vec3 b) { return length(a - b) < 1e-9; }
// Every item equals `before` except the named ones, whose anchors alone changed.
void onlyAnchorsChanged(const DrawingItemRecords &before, const DrawingItemRecords &after,
                        const std::set<Id> &changed, const std::string &what) {
    check(before.size() == after.size(), what + ": no item is added or removed");
    for (const auto &[id, item] : after)
        check(changed.contains(id) ? drawingItemAnchorsOnly(*before.at(id), *item)
                                   : item == before.at(id),
              what + ": item " + std::to_string(id) + " changes only in its anchors");
}

// ADR 0164 §3 addendum: a model edit rewrites paper anchors inside the same edit, from
// the edit's topology lineage, as it rewrites model annotations. An edge split under a
// dimension endpoint moves that endpoint to the new vertex; a leader tip elsewhere on
// the edge follows to the piece that contains it. One entry; Undo and Redo are exact.
void edgeSplitLineage() {
    Fixture f;
    auto &doc = f.doc;
    const auto bottom = edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0});
    const auto label = addEdgeLabel(f, bottom, .25);
    const auto tip = anchorWorld(doc, label);
    const auto endpoint = anchorWorld(doc, 2, 0);
    check(same(endpoint, {1, 0, 0}), "Item 2 starts at the bottom edge's midpoint");
    const auto items = doc.drawingItems();
    const auto entries = doc.history().total;
    const auto lastMm = std::get<DrawingDimensionItem>(items.at(2)->data).lastMm;
    // Split the bottom edge exactly under the dimension's endpoint.
    doc.splitEdge(f.body, bottom, .5);
    check(doc.history().total == entries + 1 && doc.history().entries.back().label == "Split edge",
          "The split and the anchor rewrite are one entry");
    onlyAnchorsChanged(items, doc.drawingItems(), {2, label}, "Split under an endpoint");
    const auto middle = vertexAt(doc, f.body, {1, 0, 0});
    const auto &moved = dimensionAnchor(doc, 2, 0);
    check(moved.kind == AnchorKind::Vertex && moved.body == f.body && moved.entity == middle,
          "The endpoint on the split point becomes the new vertex anchor");
    check(dimensionAnchor(doc, 2, 1) ==
              std::get<DrawingDimensionItem>(items.at(2)->data).anchors[1],
          "The other endpoint is untouched");
    check(std::get<DrawingDimensionItem>(doc.drawingItems().at(2)->data).lastMm == lastMm,
          "lastMm stays the last sheet edit's");
    const auto value = eval(doc, 2);
    check(value.state == DrawingItemState::Current && near(*value.valueMetres, 1),
          "The dimension stays current: " + describe(value));
    const auto &leader = labelAnchor(doc, label);
    check(leader.kind == AnchorKind::Edge && leader.body == f.body && leader.entity != bottom &&
              doc.bodies().at(f.body)->topology.edges.contains(leader.entity) &&
              same(anchorWorld(doc, label), tip) &&
              eval(doc, label).state == DrawingItemState::Current,
          "The leader tip follows to the piece that contains it");
    const auto split = doc.drawingItems();
    doc.undo();
    check(doc.drawingItems() == items, "Undo restores the exact prior anchors");
    doc.redo();
    check(doc.drawingItems() == split, "Redo restores the rewritten anchors");
    // Split the leader's piece again, away from the tip: it follows again.
    const auto piece = leader.entity;
    const auto again = doc.drawingItems();
    doc.splitEdge(f.body, piece, .5);
    onlyAnchorsChanged(again, doc.drawingItems(), {label}, "Second split");
    check(labelAnchor(doc, label).entity != piece && same(anchorWorld(doc, label), tip),
          "A second split follows again");
    std::cout << "edge splits move paper anchors to their successors: ok\n";
}

// A face split (a line drawn across the face) splits the bottom and top edges; edge
// anchors follow to the pieces, as model annotations do. Vertex anchors are untouched.
void faceSplitLineage() {
    Fixture f;
    auto &doc = f.doc;
    AnnotationRecord annotation;
    annotation.name = "Model depth";
    annotation.anchors = {dimensionAnchor(doc, 2, 0), dimensionAnchor(doc, 2, 1)};
    const auto model = createAnnotation(doc, annotation);
    const auto items = doc.drawingItems();
    const auto faces = doc.bodies().at(f.body)->surface.faces.size();
    doc.insertEdges(f.body, {0, 0, 0}, {0, 0, 1}, {{Vec3{.5, 0, 0}, Vec3{.5, 1, 0}}});
    check(doc.bodies().at(f.body)->surface.faces.size() == faces + 1, "The face is split");
    onlyAnchorsChanged(items, doc.drawingItems(), {2}, "Face split");
    const auto depth = eval(doc, 2);
    check(depth.state == DrawingItemState::Current && near(*depth.valueMetres, 1) &&
              same(anchorWorld(doc, 2, 0), {1, 0, 0}) && same(anchorWorld(doc, 2, 1), {1, 1, 0}),
          "Edge anchors follow a face split: " + describe(depth));
    check(eval(doc, 1).state == DrawingItemState::Current, "Vertex anchors are untouched");
    check(measureAnnotation(doc, *doc.annotations().at(model)).state == AnchorState::Resolved,
          "The model annotation on the same points survives too");
    doc.undo();
    check(doc.drawingItems() == items, "Undo restores the exact anchors");
    std::cout << "face splits move edge anchors to their pieces: ok\n";
}

// Make component: the anchors follow the geometry into the placement's member, the
// same {body, entity} identity that model annotations and linked views use; nesting
// keeps them; an edge split inside the definition moves them through its lineage.
void makeComponentLineage() {
    Fixture f;
    auto &doc = f.doc;
    const auto bottom = edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0});
    const auto label = addEdgeLabel(f, bottom, .25);
    const auto tip = anchorWorld(doc, label);
    const auto items = doc.drawingItems();
    const auto entries = doc.history().total;
    const auto component = createComponent(doc, f.body, "Slab");
    const auto member = component.movedGeometry.at(f.body);
    check(doc.history().total == entries + 1, "Make component is one entry");
    onlyAnchorsChanged(items, doc.drawingItems(), {1, 2, label}, "Make component");
    for (Id item : {Id{1}, Id{2}})
        for (size_t i = 0; i < 2; ++i)
            check(dimensionAnchor(doc, item, i).body == member &&
                      dimensionAnchor(doc, item, i).entity ==
                          std::get<DrawingDimensionItem>(items.at(item)->data).anchors[i].entity,
                  "Anchors follow the geometry into the placement, IDs unchanged");
    check(near(*eval(doc, 1).valueMetres, 2) && near(*eval(doc, 2).valueMetres, 1) &&
              labelAnchor(doc, label).body == member && same(anchorWorld(doc, label), tip),
          "Dimensions and the label stay current after make component");
    const auto made = doc.drawingItems();
    // Nest the placement in another component: its members keep their identity.
    const auto bay = createGroup(doc, {component.instance}, "Bay");
    const auto outer = createComponent(doc, bay, "Bay");
    check(doc.instances().contains(outer.instance) &&
              eval(doc, 1).state == DrawingItemState::Current &&
              eval(doc, 2).state == DrawingItemState::Current &&
              eval(doc, label).state == DrawingItemState::Current &&
              same(anchorWorld(doc, label), tip),
          "Anchors in a nested placement stay current");
    check(dimensionAnchor(doc, 2, 0).body == member,
          "The nested placement keeps the member's identity");
    // Split the bottom edge inside the inner definition, under item 2's endpoint: every
    // placement regenerates and the anchors follow the definition edit's lineage.
    const auto nestedItems = doc.drawingItems();
    editComponentDefinition(doc, component.definition, [&](Document &draft) {
        return draft.splitEdge(member, edgeBetween(draft, member, {0, 0, 0}, {2, 0, 0}), .5);
    });
    onlyAnchorsChanged(nestedItems, doc.drawingItems(), {2, label}, "Split inside the definition");
    check(dimensionAnchor(doc, 2, 0).kind == AnchorKind::Vertex &&
              dimensionAnchor(doc, 2, 0).body == member &&
              same(anchorWorld(doc, 2, 0), {1, 0, 0}) && same(anchorWorld(doc, label), tip) &&
              eval(doc, 2).state == DrawingItemState::Current &&
              eval(doc, label).state == DrawingItemState::Current,
          "A split inside a nested definition moves the anchors to their successors");
    // Undo steps back through each edit to the exact prior anchors.
    doc.undo();
    check(doc.drawingItems() == nestedItems, "Undo of the definition edit is exact");
    doc.undo();
    doc.undo();
    check(doc.drawingItems() == made, "Undo of the nesting keeps the anchors");
    doc.undo();
    check(doc.drawingItems() == items, "Undo of make component restores the exact anchors");
    doc.redo();
    check(doc.drawingItems() == made, "Redo of make component is exact");
    // Make component from a selected face, in a group: the face moves to a new context.
    Fixture g;
    const auto room = createGroup(g.doc, {g.body}, "Room");
    const auto before = g.doc.drawingItems();
    run(g.doc, {QJsonObject{{"command", "component.selection"},
                            {"name", "Slab"},
                            {"context", text(room)},
                            {"entities", QJsonArray{QJsonObject{{"body", text(g.body)},
                                                                {"kind", "face"},
                                                                {"entity", text(g.face)}}}}}});
    onlyAnchorsChanged(before, g.doc.drawingItems(), {1, 2}, "Make component from a face");
    const auto moved = dimensionAnchor(g.doc, 1, 0).body;
    check(moved != g.body && g.doc.bodies().at(moved)->parent != room &&
              eval(g.doc, 1).state == DrawingItemState::Current &&
              eval(g.doc, 2).state == DrawingItemState::Current &&
              near(*eval(g.doc, 1).valueMetres, 2),
          "Anchors follow a selected face into the new component in its group");
    g.doc.undo();
    check(g.doc.drawingItems() == before, "Undo restores the exact anchors");
    std::cout << "make component carries paper anchors: ok\n";
}

// When lineage does not name exactly one successor the anchor is kept as it was: the
// item breaks and Reattach repairs it, as before. Here one context's geometry is
// transferred, IDs unchanged, to two contexts at once.
void ambiguousLineageStaysBroken() {
    Fixture f;
    auto &doc = f.doc;
    const auto items = doc.drawingItems();
    const auto old = doc.bodies().at(f.body);
    Edit edit{"Duplicate slab", {{f.body, old, nullptr}}};
    for (Id id : {doc.nextId(), doc.nextId() + 1}) {
        auto copy = std::make_shared<Body>(*old);
        copy->id = id;
        edit.changes.push_back({id, nullptr, copy});
        edit.transfers[f.body].insert(id);
    }
    doc.apply(std::move(edit), doc.revision());
    check(doc.drawingItems() == items, "Ambiguous lineage writes no item records");
    for (Id item : {Id{1}, Id{2}})
        check(describe(eval(doc, item)) == "broken referenceMissing",
              "Ambiguous lineage leaves the item broken");
    const auto proposal = inspect(doc, "drawing_item.reattach_candidates", {{"item", "1"}});
    check(!proposal["anchors"].toArray()[0].toObject()["candidate"].isNull(),
          "Reattach still proposes a repair");
    doc.undo();
    check(doc.drawingItems() == items && eval(doc, 1).state == DrawingItemState::Current,
          "Undo restores the binding");
    // A transfer whose target moved the geometry is not the same point either.
    const auto moved = doc.bodies().at(f.body);
    Edit shifted{"Move slab", {{f.body, moved, nullptr}}};
    auto copy = std::make_shared<Body>(*moved);
    copy->id = doc.nextId();
    copy->transform = Transform::translation({0, 3, 0});
    shifted.changes.push_back({copy->id, nullptr, copy});
    shifted.transfers[f.body].insert(copy->id);
    doc.apply(std::move(shifted), doc.revision());
    check(doc.drawingItems() == items && eval(doc, 1).state == DrawingItemState::Broken,
          "A transfer to another position is not followed");
    std::cout << "ambiguous lineage stays broken with Reattach: ok\n";
}

// The rewritten anchors are ordinary stored anchors: the file format is unchanged and
// save, reopen and save give identical bytes.
void rewrittenAnchorsRoundTrip() {
    Fixture f;
    auto &doc = f.doc;
    doc.splitEdge(f.body, edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0}), .5);
    createComponent(doc, f.body, "Slab");
    const auto bytes = encodeContainer(doc);
    const auto reopened = decodeContainer(bytes);
    check(encodeContainer(reopened) == bytes, "Save, reopen and save give identical bytes");
    check(reopened.drawingItems().size() == doc.drawingItems().size(), "Every item reopens");
    for (const auto &[id, item] : doc.drawingItems())
        check(*reopened.drawingItems().at(id) == *item, "Reopened anchors are identical");
    for (Id item : {Id{1}, Id{2}})
        check(eval(reopened, item).state == DrawingItemState::Current,
              "Reopened dimensions are current");
    std::cout << "rewritten anchors save and reopen byte-identically: ok\n";
}

// Model annotations follow the same geometry transfers as paper anchors (ADR 0164
// addendum): vertex, edge and face anchors, in one history entry, through the composed
// "make component from a selection" edit, with a byte-identical round trip.
void annotationsFollowTransfers() {
    Fixture f;
    auto &doc = f.doc;
    const auto room = createGroup(doc, {f.body}, "Room");
    const auto create = [&](const char *name, AnnotationKind kind,
                            std::vector<AnnotationAnchor> anchors) {
        AnnotationRecord record;
        record.name = name;
        record.kind = kind;
        record.anchors = std::move(anchors);
        if (kind == AnnotationKind::Label)
            record.text = name;
        return createAnnotation(doc, std::move(record));
    };
    const auto bottom = edgeBetween(doc, f.body, {0, 0, 0}, {2, 0, 0});
    create("Run", AnnotationKind::Distance,
           {vertexAnchor(doc, f.body, vertexAt(doc, f.body, {0, 0, 0})),
            edgeAnchor(doc, f.body, bottom, .25)});
    create("Slab", AnnotationKind::Label, {faceAnchor(doc, f.body, f.face, {1.5, .5, 0})});
    std::map<Id, AnnotationRecord> original;
    std::map<Id, std::vector<Vec3>> points;
    for (const auto &[id, record] : doc.annotations()) {
        original.emplace(id, *record);
        for (const auto &anchor : record->anchors)
            points[id].push_back(resolveAnnotationAnchor(doc, anchor).point);
    }
    const auto entries = doc.history().total;
    run(doc, {QJsonObject{{"command", "component.selection"},
                          {"name", "Slab"},
                          {"context", text(room)},
                          {"entities", QJsonArray{QJsonObject{{"body", text(f.body)},
                                                              {"kind", "face"},
                                                              {"entity", text(f.face)}}}}}});
    check(doc.history().total == entries + 1, "Make component from a selection is one entry");
    std::map<Id, AnnotationRecord> made;
    for (const auto &[id, record] : doc.annotations()) {
        made.emplace(id, *record);
        for (size_t i = 0; i < record->anchors.size(); ++i) {
            const auto &anchor = record->anchors[i];
            const auto &was = original.at(id).anchors[i];
            check(anchor.body != f.body && anchor.entity == was.entity && anchor.kind == was.kind &&
                      anchor.state == AnchorState::Resolved,
                  "Vertex, edge and face anchors follow the selection into the component");
            const auto resolved = resolveAnnotationAnchor(doc, anchor);
            check(resolved.state == AnchorState::Resolved && same(resolved.point, points.at(id)[i]),
                  "The annotation stays on the same world point");
        }
    }
    const auto bytes = encodeContainer(doc);
    const auto reopened = decodeContainer(bytes);
    check(encodeContainer(reopened) == bytes, "Save, reopen and save give identical bytes");
    for (const auto &[id, record] : doc.annotations())
        check(*reopened.annotations().at(id) == *record, "Reopened annotations are identical");
    doc.undo();
    for (const auto &[id, record] : doc.annotations())
        check(*record == original.at(id), "Undo restores the exact annotation records");
    doc.redo();
    for (const auto &[id, record] : doc.annotations())
        check(*record == made.at(id), "Redo restores the exact annotation records");
    std::cout << "model annotations follow geometry transfers: ok\n";
}

// The lineage table (ADR 0164 §3 and its addendum): which common model edits keep a
// paper dimension associated. Vertex anchors (item 1) and edge anchors (item 2) are
// recorded separately. Each row starts from a fresh fixture. Rows that move an anchor
// to a successor entity rewrite only the item anchors, inside the model edit.
void lineageTable() {
    struct Row {
        const char *edit;
        std::function<void(Fixture &)> apply;
        DrawingItemState vertex, edge;
        bool annotation{true}; // Whether a model annotation on the edge points survives.
        bool rewrites{};       // Whether the edit rewrites paper anchors.
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
        {"scale body", [](Fixture &f) { f.doc.transform(f.body, Transform::scaling({2, 2, 1})); },
         current, current},
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
         current, current, true, true},
        {"erase the bottom edge",
         [](Fixture &f) {
             f.doc.eraseEdge(f.body, edgeBetween(f.doc, f.body, {0, 0, 0}, {2, 0, 0}));
         },
         current, broken, false},
        {"erase the body", [](Fixture &f) { f.doc.erase(f.body); }, broken, broken, false},
        {"group the body", [](Fixture &f) { createGroup(f.doc, {f.body}); }, current, current},
        {"make a component", [](Fixture &f) { createComponent(f.doc, f.body, "Slab"); }, current,
         current, true, true},
        {"make a component (face)",
         [](Fixture &f) {
             run(f.doc,
                 {QJsonObject{{"command", "component.selection"},
                              {"name", "Slab"},
                              {"context", "0"},
                              {"entities", QJsonArray{QJsonObject{{"body", text(f.body)},
                                                                  {"kind", "face"},
                                                                  {"entity", text(f.face)}}}}}});
         },
         current, current, true, true},
    };
    std::cout << "lineage table (vertex anchors | edge anchors):\n";
    for (const auto &row : rows) {
        Fixture f;
        // A model annotation (ADR 0080) on the same edge points, for comparison: model
        // edits remap annotation records inside the edit through topology lineage and,
        // for geometry that moved to another context, through the edit's transfers.
        AnnotationRecord annotation;
        annotation.name = "Model depth";
        annotation.anchors = {
            edgeAnchor(f.doc, f.body, edgeBetween(f.doc, f.body, {0, 0, 0}, {2, 0, 0}), .5),
            edgeAnchor(f.doc, f.body, edgeBetween(f.doc, f.body, {0, 1, 0}, {2, 1, 0}), .5)};
        const auto model = createAnnotation(f.doc, annotation);
        const auto items = f.doc.drawingItems();
        const auto entries = f.doc.history().total;
        row.apply(f);
        check(f.doc.history().total == entries + 1, std::string(row.edit) + " is one entry");
        const auto annotationState =
            measureAnnotation(f.doc, *f.doc.annotations().at(model)).state;
        if (row.rewrites) {
            check(f.doc.drawingItems() != items, std::string(row.edit) + " rewrites anchors");
            for (const auto &[id, item] : f.doc.drawingItems())
                check(item == items.at(id) || drawingItemAnchorsOnly(*items.at(id), *item),
                      std::string(row.edit) + " rewrites only item anchors");
        } else
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
        const auto rewritten = f.doc.drawingItems();
        f.doc.undo();
        check(eval(f.doc, 1).state == DrawingItemState::Current &&
                  eval(f.doc, 2).state == DrawingItemState::Current &&
                  f.doc.drawingItems() == items,
              std::string(row.edit) + " is undone to the exact anchors");
        f.doc.redo();
        check(f.doc.drawingItems() == rewritten, std::string(row.edit) + " is redone exactly");
    }
    std::cout << "lineage table: ok\n";
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        dimensionsFollowTheModel();
        brokenAndReattach();
        dimensionFormats();
        notToScale();
        pureTranslation();
        lastMmFollowsTheView();
        lineageTable();
        edgeSplitLineage();
        faceSplitLineage();
        makeComponentLineage();
        ambiguousLineageStaysBroken();
        rewrittenAnchorsRoundTrip();
        annotationsFollowTransfers();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
