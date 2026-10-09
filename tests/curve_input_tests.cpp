#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
#include <numbers>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void move(Viewport &view, Vec3 point, Qt::MouseButtons buttons = Qt::NoButton) {
    const auto pos = view.project(point);
    QMouseEvent event(QEvent::MouseMove, pos, view.mapToGlobal(pos.toPoint()), Qt::NoButton,
                      buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
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
        check(QTest::qWaitForWindowExposed(&host), "Curve viewport exposed");
        host.activateWindow();
        check(QTest::qWaitForWindowActive(&host), "Curve viewport active");
        auto reset = [&](Viewport::Tool tool) {
            doc = Document();
            view->refresh();
            view->setDrawingPlane(DrawingPlane{});
            view->setTool(tool);
            view->standardView(1);
        };
        auto click = [&](Vec3 point) {
            QTest::mouseClick(view, Qt::LeftButton, {}, view->project(point).toPoint());
        };
        auto curve = [&]() {
            check(doc.bodies().size() == 1, "One curve context");
            const auto &curves = doc.bodies().begin()->second->curves;
            check(curves.size() == 1, "One analytic curve record");
            return curves.begin()->second;
        };
        reset(Viewport::Tool::Circle);
        check(view->measurements("24s"), "Circle configurable segments");
        click({0, 0, 0});
        move(*view, {2, 0, 0});
        check(view->previewValid() && doc.bodies().empty(), "Circle preview is private");
        click({2, 0, 0});
        check(curve().kind == CurveKind::Circle && curve().segments == 24 &&
                  (view->project(curve().point(0)) - view->project({2, 0, 0})).manhattanLength() <
                      1.5,
              "Pointer circle parameters within integer pointer resolution");
        check(view->measurements("12s") && curve().segments == 12, "Circle segment amendment");
        doc.undo();
        check(doc.bodies().empty(), "Circle amended one undo");
        reset(Viewport::Tool::CenterArc);
        check(view->measurements("24s"), "Arc segments");
        click({0, 0, 0});
        click({2, 0, 0});
        move(*view, {0, 2, 0});
        check(view->previewValid() && doc.bodies().empty(), "Center arc preview is private");
        click({0, 2, 0});
        check(std::abs(curve().sweepAngle - std::numbers::pi / 2) < tolerance,
              "Pointer center arc angle");
        check(view->measurements("3m,-90deg") && std::abs(curve().radius - 3) < tolerance &&
                  curve().sweepAngle < 0,
              "Signed radius/angle amendment");
        check(view->measurements("16s") && curve().segments == 16 && curve().sweepAngle < 0,
              "Segment amendment preserves negative sweep");
        const auto before = encodeDocument(doc);
        check(!view->measurements("0m,90deg") && !view->measurements("3m,360deg") &&
                  !view->measurements("0s"),
              "Invalid radius/angle/count rejected");
        check(encodeDocument(doc) == before, "Invalid curve input atomic");
        doc.undo();
        check(doc.bodies().empty(), "Center arc re-entry is one undo");
        reset(Viewport::Tool::TwoPointArc);
        click({-2, 0, 0});
        click({2, 0, 0});
        move(*view, {0, 1, 0});
        click({0, 1, 0});
        auto arc = curve();
        check((view->project(arc.point(arc.sweepAngle / 2)) - view->project({0, 1, 0}))
                      .manhattanLength() < 1.5,
              "Pointer bulge constraint within integer pointer resolution");
        check(view->measurements("-2m"), "Signed bulge amendment");
        arc = curve();
        const auto midpoint = (arc.point(0) + arc.point(arc.sweepAngle)) * .5;
        check(length(arc.point(arc.sweepAngle / 2) - midpoint - Vec3{0, -2, 0}) < tolerance,
              "Typed negative bulge is exact relative to pointer-defined endpoints");
        reset(Viewport::Tool::ThreePointArc);
        check(view->measurements("[2,0,0]") && view->measurements("[0,2,0]"),
              "Typed start/through phase");
        check(!view->measurements("[-2,4,0]") && doc.bodies().empty(),
              "Collinear third point rejects");
        check(view->measurements("[-2,0,0]"), "Typed third point commits");
        check(std::abs(curve().radius - 2) < tolerance &&
                  std::abs(curve().sweepAngle - std::numbers::pi) < tolerance,
              "Three point radius/sweep");
        reset(Viewport::Tool::Pie);
        const auto start = view->project({0, 0, 0}).toPoint();
        QTest::mousePress(view, Qt::LeftButton, {}, start);
        move(*view, {2, 0, 0}, Qt::LeftButton);
        QTest::mouseRelease(view, Qt::LeftButton, {}, view->project({2, 0, 0}).toPoint());
        check(doc.bodies().empty() && view->operationAnchor().has_value(),
              "First drag sets pie radius only");
        move(*view, {0, 2, 0});
        click({0, 2, 0});
        check(curve().kind == CurveKind::Pie &&
                  doc.bodies().begin()->second->surface.faces.size() == 1,
              "Pie closes sector face");
        reset(Viewport::Tool::CenterArc);
        check(view->measurements("[0,0,0]") && view->measurements("2m"),
              "Typed center and radius phase");
        check(doc.bodies().empty() && view->measurements("1.5707963267948966rad"),
              "Typed radian angle commits");
        const auto typed = curve();
        // Starting a new operation must not reuse the previous radius point.
        click({4, 0, 0});
        check(view->measurements("1m") && doc.bodies().size() == 1,
              "New arc starts a fresh radius phase");
        QTest::keyClick(view, Qt::Key_Escape);
        check(doc.bodies().size() == 1 && !view->operationAnchor(),
              "Cancel pending arc keeps prior commit");
        reset(Viewport::Tool::CenterArc);
        const auto tilted = DrawingPlane::make({0, 0, 1}, {0, 1, 1}, {1, 0, 0});
        view->setDrawingPlane(tilted);
        view->standardView(2);
        click(tilted.origin);
        click(tilted.point(2, 0));
        move(*view, tilted.point(0, 2));
        click(tilted.point(0, 2));
        auto pointerCurve = curve();
        check((view->project(tilted.point(pointerCurve.radius, 0)) -
               view->project(tilted.point(typed.radius, 0)))
                          .manhattanLength() < 1.5 &&
                  std::abs(pointerCurve.sweepAngle - typed.sweepAngle) < tolerance,
              "Tilted pointer radius agrees within pixel resolution; axis angle is exact");
        for (const auto &[id, p] : doc.bodies().begin()->second->surface.vertices)
            check(std::abs(tilted.coordinates(p).z) < tolerance,
                  "Curve vertices stay on locked plane");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved,
              "Native curve persistent roundtrip");
        reset(Viewport::Tool::Pie);
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        check(view->measurements("[0;0;0]") && view->measurements("2,5m;90deg"),
              "Locale radius/angle separator");
        check(std::abs(curve().radius - 2.5) < tolerance, "Comma decimal radius");
        QLocale::setDefault(QLocale::c());
        const auto revision = doc.revision();
        doc.move(doc.bodies().begin()->first, {1, 0, 0});
        view->refresh();
        check(!view->measurements("12s") && doc.revision() == revision + 1,
              "Intervening edit prevents segment amendment");
        std::cout << "Native circle/arc/pie pointer phases, numeric constraints, segmentation, "
                     "amendment, cancel and persistence passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
