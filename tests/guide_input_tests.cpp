#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
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
void click(Viewport &view, Vec3 point) {
    const auto p = view.project(point);
    for (auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
        QMouseEvent event(type, p, view.mapToGlobal(p.toPoint()), Qt::LeftButton,
                          type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &event);
    }
    QCoreApplication::processEvents();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    try {
        Document doc;
        doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 0, 3}, {0, 0, 3}}});
        const auto original = doc.bodies().at(1);
        QWidget host;
        host.resize(1000, 750);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Guide viewport exposed");
        host.activateWindow();
        check(QTest::qWaitForWindowActive(&host), "Guide viewport active");
        view->setFocus();
        view->fit();
        view->standardView(2);
        view->setTool(Viewport::Tool::Tape);
        waitFor([&] { return view->inferenceReady(); }, "Wall inference ready");
        move(*view, {2, 0, 0});
        check(view->acquiredInference() &&
                  view->acquiredInference()->kind == InferenceKind::Midpoint,
              "Tape acquires wall baseline midpoint");
        click(*view, {2, 0, 0});
        const auto beforePreview = encodeDocument(doc);
        move(*view, {2, 0, .9});
        check(view->previewValid() && encodeDocument(doc) == beforePreview,
              "Offset preview is valid and private");
        QTest::keyClick(view, Qt::Key_Up);
        check(bool(view->lockedDirection()), "Tape accepts world Z lock in wall plane");
        check(view->measurements("900 mm"), "Typed 900 mm sill guide");
        check(doc.bodies().at(1)->guides.size() == 1, "One construction guide created");
        auto sill = doc.bodies().at(1)->guides.begin()->second;
        check(sill.kind == GuideKind::Line && std::abs(sill.origin.z - .9) < tolerance &&
                  std::abs(sill.direction.x) > .999,
              "Sill is exactly 0.9 m above baseline");
        check(doc.bodies().at(1)->surface.vertices == original->surface.vertices &&
                  doc.bodies().at(1)->surface.faces == original->surface.faces &&
                  doc.bodies().at(1)->topology.edges == original->topology.edges,
              "Guides do not create or partition model faces");
        check(view->measurements("120 cm") && doc.bodies().at(1)->guides.size() == 1 &&
                  std::abs(doc.bodies().at(1)->guides.begin()->second.origin.z - 1.2) < tolerance,
              "Numeric guide amendment replaces one guide");
        check(view->measurements("0.9m"), "Restore sill height by amendment");
        doc.undo();
        check(doc.bodies().at(1)->guides.empty() && doc.bodies().at(1)->surface.faces.size() == 1,
              "All numeric revisions undo as one operation");
        doc.redo();
        view->refresh();
        view->setTool(Viewport::Tool::Rectangle);
        waitFor([&] { return view->inferenceReady(); }, "Guide index ready");
        move(*view, {.7, 0, .9});
        check(view->acquiredInference() &&
                  view->acquiredInference()->kind == InferenceKind::OnGuide &&
                  std::abs(view->acquiredInference()->point.z - .9) < tolerance,
              "Sill guide can be reused by a drawing tool");
        view->setGuidesVisible(false);
        move(*view, {.7, 0, .9});
        check(!view->acquiredInference() ||
                  view->acquiredInference()->entityType != InferenceEntity::Guide,
              "Hidden guides are excluded from snapping");
        view->setGuidesVisible(true);
        click(*view, {.7, 0, .9});
        check(view->operationAnchor() && std::abs(view->operationAnchor()->z - .9) < tolerance &&
                  std::abs(view->drawingPlane().normal.y) > .999,
              "Starting on a wall guide preserves the wall plane");
        check(view->measurements("400mm,300mm"), "Draw a measured rectangle from sill guide");
        check(doc.bodies().at(1)->surface.faces.size() == 2 &&
                  doc.bodies().at(1)->guides.size() == 1,
              "Only drawing the rectangle forms an additional face");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Protractor);
        view->setDrawingPlane(DrawingPlane::make({}, {0, -1, 0}, {1, 0, 0}), 1);
        check(view->measurements("[0,0,0]") && view->measurements("[1,0,0]") &&
                  view->measurements("45deg"),
              "Protractor creates a typed 45-degree guide");
        const auto angled = doc.bodies().at(1)->guides.rbegin()->second;
        check(length(angled.direction - Vec3{std::sqrt(.5), 0, std::sqrt(.5)}) < tolerance,
              "Protractor uses the wall's signed angle");
        view->setTool(Viewport::Tool::Tape);
        QTest::keyPress(view, Qt::Key_Control);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyRelease(view, Qt::Key_Control);
        check(view->guideCreation(), "Ctrl shortcuts do not toggle the measurement mode");
        QTest::keyClick(view, Qt::Key_Control);
        check(!view->guideCreation(), "Ctrl switches to measurement only");
        const auto unchanged = encodeDocument(doc);
        const auto history = doc.historyBytes();
        const auto dirty = doc.dirty();
        QString measured;
        QObject::connect(view, &Viewport::message, [&](const QString &text) { measured = text; });
        check(view->measurements("[0,0,0]") && view->measurements("[3,0,0]"),
              "Measurement-only distance accepts typed points");
        check(measured.contains("3 m"), "Distance is reported to the user");
        view->setTool(Viewport::Tool::Protractor);
        check(view->measurements("[0,0,0]") && view->measurements("[1,0,0]") &&
                  view->measurements("-90 deg"),
              "Measurement-only angle accepts signed degrees");
        check(measured.contains("-90 deg") && encodeDocument(doc) == unchanged &&
                  doc.historyBytes() == history && doc.dirty() == dirty,
              "Measurement-only tools leave bytes, history and dirty state unchanged");
        view->setGuideCreation(true);
        view->setTool(Viewport::Tool::Tape);
        check(view->measurements("[0,0,0]"), "Point guide anchor");
        QTest::keyClick(view, Qt::Key_Up);
        check(view->measurements("18in"), "Point guide follows numeric locked distance");
        const auto point = doc.bodies().at(1)->guides.rbegin()->second;
        check(point.kind == GuideKind::Point &&
                  length(point.origin - Vec3{0, 0, .4572}) < tolerance,
              "Inch conversion produces exact guide point");
        view->cancel();
        doc = decodeContainer(encodeContainer(doc));
        view->refresh();
        view->setTool(Viewport::Tool::Tape);
        waitFor([&] { return view->inferenceReady(); }, "Reopened guide index ready");
        move(*view, {.7, 0, .9});
        check(view->acquiredInference() &&
                  view->acquiredInference()->entityType == InferenceEntity::Guide,
              "Saved and reopened guide remains acquirable");
        view->setTool(Viewport::Tool::Select);
        QTest::qWait(30);
        const auto visible = view->grabFramebuffer();
        const auto capture = app.arguments().indexOf("--capture");
        if (capture >= 0)
            check(visible.save(app.arguments().value(capture + 1)), "Guide capture saved");
        view->setGuidesVisible(false);
        QTest::qWait(30);
        const auto hidden = view->grabFramebuffer();
        check(visible != hidden, "Guides are drawn in the framebuffer");
        view->clearGuides();
        check(doc.bodies().at(1)->guides.empty() && doc.bodies().at(1)->surface.faces.size() == 1,
              "Explicit cleanup preserves modeled geometry");
        doc.undo();
        check(doc.bodies().at(1)->guides.size() == 3, "Cleanup is one undoable operation");
        check(!view->renderStats().glError, "Guide overlay leaves no GL errors");
        doc = Document();
        view->refresh();
        view->setGuidesVisible(true);
        view->setDrawingPlane(DrawingPlane{});
        view->fit();
        view->standardView(1);
        view->setTool(Viewport::Tool::Tape);
        waitFor([&] { return view->inferenceReady(); }, "Pointer guide index ready");
        click(*view, {0, 0, 0});
        move(*view, {1.5, 0, 0});
        check(view->previewValid(), "Pointer tape previews a guide point");
        click(*view, {1.5, 0, 0});
        check(doc.bodies().size() == 1 && doc.bodies().at(1)->guides.size() == 1,
              "Pointer tape commits one guide point");
        const auto center = doc.bodies().at(1)->guides.begin()->second.origin;
        check((view->project(center) - view->project({1.5, 0, 0})).manhattanLength() < 1.5,
              "Pointer guide matches the requested logical-pixel position");
        view->setTool(Viewport::Tool::Protractor);
        view->setDrawingPlane(std::nullopt);
        waitFor([&] { return view->inferenceReady(); }, "Pointer protractor index ready");
        click(*view, center);
        click(*view, center + Vec3{1, 0, 0});
        const auto beforeAngle = encodeDocument(doc);
        move(*view, center + Vec3{.5, .5, 0});
        check(view->previewValid() && encodeDocument(doc) == beforeAngle,
              "Pointer protractor uses a private third-point preview");
        click(*view, center + Vec3{.5, .5, 0});
        check(doc.bodies().at(1)->guides.size() == 2 &&
                  doc.bodies().at(1)->surface.vertices.empty() &&
                  doc.bodies().at(1)->surface.faces.empty(),
              "Pointer protractor creates a guide without model topology");
        const auto pointerAngle = doc.bodies().at(1)->guides.rbegin()->second;
        check(pointerAngle.kind == GuideKind::Line &&
                  length(pointerAngle.direction - Vec3{std::sqrt(.5), std::sqrt(.5), 0}) < .02,
              "Three pointer phases preserve the requested protractor angle");
        std::cout << "Tape, sill reuse, protractor, unit conversion, locks, numeric amendment, "
                     "measurement-only, visibility, persistence and cleanup passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
