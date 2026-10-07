#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
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
void move(Viewport &view, QPointF p) {
    QMouseEvent event(QEvent::MouseMove, p, view.mapToGlobal(p.toPoint()), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
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
        const Vec3 endpoint{-2.037, .143, 0};
        doc.addWire(0, endpoint, {2.013, .143, 0});
        QWidget host;
        host.resize(1000, 750);
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Inference viewport exposed");
        host.activateWindow();
        check(QTest::qWaitForWindowActive(&host), "Inference viewport active");
        view->standardView(1);
        view->setTool(Viewport::Tool::Line);
        waitFor([&] { return view->inferenceReady(); }, "Initial background index ready");
        const auto baseline = encodeDocument(doc);
        for (int zoom = 0; zoom < 3; ++zoom) {
            auto p = view->project(endpoint);
            move(*view, p + QPointF(0, 7));
            check(view->acquiredInference() &&
                      view->acquiredInference()->kind == InferenceKind::Endpoint &&
                      length(view->acquiredInference()->point - endpoint) < tolerance,
                  "Endpoint acquired within seven logical pixels");
            move(*view, p + QPointF(0, 9));
            check(!view->acquiredInference() ||
                      view->acquiredInference()->kind != InferenceKind::Endpoint,
                  "Endpoint outside nine logical pixels");
            QWheelEvent wheel(p, view->mapToGlobal(p.toPoint()), {}, QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(view, &wheel);
            QCoreApplication::processEvents();
        }
        check(encodeDocument(doc) == baseline, "Hover and zoom never mutate document");
        const auto p = view->project(endpoint) + QPointF(0, 4);
        move(*view, p);
        QTest::mouseClick(view, Qt::LeftButton, {}, p.toPoint());
        check(view->operationAnchor() && length(*view->operationAnchor() - endpoint) < tolerance,
              "Click acquires canonical endpoint instead of grid rounding");
        move(*view, view->project({-2.037, 1.343, 0}));
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({-2.037, 1.343, 0}).toPoint());
        check(doc.bodies().size() == 1 && doc.bodies().at(1)->surface.wires.size() == 2,
              "Loose-edge inference adopts its editing context");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Line);
        waitFor([&] { return view->inferenceReady(); }, "Undo index ready");
        const auto midpoint = (endpoint + Vec3{2.013, .143, 0}) * .5;
        move(*view, view->project(midpoint));
        check(view->acquiredInference() &&
                  view->acquiredInference()->kind == InferenceKind::Midpoint,
              "Midpoint marker ranks ahead of on-edge");
        check(view->inference().candidates.size() > 1, "Selectable inference alternatives");
        QTest::keyClick(view, Qt::Key_Tab);
        check(view->hasFocus() && view->acquiredInference()->kind == InferenceKind::OnEdge,
              "Tab cycles inference without moving focus");
        QTest::keyClick(view, Qt::Key_Tab);
        check(view->acquiredInference()->kind == InferenceKind::Midpoint,
              "Tab wraps candidate selection");
        view->update();
        QTest::qWait(50);
        const auto framebuffer = view->grabFramebuffer();
        const auto marker = view->project(midpoint) * view->devicePixelRatioF();
        const auto accent = themeColors(false).accent;
        bool markerVisible = false;
        for (int y = int(marker.y()) - 5; y <= int(marker.y()) + 5; ++y)
            for (int x = int(marker.x()) - 5; x <= int(marker.x()) + 5; ++x) {
                const auto color = framebuffer.pixelColor(x, y);
                markerVisible |= std::abs(color.red() - accent.red()) < 12 &&
                                 std::abs(color.green() - accent.green()) < 12 &&
                                 std::abs(color.blue() - accent.blue()) < 12;
            }
        check(markerVisible, "Inference triangle is rendered, not just present in state");
        if (app.arguments().contains("--capture")) {
            const auto position = app.arguments().indexOf("--capture");
            QTest::qWait(50);
            check(framebuffer.save(app.arguments().value(position + 1)),
                  "Inference marker capture");
        }
        doc = Document();
        doc.addCurve(0, centerCurve(CurveKind::Circle, {}, 2, 0, 2 * std::acos(-1), 24));
        view->refresh();
        view->setTool(Viewport::Tool::Circle);
        waitFor([&] { return view->inferenceReady(); }, "Circle index ready");
        move(*view, view->project({0, 0, 0}));
        check(view->acquiredInference() && view->acquiredInference()->kind == InferenceKind::Center,
              "Analytic center acquired in native viewport");
        view->setDrawingPlane(DrawingPlane::make({0, 0, 1}, {0, 0, 1}, {1, 0, 0}));
        move(*view, view->project({0, 0, 0}));
        check(!view->acquiredInference(), "Explicit plane excludes off-plane center and face");
        // Rapid edit requests coalesce and never expose stale revision results.
        InferenceWorker worker;
        worker.request(doc);
        for (int i = 0; i < 12; ++i) {
            doc.move(1, {.01, 0, 0});
            worker.request(doc);
        }
        waitFor([&] { return bool(worker.ready(doc)); }, "Latest background snapshot ready");
        const auto ready = worker.ready(doc);
        const auto oldBuilds = ready->bodyBuilds();
        doc.move(1, {.01, 0, 0});
        worker.request(doc);
        check(!worker.ready(doc) || worker.ready(doc) != ready,
              "Old index cannot masquerade as current revision");
        waitFor([&] { return bool(worker.ready(doc)); }, "Incremental background snapshot ready");
        check(worker.ready(doc)->bodyBuilds() == oldBuilds + 1,
              "Background edit rebuilds affected body only");
        auto reopened = decodeContainer(encodeContainer(doc));
        worker.request(reopened);
        waitFor([&] { return bool(worker.ready(reopened)); }, "Reopened snapshot ready");
        check(!worker.ready(doc), "Same identity and revision after reopen have distinct sessions");
        Document replacement;
        worker.request(replacement);
        waitFor([&] { return bool(worker.ready(replacement)); },
                "Replacement document snapshot ready");
        check(worker.ready(replacement)->primitiveCount() == 0,
              "Document replacement clears old candidates");
        std::cout << "Native inference acquisition, Tab alternatives, context adoption, plane "
                     "filtering and snapshot preparation passed; devicePixelRatio="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
