#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include "core/tags.hpp"
#include "io/drawing_view.hpp"
#include <QCoreApplication>
#include <cmath>
#include <iostream>
#include <string>
using namespace sketchy;
namespace {
void check(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
void near(double a, double b, const std::string &message, double tolerance = 1e-7) {
    check(std::isfinite(a) && std::abs(a - b) < tolerance,
          message + " (" + std::to_string(a) + " != " + std::to_string(b) + ")");
}
template <class F> void rejects(F f, const std::string &what) {
    try {
        f();
    } catch (const ValidationOracleMismatch &) {
        throw;
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("Expected rejection: " + what);
}
// Recorded from the dev build; the sanitize build must produce the same digest.
const std::string goldenPerspectiveFingerprint =
    "01ccf7a9736d87ba2e16b8319f07b10a400f053e256b76a2641a33a1a99b6978";
const std::string goldenFingerprint =
    "2979d0391bf8426b5ee74adbd67c053ac4a06fc22ef6e8384dda2b3dc4b5e487";
SceneCamera planCamera(Vec3 target = {1, .5, 0}, bool orthographic = true) {
    SceneCamera camera;
    camera.target = target;
    camera.yaw = 0;
    camera.pitch = 90;
    camera.distance = 20;
    camera.orthographic = orthographic;
    return camera;
}
SceneSnapshot planScene(Vec3 target = {1, .5, 0}, bool orthographic = true) {
    SceneSnapshot snapshot;
    snapshot.camera = planCamera(target, orthographic);
    return snapshot;
}
DrawingViewRecord viewRecord(Id sheet, Id scene, std::string name = "Plan") {
    DrawingViewRecord record;
    record.name = std::move(name);
    record.sheet = sheet;
    record.source = {DrawingViewSourceKind::Scene, scene};
    record.frame = {20, 20, 200, 150};
    record.offsetMm = {10, -5};
    record.scaleDenominator = 50;
    return record;
}
void accept(Document &doc, Id view) {
    const auto status = evaluateDrawingView(doc, view);
    check(status.fingerprint.size() == 64, "Accept needs a captured fingerprint");
    acceptDrawingViews(doc, {{view, DrawingViewAcceptance{doc.revision(), status.fingerprint}}});
}
DrawingViewState state(const Document &doc, Id view) {
    return evaluateDrawingView(doc, view).state;
}
std::string name(DrawingViewState s) { return drawingViewStateName(s); }
void expect(const Document &doc, Id view, DrawingViewState expected, const std::string &what,
            std::vector<std::string> reasons = {}) {
    const auto status = evaluateDrawingView(doc, view);
    check(status.state == expected, what + ": expected " + name(expected) + ", got " +
                                        name(status.state) + " " + status.detail);
    if (!reasons.empty())
        check(status.reasons == reasons, what + ": reasons");
}
// A 2 m x 1 m rectangle in the XY plane plus one plan scene, sheet and view.
struct Fixture {
    Document doc;
    Id face{}, scene{}, sheet{}, view{};
    Fixture() {
        face = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
        scene = createScene(doc, "Plan", planScene());
        sheet = createDrawingSheet(doc, "A3", {420, 297});
        view = createDrawingView(doc, viewRecord(sheet, scene));
    }
};
double totalLength(const MeasuredDrawing &drawing) {
    double total{};
    for (const auto &line : drawing.geometry.lines)
        total += std::hypot(line.b.x - line.a.x, line.b.y - line.a.y);
    return total;
}

void projectionAndScale() {
    Fixture f;
    const auto evaluation = evaluateDrawingViewDrawing(f.doc, f.view);
    check(evaluation.projection && evaluation.drawing, "Orthographic view is captured");
    const auto &projection = *evaluation.projection;
    // 40 mm on paper for 2 m at 1:50, wherever the frame and pan put it.
    const auto a = projection.worldToSheet({0, 0, 0}), b = projection.worldToSheet({2, 0, 0});
    near(b.x - a.x, 40, "2 m at 1:50 is 40 mm");
    near(b.y - a.y, 0, "Plan X is paper X");
    const auto top = projection.worldToSheet({0, 1, 0});
    near(a.y - top.y, 20, "1 m at 1:50 is 20 mm, world +Y is paper up");
    // The scene target is drawn at the frame centre plus the paper-space offset.
    const auto target = projection.worldToSheet({1, .5, 0});
    near(target.x, 20 + 100 + 10, "Target x at frame centre plus offset");
    near(target.y, 20 + 75 - 5, "Target y at frame centre plus offset");
    const auto back = projection.sheetToWorld({57.5, 101.25, 0});
    const auto again = projection.worldToSheet(back);
    near(again.x, 57.5, "Sheet to world round trip x");
    near(again.y, 101.25, "Sheet to world round trip y");
    near(back.z, 0, "Depth zero stays on the frame plane through the target");
    // The measured drawing uses the same frame-local page.
    const auto &drawing = *evaluation.drawing;
    near(drawing.page.scaleDenominator, 50, "Declared scale");
    near(drawing.page.widthMm, 200, "Frame width");
    near(drawing.page.marginMm, 0, "No margin inside the frame");
    near(length(drawing.page.origin - projection.page.origin), 0, "Same origin");
    check(drawing.geometry.lines.size() == 4, "Rectangle draws four lines");
    near(totalLength(drawing), 120, "Perimeter 6 m at 1:50");
    for (const auto &line : drawing.geometry.lines)
        if (std::abs(line.a.y - line.b.y) < 1e-9 && std::abs(line.a.y - (a.y - 20)) < 1e-6)
            near(std::abs(line.b.x - line.a.x), 40, "Top edge 40 mm");
    // A perspective camera has no measured mapping.
    auto perspective = renderSceneCamera(planCamera({1, .5, 0}, false));
    rejects([&] { drawingViewProjection(*f.doc.drawingViews().at(f.view), perspective); },
            "perspective mapping");
    // Changing the camera zoom (distance) never changes the declared scale.
    auto zoomed = planScene();
    zoomed.camera->distance = 3;
    updateScene(f.doc, f.scene, zoomed);
    const auto zoomedEvaluation = evaluateDrawingViewDrawing(f.doc, f.view);
    near(totalLength(*zoomedEvaluation.drawing), 120, "Scale independent of camera zoom");
    std::cout << "projection and 40 mm at 1:50 with offset: ok\n";
}

void staleAndCurrent() {
    Fixture f;
    expect(f.doc, f.view, DrawingViewState::Stale, "Never accepted", {"notAccepted"});
    accept(f.doc, f.view);
    expect(f.doc, f.view, DrawingViewState::Current, "Accepted");
    const auto accepted = *f.doc.drawingViews().at(f.view);
    // Moving geometry inside the crop: stale, declared scale unchanged.
    f.doc.move(f.face, {.25, 0, 0});
    expect(f.doc, f.view, DrawingViewState::Stale, "Moved geometry", {"changedSinceAccepted"});
    check(*f.doc.drawingViews().at(f.view) == accepted, "Model edits never touch the view record");
    const auto moved = evaluateDrawingViewDrawing(f.doc, f.view);
    near(moved.drawing->page.scaleDenominator, 50, "Scale unchanged after a model edit");
    near(totalLength(*moved.drawing), 120, "Lengths unchanged after a move");
    // Undo back to equal content: a new revision, but current.
    const auto before = f.doc.revision();
    f.doc.undo();
    check(f.doc.revision() != before, "Undo raises the revision");
    expect(f.doc, f.view, DrawingViewState::Current, "Undo to equal content");
    // A revision-only change: an unrelated record.
    const auto tagRevision = f.doc.revision();
    createTag(f.doc, "Notes");
    check(f.doc.revision() != tagRevision, "Tag creation raises the revision");
    expect(f.doc, f.view, DrawingViewState::Current, "Revision-only change");
    renameScene(f.doc, f.scene, "Ground plan");
    expect(f.doc, f.view, DrawingViewState::Current, "Scene rename");
    // An edit outside the crop: the frame covers 10 m x 7.5 m around the target.
    const auto far = f.doc.addFace({{{40, 40, 0}, {41, 40, 0}, {41, 41, 0}, {40, 41, 0}}});
    expect(f.doc, f.view, DrawingViewState::Current, "Edit outside the crop");
    f.doc.move(far, {1, 0, 0});
    expect(f.doc, f.view, DrawingViewState::Current, "Move outside the crop");
    // A scene update changes what the view draws.
    updateScene(f.doc, f.scene, planScene({1.5, .5, 0}));
    expect(f.doc, f.view, DrawingViewState::Stale, "Scene update", {"changedSinceAccepted"});
    f.doc.undo();
    expect(f.doc, f.view, DrawingViewState::Current, "Scene update undone");
    // Deleting the scene breaks the view; undo relinks it.
    eraseScene(f.doc, f.scene);
    check(f.doc.drawingViews().contains(f.view), "Scene deletion keeps the view");
    const auto broken = evaluateDrawingViewDrawing(f.doc, f.view);
    check(broken.status.state == DrawingViewState::Broken &&
              broken.status.reasons == std::vector<std::string>{"sceneMissing"} &&
              broken.status.fingerprint.empty() && !broken.drawing && !broken.projection,
          "Deleted scene is broken (sceneMissing)");
    f.doc.undo();
    expect(f.doc, f.view, DrawingViewState::Current, "Scene deletion undone");
    // Changing the view's own hidden-line mode changes the drawing.
    auto record = *f.doc.drawingViews().at(f.view);
    record.hiddenLines = true;
    const auto plain = evaluateDrawingView(f.doc, f.view).fingerprint;
    updateDrawingView(f.doc, f.view, record);
    check(evaluateDrawingView(f.doc, f.view).fingerprint != plain,
          "Hidden-line mode is part of the fingerprint");
    std::cout << "stale, current, outside crop, scene update and deletion: ok\n";
}

void brokenAndNotToScale() {
    Fixture f;
    const auto perspectiveScene = createScene(f.doc, "Eye level", planScene({1, .5, 0}, false));
    const auto perspectiveView =
        createDrawingView(f.doc, viewRecord(f.sheet, perspectiveScene, "Perspective"));
    const auto perspective = evaluateDrawingViewDrawing(f.doc, perspectiveView);
    check(perspective.status.notToScale && perspective.status.state == DrawingViewState::Stale &&
              perspective.status.reasons == std::vector<std::string>{"notAccepted"} &&
              perspective.status.fingerprint.size() == 64 && !perspective.projection &&
              !perspective.drawing,
          "Perspective scene is not to scale, not broken, with no measured mapping");

    auto showHidden = planScene();
    showHidden.visibility = SceneVisibility{};
    showHidden.visibility->showHidden = true;
    const auto showHiddenView = createDrawingView(
        f.doc, viewRecord(f.sheet, createScene(f.doc, "Show hidden", showHidden), "Hidden"));
    expect(f.doc, showHiddenView, DrawingViewState::Broken, "Show hidden", {"showHidden"});

    auto clipped = planScene();
    clipped.section = SceneSection{};
    clipped.section->plane = std::array<double, 4>{0, 0, 1, -.5};
    const auto clippedView = createDrawingView(
        f.doc, viewRecord(f.sheet, createScene(f.doc, "Clipped", clipped), "Clipped"));
    expect(f.doc, clippedView, DrawingViewState::Broken, "Temporary plane", {"temporaryClipPlane"});

    auto both = clipped;
    both.visibility = showHidden.visibility;
    both.camera.reset();
    const auto bothView = createDrawingView(
        f.doc, viewRecord(f.sheet, createScene(f.doc, "Everything", both), "Everything"));
    expect(f.doc, bothView, DrawingViewState::Broken, "All reasons, in order",
           {"sceneHasNoCamera", "temporaryClipPlane", "showHidden"});

    // captureDrawingView reports instead of throwing; captureSavedScene still throws.
    rejects(
        [&] {
            RenderSnapshot::captureSavedScene(f.doc,
                                              f.doc.drawingViews().at(clippedView)->source.scene);
        },
        "captureSavedScene keeps rejecting temporary planes");
    const auto capture = RenderSnapshot::captureDrawingView(f.doc, 999999);
    check(!capture.snapshot && capture.broken == std::vector<std::string>{"sceneMissing"},
          "Missing scene is reported");

    // Missing references inside a scene are warnings, not broken.
    const auto extra = f.doc.addFace({{{3, 0, 0}, {4, 0, 0}, {4, 1, 0}, {3, 1, 0}}});
    auto hiding = planScene();
    hiding.visibility = SceneVisibility{};
    hiding.visibility->bodyVisible = {{f.face, true}, {extra, true}};
    hiding.visibility->hiddenEntities.insert({extra, SceneEntityKind::Body, 0});
    const auto hidingScene = createScene(f.doc, "Hides extra", hiding);
    const auto hidingView = createDrawingView(f.doc, viewRecord(f.sheet, hidingScene, "Hiding"));
    accept(f.doc, hidingView);
    f.doc.erase(extra);
    const auto warned = evaluateDrawingView(f.doc, hidingView);
    // The scene names the erased body twice: its visibility flag and its hidden entity.
    check(warned.state == DrawingViewState::Current && warned.warnings == 2,
          "Missing scene references are warnings; the drawing is unchanged: " + name(warned.state) +
              " " + std::to_string(warned.warnings));
    rejects([&] { RenderSnapshot::captureSavedScene(f.doc, hidingScene); },
            "captureSavedScene keeps rejecting missing references");
    std::cout << "broken reasons, notToScale and warnings: ok\n";
}

void perspectiveViews() {
    Fixture f;
    const auto scene = createScene(f.doc, "Eye level", planScene({1, .5, 0}, false));
    const auto view = createDrawingView(f.doc, viewRecord(f.sheet, scene, "Perspective"));
    const auto initial = evaluateDrawingView(f.doc, view);
    std::cout << "golden perspective fingerprint: " << initial.fingerprint << '\n';
    check(initial.fingerprint == goldenPerspectiveFingerprint,
          "Golden perspective fingerprint changed: " + initial.fingerprint);
    check(initial.notToScale && initial.state == DrawingViewState::Stale &&
              initial.reasons == std::vector<std::string>{"notAccepted"},
          "Unaccepted perspective view is stale and not to scale");
    accept(f.doc, view);
    auto status = evaluateDrawingView(f.doc, view);
    check(status.notToScale && status.state == DrawingViewState::Current && status.reasons.empty(),
          "Accepting a perspective view makes it current");
    // Geometry moved in view: changed since accepted.
    f.doc.move(f.face, {.25, 0, 0});
    status = evaluateDrawingView(f.doc, view);
    check(status.notToScale && status.state == DrawingViewState::Stale &&
              status.reasons == std::vector<std::string>{"changedSinceAccepted"},
          "Moved geometry makes a perspective view stale");
    f.doc.undo();
    status = evaluateDrawingView(f.doc, view);
    check(status.state == DrawingViewState::Current && status.fingerprint == initial.fingerprint,
          "Undo returns the perspective view to current");
    // Accepting the changed content makes it current again.
    f.doc.move(f.face, {.25, 0, 0});
    accept(f.doc, view);
    check(evaluateDrawingView(f.doc, view).state == DrawingViewState::Current,
          "Accepting changed content makes the perspective view current");
    // Colour, camera and hidden state are inputs; the declared scale is not used.
    const auto accepted = evaluateDrawingView(f.doc, view).fingerprint;
    f.doc.paint(f.face, {.9F, .1F, .1F});
    check(evaluateDrawingView(f.doc, view).fingerprint != accepted, "Colour is an input");
    f.doc.undo();
    auto moved = planScene({1, .5, 0}, false);
    moved.camera->yaw = 30;
    moved.camera->pitch = 30;
    updateScene(f.doc, scene, moved);
    check(evaluateDrawingView(f.doc, view).state == DrawingViewState::Stale, "Camera is an input");
    f.doc.undo();
    auto hiding = planScene({1, .5, 0}, false);
    hiding.visibility = SceneVisibility{};
    hiding.visibility->bodyVisible = {{f.face, false}};
    updateScene(f.doc, scene, hiding);
    check(evaluateDrawingView(f.doc, view).state == DrawingViewState::Stale,
          "Hidden state is an input");
    f.doc.undo();
    check(evaluateDrawingView(f.doc, view).state == DrawingViewState::Current, "Back to accepted");
    // Measured mapping and dimensions stay refused.
    const auto evaluation = evaluateDrawingViewDrawing(f.doc, view);
    check(!evaluation.projection && !evaluation.drawing, "No measured mapping");
    rejects(
        [&] {
            drawingViewProjection(*f.doc.drawingViews().at(view),
                                  renderSceneCamera(*f.doc.scenes().at(scene)->snapshot.camera));
        },
        "perspective projection");
    // Order and ID independence.
    Document other;
    createTag(other, "Shift the allocators");
    other.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}}, "Slab");
    const auto otherView = createDrawingView(
        other, viewRecord(createDrawingSheet(other, "A3", {420, 297}),
                          createScene(other, "Eye level", planScene({1, .5, 0}, false))));
    check(evaluateDrawingView(other, otherView).fingerprint == initial.fingerprint,
          "Perspective fingerprint independent of IDs");
    std::cout << "perspective views: ok\n";
}

