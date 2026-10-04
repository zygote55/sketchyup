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
void move(Viewport &view, QPointF point, Qt::MouseButtons buttons = Qt::NoButton) {
    QMouseEvent event(QEvent::MouseMove, point, view.mapToGlobal(point.toPoint()), Qt::NoButton,
                      buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
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
        QWidget host;
        host.resize(1000, 750);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Drawing viewport exposed");
        host.activateWindow();
        check(QTest::qWaitForWindowActive(&host), "Drawing viewport active");
        view->standardView(1);
        view->setTool(Viewport::Tool::Line);
        auto click = [&](Vec3 point) {
            QTest::mouseClick(view, Qt::LeftButton, {}, view->project(point).toPoint());
        };
        click({0, 0, 0});
        move(*view, view->project({2, 0, 0}));
        click({2, 0, 0});
        const auto first = doc.bodies().begin()->first;
        for (auto point : {Vec3{2, 2, 0}, Vec3{0, 2, 0}, Vec3{0, 0, 0}}) {
            move(*view, view->project(point));
            click(point);
        }
        check(doc.bodies().size() == 1 && doc.bodies().at(first)->surface.faces.size() == 1,
              "Chained line closes a face in one context");
        check(std::abs(doc.bodies().at(first)->surface.area(
                           doc.bodies().at(first)->surface.faces.begin()->first) -
                       4) < 1e-8,
              "Chained line area");
        const auto chained = encodeDocument(doc);
        QTest::keyClick(view, Qt::Key_Escape);
        check(view->tool() == Viewport::Tool::Line && encodeDocument(doc) == chained,
              "Escape disconnects chain without removing committed lines");
        click({4, 0, 0});
        move(*view, view->project({5, 0, 0}));
        click({5, 0, 0});
        check(doc.bodies().size() == 2, "Disconnected line starts separate context");
        doc = Document();
        view->refresh();
        view->setTool(Viewport::Tool::Freehand);
        view->standardView(1);
        const auto start = view->project({0, 0, 0}).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        for (auto point : {Vec3{2, 0, 0}, Vec3{2, 2, 0}, Vec3{0, 2, 0}, Vec3{0, 0, 0}})
            move(*view, view->project(point), Qt::LeftButton);
        QTest::mouseRelease(view, Qt::LeftButton, {}, start);
        check(doc.bodies().size() == 1 && doc.bodies().begin()->second->surface.faces.size() == 1,
              "Closed freehand stroke forms face");
        const auto stroke = encodeDocument(doc);
        const auto history = doc.historyBytes();
        QTest::mousePress(view, Qt::LeftButton, {}, view->project({3, 0, 0}).toPoint());
        move(*view, view->project({4, 1, 0}), Qt::LeftButton);
        QTest::keyClick(view, Qt::Key_Escape);
        QTest::mouseRelease(view, Qt::LeftButton, {}, view->project({4, 1, 0}).toPoint());
        check(encodeDocument(doc) == stroke && doc.historyBytes() == history,
              "Canceled sampled stroke leaves history unchanged");
        doc = Document();
        view->refresh();
        view->setTool(Viewport::Tool::RotatedRectangle);
        view->standardView(1);
        click({0, 0, 0});
        move(*view, view->project({2, 2, 0}));
        click({2, 2, 0});
        move(*view, view->project({1, 3, 0}));
        click({1, 3, 0});
        check(doc.bodies().size() == 1, "Three-point rotated rectangle commits");
        auto body = doc.bodies().begin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 4) < 1e-6,
              "Rotated rectangle baseline/height area");
        doc = Document();
        view->refresh();
        const auto tilted = DrawingPlane::make({0, 0, 1}, {0, 1, 1}, {1, 0, 0});
        view->setDrawingPlane(tilted);
        view->setTool(Viewport::Tool::Rectangle);
        // The default perspective is nearly edge-on to this plane. Use the
        // front view so integer pointer pixels resolve both construction axes.
        view->standardView(2);
        check(view->measurements("[0,0,1]") && view->measurements("2m,3m"),
              "Keyboard rectangle on explicit tilted plane");
        body = doc.bodies().begin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 6) < 1e-6,
              "Typed tilted rectangle dimensions");
        for (const auto &[id, point] : body->surface.vertices)
            check(std::abs(tilted.coordinates(point).z) < tolerance,
                  "Typed coordinates stay on explicit plane");
        auto typed = body->surface;
        doc = Document();
        view->refresh();
        view->setDrawingPlane(tilted);
        view->setTool(Viewport::Tool::Rectangle);
        click(tilted.point(0, 0));
        move(*view, view->project(tilted.point(2, 3)));
        click(tilted.point(2, 3));
        check(doc.bodies().size() == 1, "Pointer rectangle on tilted plane");
        body = doc.bodies().begin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 6) < 1e-6,
              "Pointer and typed tilted dimensions agree");
        for (const auto &[id, point] : typed.vertices) {
            bool matched = false;
            for (const auto &[other, candidate] : body->surface.vertices)
                matched |= length(candidate - point) < tolerance;
            check(matched, "Pointer and typed tilted construction have the same vertices");
        }
        view->setTool(Viewport::Tool::Polygon);
        view->setDrawingPlane(tilted);
        check(view->measurements("8s") && view->measurements("[0,0,1]") && view->measurements("1m"),
              "Polygon center/radius/sides keyboard construction");
        body = doc.bodies().rbegin()->second;
        check(body->surface.vertices.size() == 8, "Polygon side count reaches shared command");
        check(view->measurements("6s") &&
                  doc.bodies().rbegin()->second->surface.vertices.size() == 6,
              "Polygon side re-entry amends operation");
        doc = Document();
        view->refresh();
        view->setDrawingPlane(std::nullopt);
        const auto block = doc.addFace({{{0, 0, 0}, {4, 0, 0}, {4, 4, 0}, {0, 4, 0}}});
        doc.pushPull(block, doc.bodies().at(block)->surface.faces.begin()->first, 2);
        view->refresh();
        view->fit();
        view->standardView(1);
        view->setTool(Viewport::Tool::Rectangle);
        click({1, 1, 2});
        check(view->operationAnchor() && std::abs(view->operationAnchor()->z - 2) < tolerance,
              "First hovered face sets drawing plane");
        move(*view, view->project({3, 3, 2}));
        click({3, 3, 2});
        check(doc.bodies().size() == 1 && doc.bodies().at(block)->surface.faces.size() == 7,
              "Rectangle on hovered face subdivides its context");
        std::cout << "Chained/disconnected lines, freehand, rotated/tilted rectangles, polygons "
                     "and hovered planes passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
