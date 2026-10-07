#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QNativeGestureEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void click(Viewport &view, Vec3 point) {
    QTest::mouseClick(&view, Qt::LeftButton, {}, view.project(point).toPoint());
    QCoreApplication::processEvents();
}
void wheel(Viewport &view, QPoint delta, Qt::KeyboardModifiers modifiers = {}) {
    const auto at = QPointF(view.rect().center());
    QWheelEvent event(at, view.mapToGlobal(at.toPoint()), delta, {}, Qt::NoButton, modifiers,
                      Qt::ScrollUpdate, false);
    QCoreApplication::sendEvent(&view, &event);
}
void gesture(Viewport &view, Qt::NativeGestureType type, double value, QPointF delta = {}) {
    const auto at = QPointF(view.rect().center());
    QNativeGestureEvent event(type, QPointingDevice::primaryPointingDevice(), 2, at, at,
                              view.mapToGlobal(at.toPoint()), value, delta);
    QCoreApplication::sendEvent(&view, &event);
}
double height(const Document &doc, Id body, Vec3 normal = {0, 0, 1}, Vec3 origin = {}) {
    double high = -INFINITY, low = INFINITY;
    for (auto [id, point] : doc.bodies().at(body)->surface.vertices) {
        const auto distance = dot(doc.worldTransform(body).point(point) - origin, normal);
        high = std::max(high, distance);
        low = std::min(low, distance);
    }
    return high - low;
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    qputenv("XDG_DATA_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    QString status;
    try {
        Window window;
        window.resize(1200, 800);
        auto &doc = window.document();
        auto *view = window.viewport();
        QObject::connect(view, &Viewport::message, [&](const QString &text) { status = text; });
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Navigation window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Navigation window active");
        view->setFocus();
        view->setTrackpadNavigation(false);
        view->setFieldOfView(45);
        view->setDrawingPlane(DrawingPlane{});
        view->setTool(Viewport::Tool::Rectangle);
        check(view->measurements("[0,0,0]") && view->measurements("4m,3m"), "Draw measured base");
        view->standardView(0);
        view->fit();
        view->setTool(Viewport::Tool::Extrude);
        click(*view, {2, 1.5, 0});
        check(view->measurements("2m") && view->measurements("250cm"),
              "Exact push/pull and re-entry");
        check(std::abs(height(doc, 1) - 2.5) < tolerance && view->selectedFace(),
              "Amendment selects the resulting cap and uses world units");
        const auto revision = doc.revision();
        QTest::mouseDClick(view, Qt::LeftButton, {}, view->project({2, 1.5, 2.5}).toPoint());
        check(doc.revision() == revision + 1 && std::abs(height(doc, 1) - 5) < tolerance,
              "Double-click repeats the last distance as a new edit");
        doc.undo();
        view->refresh();
        check(std::abs(height(doc, 1) - 2.5) < tolerance, "Repeat is exactly one undo step");
        view->setTool(Viewport::Tool::Extrude);
        click(*view, {2, 1.5, 2.5});
        QTest::keyClick(view, Qt::Key_Control);
        check(view->pushPullNewFace(), "Standalone Ctrl toggles create-new-face mode");
        const auto beforeRetained = doc.bodies().at(1);
        const auto oldCap = view->selectedFace();
        check(view->measurements("50cm") && view->measurements("75cm"),
              "Retained-face numeric amendment");
        check(doc.bodies().at(1)->surface.faces.size() ==
                      beforeRetained->surface.faces.size() + 5 &&
                  doc.bodies().at(1)->surface.faces.at(oldCap) ==
                      beforeRetained->surface.faces.at(oldCap) &&
                  std::abs(height(doc, 1) - 3.25) < tolerance,
              "Create-new-face preserves old cap and walls while revising the new cap");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        check(view->pushPullNewFace() && std::abs(height(doc, 1) - 2.5) < tolerance,
              "Ctrl+Z undoes retained-face amendment without toggling its mode");
        view->setPushPullNewFace(false);
        // World distance remains exact in a nonuniformly scaled editing context.
        doc = Document();
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}}});
        doc.transform(1, Transform::scaling({1, 1, 2}));
        view->refresh();
        view->standardView(0);
        view->fit();
        view->setTool(Viewport::Tool::Extrude);
        click(*view, {1, 1, 0});
        check(view->measurements("150cm") && std::abs(height(doc, 1) - 1.5) < tolerance,
              "Push/pull measurement is world distance under nonuniform scale");
        const auto saved = encodeDocument(doc);
        view->setTool(Viewport::Tool::Select);
        for (int preset = 0; preset < 8; ++preset) {
            view->standardView(preset);
            view->fit();
            const auto point = view->project({1, 1, .75});
            const auto picked = view->selectionAt(point);
            check(picked && picked->body == 1 && picked->kind == SelectionKind::Face,
                  "Every standard view preserves depth picking");
        }
        view->standardView(0);
        view->fit();
        const auto wideBefore = view->inferenceCamera().clipFromWorld;
        view->setFieldOfView(90);
        check(view->inferenceCamera().clipFromWorld != wideBefore, "FOV changes projection");
        bool rejected = false;
        try {
            view->setFieldOfView(180);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && view->fieldOfView() == 90, "Invalid FOV preserves camera state");
        view->setFieldOfView(45);
        view->setDrawingPlane(DrawingPlane{});
        view->setTool(Viewport::Tool::Line);
        check(view->measurements("[0,0,0]"), "Anchor a drawing tool before camera gestures");
        const auto anchor = view->operationAnchor();
        view->setTrackpadNavigation(true);
        auto camera = view->inferenceCamera().clipFromWorld;
        wheel(*view, {30, 15});
        check(view->inferenceCamera().clipFromWorld != camera, "Trackpad scroll pans");
        camera = view->inferenceCamera().clipFromWorld;
        wheel(*view, {25, -12}, Qt::AltModifier);
        check(view->inferenceCamera().clipFromWorld != camera, "Alt-trackpad scroll orbits");
        camera = view->inferenceCamera().clipFromWorld;
        wheel(*view, {0, 15}, Qt::ControlModifier);
        check(view->inferenceCamera().clipFromWorld != camera, "Ctrl-trackpad scroll zooms");
        camera = view->inferenceCamera().clipFromWorld;
        gesture(*view, Qt::BeginNativeGesture, 0);
        gesture(*view, Qt::ZoomNativeGesture, .2);
        gesture(*view, Qt::PanNativeGesture, 0, {10, 20});
        gesture(*view, Qt::RotateNativeGesture, 12);
        gesture(*view, Qt::EndNativeGesture, 0);
        check(view->inferenceCamera().clipFromWorld != camera &&
                  view->operationAnchor() == anchor &&
                  view->interactionPhase() == ToolSession::Phase::Anchored &&
                  encodeDocument(doc) == saved,
              "Native trackpad gestures preserve anchored tools and saved document");
        QTest::keyClick(view, Qt::Key_Escape);
        view->setTool(Viewport::Tool::Rectangle);
        const auto dragStart = view->project({.25, .25, 0}).toPoint();
        const auto dragEnd = view->project({1.25, 1.25, 0}).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, dragStart);
        QMouseEvent rectangleDrag(QEvent::MouseMove, QPointF(dragEnd), view->mapToGlobal(dragEnd),
                                  Qt::NoButton, Qt::LeftButton, {});
        QCoreApplication::sendEvent(view, &rectangleDrag);
        const auto dragAnchor = view->operationAnchor();
        wheel(*view, {12, 8});
        QTest::mouseRelease(view, Qt::LeftButton, {}, dragEnd);
        check(dragAnchor && view->operationAnchor() == dragAnchor && encodeDocument(doc) == saved,
              "Scroll navigation cannot accidentally commit a pending drawing drag");
        QTest::keyClick(view, Qt::Key_Escape);
        view->setTool(Viewport::Tool::Zoom);
        const auto at = view->rect().center();
        camera = view->inferenceCamera().clipFromWorld;
        QTest::mousePress(view, Qt::LeftButton, {}, at);
        QMouseEvent move(QEvent::MouseMove, QPointF(at + QPoint(0, -35)),
                         view->mapToGlobal(at + QPoint(0, -35)), Qt::NoButton, Qt::LeftButton, {});
        QCoreApplication::sendEvent(view, &move);
        QTest::mouseRelease(view, Qt::LeftButton, {}, at + QPoint(0, -35));
        check(view->inferenceCamera().clipFromWorld != camera && encodeDocument(doc) == saved,
              "Explicit zoom tool works without a middle mouse button");
        // Build a 6 x 4 m room shell, 20 cm thick, on an arbitrary tilted plane.
        doc = Document();
        view->refresh();
        const auto plane = DrawingPlane::make({7, -2, 1}, {0, -1, 1}, {1, 0, 0});
        view->setDrawingPlane(plane);
        view->setTool(Viewport::Tool::Rectangle);
        check(view->measurements("[7,-2,1]") && view->measurements("6m,4m"),
              "Tilted room outer loop");
        view->enterContext(1);
        view->setTool(Viewport::Tool::Rectangle);
        const auto inner = plane.point(.2, .2);
        const auto coordinate = QString("[%1,%2,%3]")
                                    .arg(inner.x, 0, 'g', 17)
                                    .arg(inner.y, 0, 'g', 17)
                                    .arg(inner.z, 0, 'g', 17);
        check(view->measurements(coordinate) && view->measurements("560cm,360cm"),
              "Tilted room inner loop");
        Id innerFace = 0;
        for (auto [id, face] : doc.bodies().at(1)->surface.faces)
            if (std::abs(doc.bodies().at(1)->surface.area(id) - 5.6 * 3.6) < 1e-6)
                innerFace = id;
        check(innerFace != 0, "Room interior formed an independently selectable face");
        view->selectEntities({{1, SelectionKind::Face, innerFace}});
        view->deleteSelection();
        view->leaveContext();
        view->setTool(Viewport::Tool::Extrude);
        view->standardView(0);
        view->fit();
        click(*view, plane.point(.1, 2));
        check(view->measurements("280cm"), "Tilted room walls use exact height");
        check(std::abs(height(doc, 1, plane.normal, plane.origin) - 2.8) < 1e-6,
              "Room wall height follows its arbitrary construction plane");
        doc.bodies().at(1)->surface.validate();
        for (auto edge : doc.bodies().at(1)->surface.edges())
            check(edge.faces.size() == 2, "Room shell has closed radial incidence");
        QTemporaryDir files;
        check(files.isValid(), "Room evidence directory");
        saveDocument(doc, files.filePath("tilted-room.sketchyup"));
        auto reopened = loadDocument(files.filePath("tilted-room.sketchyup"));
        check(encodeDocument(reopened) == encodeDocument(doc),
              "Measured room saves and reopens exactly");
        view->setTool(Viewport::Tool::Select);
        view->selectEntities({});
        view->fit();
        QTest::qWait(40);
        const auto beforeNavigation = encodeDocument(doc);
        const auto beforeHistory = doc.history().total;
        auto *look = window.findChild<QAction *>("tool.27");
        auto *walk = window.findChild<QAction *>("tool.28");
        check(look && walk, "Camera menu exposes look-around and walk tools");
        look->trigger();
        view->setFocus();
        const auto beforeLook = view->renderCamera();
        QTest::keyClick(view, Qt::Key_Right);
        const auto afterLook = view->renderCamera();
        check(length(afterLook.position - beforeLook.position) < 1e-5 &&
                  length(afterLook.target - beforeLook.target) > .01,
              "Look-around rotates about a fixed eye");
        wheel(*view, QPoint(0, 30));
        check(length(view->renderCamera().position - afterLook.position) < 1e-5 &&
                  view->renderCamera().verticalFov != afterLook.verticalFov,
              "Look wheel adjusts lens without moving eye");
        walk->trigger();
        view->setFocus();
        const auto beforeWalk = view->renderCamera();
        QTest::keyPress(view, Qt::Key_W);
        check(QTest::qWaitFor([&] {
                  return length(view->renderCamera().position - beforeWalk.position) > .02;
              }),
              "Held walk key advances camera");
        QTest::keyRelease(view, Qt::Key_W);
        const auto afterWalk = view->renderCamera();
        check(std::abs(afterWalk.position.z - beforeWalk.position.z) < 1e-5,
              "Walking preserves eye height");
        QTest::qWait(70);
        check(length(view->renderCamera().position - afterWalk.position) < 1e-8,
              "Key release stops walking");
        QTest::keyPress(view, Qt::Key_E);
        check(QTest::qWaitFor(
                  [&] { return view->renderCamera().position.z > afterWalk.position.z + .02; }),
              "Explicit raise key changes height");
        view->clearFocus();
        const auto unfocused = view->renderCamera();
        QTest::qWait(70);
        check(length(view->renderCamera().position - unfocused.position) < 1e-8,
              "Focus loss stops held movement");
        view->setFocus();
        QKeyEvent saveOverride(QEvent::ShortcutOverride, Qt::Key_S, Qt::ControlModifier);
        saveOverride.setAccepted(false);
        QCoreApplication::sendEvent(view, &saveOverride);
        check(!saveOverride.isAccepted(), "Walk preserves the application Save shortcut");
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->tool() == Viewport::Tool::Select, "Escape exits walk mode");
        check(encodeDocument(doc) == beforeNavigation && doc.history().total == beforeHistory,
              "Look and walk preserve model and undo history");
        const auto capture = app.arguments().indexOf("--capture");
        if (capture >= 0)
            check(view->grabFramebuffer().save(app.arguments().value(capture + 1)),
                  "Navigation capture saved");
        check(!view->renderStats().glError, "Navigation and push/pull leave no OpenGL errors");
        std::cout << "Camera presets, FOV, trackpad/native gestures, anchored tools, world "
                     "push/pull, repeat, retained faces and tilted measured room passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << " · status: " << status.toStdString() << '\n';
        return 1;
    }
}