void hiddenEdges() {
    Fixture f;
    const auto edge = f.doc.bodies().at(f.face)->topology.edges.begin()->first;
    auto hiding = planScene();
    hiding.visibility = SceneVisibility{};
    hiding.visibility->bodyVisible = {{f.face, true}};
    hiding.visibility->hiddenEntities.insert({f.face, SceneEntityKind::Edge, edge});
    const auto scene = createScene(f.doc, "Edge hidden", hiding);
    const auto capture = RenderSnapshot::captureDrawingView(f.doc, scene);
    check(capture.snapshot && !capture.snapshot->edgeVisible(f.face, edge) &&
              capture.snapshot->visible(f.face),
          "captureDrawingView honours hidden edges");
    check(RenderSnapshot::captureSavedScene(f.doc, scene).edgeVisible(f.face, edge),
          "captureSavedScene behaviour is unchanged");
    const auto view = createDrawingView(f.doc, viewRecord(f.sheet, scene, "Edge hidden"));
    const auto evaluation = evaluateDrawingViewDrawing(f.doc, view);
    check(evaluation.drawing->sources.size() == 3, "Hidden edge is not drawn");
    check(evaluation.status.fingerprint != evaluateDrawingView(f.doc, f.view).fingerprint,
          "Hidden edge changes the fingerprint");
    std::cout << "hidden edges: ok\n";
}

