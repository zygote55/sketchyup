#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void waitFor(F ready, const char *message) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(ready(), message);
}
void move(Viewport &view, Vec3 point) {
    const auto p = view.project(point);
    QMouseEvent event(QEvent::MouseMove, p, view.mapToGlobal(p.toPoint()), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
bool vertex(const Document &doc, Vec3 point) {
    for (const auto &[id, body] : doc.bodies())
        for (const auto &[vid, p] : body->surface.vertices)
            if (length(doc.worldTransform(id).point(p) - point) < tolerance)
                return true;
    return false;
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    try {
        Document doc;
        QWidget host;
        host.resize(1000, 750);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Constraint viewport exposed");
        host.activateWindow();
        check(QTest::qWaitForWindowActive(&host), "Constraint viewport active");
        view->setFocus();
        view->standardView(1);
        view->setTool(Viewport::Tool::Line);
        waitFor([&] { return view->inferenceReady(); }, "Empty index prepared");
        check(view->measurements("[0,0,0]"), "Typed first point");
        for (int zoom = 0; zoom < 3; ++zoom) {
            move(*view, {2, 0, 0});
            check(view->acquiredDirection() &&
                      view->acquiredDirection()->constraint.kind == DirectionKind::RedAxis,
                  "Red direction at several zooms");
            QTest::keyPress(view, Qt::Key_Shift);
            check(view->lockedDirection() &&
                      view->lockedDirection()->kind == DirectionKind::RedAxis,
                  "Shift captures current world direction");
            move(*view, {2, 1, 0});
            check(view->acquiredDirection() &&
                      std::abs(view->acquiredDirection()->point.y) < tolerance,
                  "Held direction ignores off-axis pointer");
            QTest::keyRelease(view, Qt::Key_Shift);
            check(!view->lockedDirection(), "Shift releases predictably");
            const auto p = view->project({0, 0, 0});
            QWheelEvent wheel(p, view->mapToGlobal(p.toPoint()), {}, QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(view, &wheel);
        }
        QTest::keyClick(view, Qt::Key_Right);
        const auto red = view->lockedDirection();
        check(red && red->kind == DirectionKind::RedAxis, "Arrow locks red axis");
        view->standardView(0);
        move(*view, {2, 1, 1});
        check(view->lockedDirection() == red && view->previewValid(),
              "Camera change preserves lock and geometry");
        const auto orbitStart = view->rect().center();
        QTest::mousePress(view, Qt::MiddleButton, {}, orbitStart);
        QMouseEvent orbit(QEvent::MouseMove, orbitStart + QPoint(25, 10),
                          view->mapToGlobal(orbitStart + QPoint(25, 10)), Qt::NoButton,
                          Qt::MiddleButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view, &orbit);
        QTest::mouseRelease(view, Qt::MiddleButton, {}, orbitStart + QPoint(25, 10));
        check(view->lockedDirection() == red && view->operationAnchor(),
              "Middle orbit preserves in-progress world constraint");
        move(*view, {2, 1, 1});
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier, "", true);
        QCoreApplication::sendEvent(view, &repeat);
        check(view->lockedDirection() == red, "Arrow auto-repeat does not toggle lock");
        const auto before = encodeDocument(doc);
        check(!view->measurements("[2,1,0]") && encodeDocument(doc) == before,
              "Contradictory coordinates reject atomically");
        check(view->measurements("2") && vertex(doc, {2, 0, 0}),
              "Typed length obeys locked direction");
        check(!view->lockedDirection(), "Commit releases lock");
        doc.undo();
        check(doc.bodies().empty(), "Locked edit is one undo item");
        view->refresh();
        view->setTool(Viewport::Tool::Line);
        check(view->measurements("[0,0,0]"), "Vertical line anchor");
        QTest::keyClick(view, Qt::Key_Up);
        check(view->lockedDirection()->kind == DirectionKind::BlueAxis, "Up locks world Z");
        check(view->measurements("3") && vertex(doc, {0, 0, 3}),
              "Numeric Z line leaves initial plane");
        view->cancel();
        doc = Document();
        doc.addWire(0, {-2, 2, 0}, {2, 2, 0});
        view->refresh();
        view->standardView(1);
        view->setTool(Viewport::Tool::Line);
        waitFor([&] { return view->inferenceReady(); }, "Reference index ready");
        check(view->measurements("[0,0,0]"), "Reference operation anchor");
        move(*view, {0, 2, 0});
        waitFor([&] { return bool(view->armedReference()); }, "Hover arms midpoint");
        check(length(view->armedReference()->point - Vec3{0, 2, 0}) < tolerance,
              "Reference retains exact midpoint");
        move(*view, {3, 2, 0});
        check(view->acquiredDirection() &&
                  view->acquiredDirection()->constraint.kind == DirectionKind::FromPoint,
              "Armed reference supplies from-point alignment");
        QTest::keyPress(view, Qt::Key_Shift);
        check(view->lockedDirection() && view->lockedDirection()->kind == DirectionKind::FromPoint,
              "Shift locks offset reference line");
        const auto captureAt = app.arguments().indexOf("--capture");
        if (captureAt >= 0) {
            view->update();
            QTest::qWait(50);
            check(view->grabFramebuffer().save(app.arguments().value(captureAt + 1)),
                  "Lock capture");
        }
        const auto unchanged = encodeDocument(doc);
        check(!view->measurements("1") && encodeDocument(doc) == unchanged,
              "Unreachable reference length rejects without mutation");
        QTest::keyRelease(view, Qt::Key_Shift);
        QTest::keyClick(view, Qt::Key_Down);
        check(view->lockedDirection() && view->lockedDirection()->kind == DirectionKind::Parallel,
              "Down locks reference parallel");
        QTest::keyClick(view, Qt::Key_Down);
        check(view->lockedDirection() &&
                  view->lockedDirection()->kind == DirectionKind::Perpendicular,
              "Down cycles perpendicular");
        QTest::keyClick(view, Qt::Key_Down);
        check(!view->lockedDirection(), "Third Down releases");
        move(*view, {3, 0, 0});
        check(view->acquiredDirection() &&
                  view->acquiredDirection()->constraint.kind == DirectionKind::RedAxis,
              "Axis default for ambiguous direction");
        QTest::keyClick(view, Qt::Key_Tab);
        check(view->hasFocus() && view->acquiredDirection() &&
                  view->acquiredDirection()->constraint.kind == DirectionKind::Parallel,
              "Tab exposes coincident parallel alternative");
        QTest::keyPress(view, Qt::Key_Shift);
        QFocusEvent focusOut(QEvent::FocusOut);
        QCoreApplication::sendEvent(view, &focusOut);
        check(!view->lockedDirection() && view->operationAnchor(),
              "Focus transfer releases held Shift but preserves operation");
        QTest::keyRelease(view, Qt::Key_Shift);
        QTest::keyClick(view, Qt::Key_Left);
        QTest::keyClick(view, Qt::Key_Left);
        check(!view->lockedDirection(), "Same arrow toggles persistent lock off");
        QTest::keyClick(view, Qt::Key_Escape);
        check(!view->armedReference() && !view->operationAnchor(),
              "Escape clears reference and anchor");
        doc = Document();
        doc.addCurve(0, centerCurve(CurveKind::Circle, {}, 2, 0, 2 * std::acos(-1), 24));
        view->refresh();
        view->setTool(Viewport::Tool::Line);
        waitFor([&] { return view->inferenceReady(); }, "Tangent reference prepared");
        check(view->measurements("[4,0,0]"), "External tangent anchor");
        move(*view, {2, 0, 0});
        waitFor([&] { return bool(view->armedReference()); }, "Curve endpoint armed");
        move(*view, {1, std::sqrt(3.), 0});
        bool tangentSelected = false;
        for (int i = 0; i < 20; ++i) {
            if (view->acquiredDirection() &&
                view->acquiredDirection()->constraint.kind == DirectionKind::Tangent) {
                tangentSelected = true;
                break;
            }
            QTest::keyClick(view, Qt::Key_Tab);
        }
        check(tangentSelected, "Tab exposes analytic tangent among point/edge alternatives");
        QTest::keyPress(view, Qt::Key_Shift);
        check(view->lockedDirection() && view->lockedDirection()->kind == DirectionKind::Tangent,
              "Shift holds selected tangent");
        QTest::keyRelease(view, Qt::Key_Shift);
        doc.move(1, {0, 0, .5});
        view->refresh();
        check(!view->armedReference() && !view->lockedDirection() && !view->operationAnchor(),
              "External edit invalidates reference and active constraint session");
        doc = Document();
        doc.addWire(0, {0, 0, 3}, {1, 0, 3});
        view->refresh();
        view->standardView(0);
        view->setTool(Viewport::Tool::Line);
        waitFor([&] { return view->inferenceReady(); }, "Elevated endpoint prepared");
        check(view->measurements("[0,0,0]"), "Locked endpoint anchor");
        QTest::keyClick(view, Qt::Key_Up);
        move(*view, {0, 0, 3.01});
        check(view->acquiredDirection() &&
                  length(view->acquiredDirection()->point - Vec3{0, 0, 3}) < tolerance,
              "Z lock still acquires canonical endpoint outside initial plane");
        view->cancel();
        view->standardView(1);
        doc = Document();
        doc.addFace({{{-2, -2, 1}, {2, -2, 1}, {2, 2, 1}, {-2, 2, 1}}});
        view->refresh();
        view->setTool(Viewport::Tool::Rectangle);
        waitFor([&] { return view->inferenceReady(); }, "Face index ready");
        move(*view, {.7, .4, 1});
        check(view->acquiredInference() && view->acquiredInference()->kind == InferenceKind::OnFace,
              "Face available for plane hold");
        QTest::keyPress(view, Qt::Key_Shift);
        check(view->planeHeld(), "Shift holds hovered face plane before first point");
        check(view->measurements("[0,0,1]") && view->operationAnchor()->z == 1,
              "Typed first point uses held elevated face plane");
        QTest::keyRelease(view, Qt::Key_Shift);
        check(!view->planeHeld(), "Plane hold releases");
        QTest::keyClick(view, Qt::Key_Up);
        check(!view->lockedDirection(), "Planar shape rejects incompatible Z lock");
        std::cout
            << "Native world locks, zoom, camera, Shift/arrow release, reference arming, Tab, "
               "plane hold, numeric constraints and undo passed; devicePixelRatio="
            << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