void workLimits() {
    // Frame-covering squares: each triangle lands in all 1024 occlusion cells, so 489+
    // squares exceed the one-million-entry R078 grid budget.
    Document doc;
    for (int i = 0; i < 500; ++i) {
        const double z = i * .001;
        doc.addFace({{{-20, -20, z}, {20, -20, z}, {20, 20, z}, {-20, 20, z}}});
    }
    const auto scene = createScene(doc, "Plan", planScene());
    const auto sheet = createDrawingSheet(doc, "A3", {420, 297});
    const auto view = createDrawingView(doc, viewRecord(sheet, scene));
    const auto record = *doc.drawingViews().at(view);
    const auto status = evaluateDrawingView(doc, view);
    check(status.state == DrawingViewState::Failed &&
              status.reasons == std::vector<std::string>{"captureFailed"} &&
              status.detail.find("one million") != std::string::npos && status.fingerprint.empty(),
          "Work-limit overrun is failed: " + status.detail);
    check(*doc.drawingViews().at(view) == record, "Failure leaves the record unchanged");
    std::cout << "work limits: ok\n";
}

void fingerprints() {
    // Same content, different creation order and therefore different IDs.
    Document first, second;
    const std::vector<std::vector<Vec3>> a{{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}};
    const std::vector<std::vector<Vec3>> b{{{3, 0, 0}, {4, 0, 0}, {4, 2, 0}, {3, 2, 0}}};
    first.addFace(a);
    first.addFace(b);
    createTag(second, "Shift the allocators");
    second.addFace(b);
    second.addFace(a);
    auto view = [](Document &doc) {
        const auto scene = createScene(doc, "Plan", planScene());
        return createDrawingView(doc, viewRecord(createDrawingSheet(doc, "A3", {420, 297}), scene));
    };
    const auto one = evaluateDrawingView(first, view(first));
    const auto two = evaluateDrawingView(second, view(second));
    check(one.fingerprint == two.fingerprint, "Fingerprint independent of IDs and order");
    // Evaluating a read snapshot gives the same answer at the same revision.
    const auto snapshot = first.readSnapshot();
    check(evaluateDrawingView(snapshot, one.view) == one, "Read snapshot evaluates identically");
    // Golden value for the fixture; must match in normal and sanitizer builds.
    Fixture f;
    const auto golden = evaluateDrawingView(f.doc, f.view).fingerprint;
    std::cout << "golden fingerprint: " << golden << '\n';
    check(golden == goldenFingerprint, "Golden fingerprint changed: " + golden);
    // Sub-quantum motion does not change the fingerprint; a visible one does.
    f.doc.move(f.face, {1e-9, 0, 0});
    check(evaluateDrawingView(f.doc, f.view).fingerprint == golden, "Quantized to 0.001 mm");
    f.doc.undo();
    // Raster appearance views also hash appearance inputs.
    auto raster = viewRecord(f.sheet, f.scene, "Rendered");
    raster.content = DrawingViewContent::RasterAppearance;
    const auto rasterView = createDrawingView(f.doc, raster);
    const auto rasterBefore = evaluateDrawingView(f.doc, rasterView).fingerprint;
    check(rasterBefore != golden, "Appearance digest is part of raster fingerprints");
    f.doc.paint(f.face, {.9F, .1F, .1F});
    check(evaluateDrawingView(f.doc, rasterView).fingerprint != rasterBefore,
          "Colour change makes a raster view change");
    check(evaluateDrawingView(f.doc, f.view).fingerprint == golden,
          "Colour change does not affect technical lines");
    std::cout << "fingerprints: ok\n";
}

void statusCache() {
    Fixture f;
    accept(f.doc, f.view);
    DrawingViewStatusCache cache;
    const auto first = cache.status(f.doc, f.view);
    check(first.state == DrawingViewState::Current && cache.evaluations() == 1, "First evaluation");
    check(cache.status(f.doc, f.view) == first && cache.evaluations() == 1,
          "Unchanged revision costs nothing");
    check(cache.find(f.doc.identity(), f.view, f.doc.revision()) != nullptr, "Cache hit");
    createTag(f.doc, "Notes");
    check(!cache.find(f.doc.identity(), f.view, f.doc.revision()), "New revision misses");
    const auto renamed = cache.status(f.doc, f.view);
    check(renamed.state == DrawingViewState::Current && cache.evaluations() == 2 &&
              cache.size() == 1,
          "Revision-only change re-evaluates to current, one entry per view");
    // A late result from an older revision never replaces a newer one.
    cache.store(f.doc.identity(), first);
    check(cache.find(f.doc.identity(), f.view, f.doc.revision()) != nullptr,
          "Older status does not replace newer");
    f.doc.move(f.face, {.5, 0, 0});
    check(cache.status(f.doc, f.view).state == DrawingViewState::Stale, "Cache sees the edit");
    eraseDrawingView(f.doc, f.view);
    cache.prune(f.doc);
    check(cache.size() == 0, "Prune drops deleted views");
    rejects([&] { evaluateDrawingView(f.doc, f.view); }, "evaluating a deleted view");
    std::cout << "status cache: ok\n";
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        Document::setFullValidationOracle(true);
        projectionAndScale();
        staleAndCurrent();
        brokenAndNotToScale();
        hiddenEdges();
        perspectiveViews();
        fingerprints();
        statusCache();
        Document::setFullValidationOracle(false);
        workLimits();
    } catch (const std::exception &error) {
        std::cerr << "drawing_view_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
