#include "app/annotation_display.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "core/annotations.hpp"
#include "core/sections.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QImage frame(Viewport &view) {
    view.refresh();
    QCoreApplication::processEvents();
    auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady() && !view.renderStats().glError,
          "Valid annotation framebuffer");
    return image;
}
int changed(const QImage &a, const QImage &b, QRectF logical, const Viewport &view) {
    const double scale = double(a.width()) / view.width();
    const QRect area(qFloor(logical.x() * scale), qFloor(logical.y() * scale),
                     qCeil(logical.width() * scale), qCeil(logical.height() * scale));
    int count{};
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x)
            if (a.rect().contains(x, y) && a.pixel(x, y) != b.pixel(x, y))
                ++count;
    return count;
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Document doc;
        const auto body = doc.addWire(0, {0, 0, 0}, {4, 0, 0});
        const auto edge = doc.bodies().at(body)->topology.edges.begin()->first;
        auto style = doc.style();
        style.gridVisible = style.axesVisible = false;
        style.background = {.9f, .9f, .9f};
        doc.setStyle(style);
        QWidget host;
        QVBoxLayout layout(&host);
        auto *view = new Viewport(doc);
        layout.addWidget(view);
        host.resize(960, 720);
        host.show();
        check(QTest::qWaitForWindowExposed(&host), "Annotation viewport exposed");
        view->standardView(1);
        view->frameBounds({-1, -1, -1}, {5, 3, 1});
        const auto baseline = frame(*view);
        const auto meshes = view->renderStats().bodyMeshBuilds;
        AnnotationRecord span;
        span.name = "Width";
        span.text = "Width";
        span.anchors = {edgeAnchor(doc, body, edge, 0), edgeAnchor(doc, body, edge, 1)};
        span.offset = {0, 1, 0};
        span.color = {.1f, .6f, .1f};
        span.textSize = 18;
        const auto id = createAnnotation(doc, span);
        const auto drawn = frame(*view);
        const auto at = view->project({2, 1, 0});
        const QRectF box(at - QPointF(100, 30), QSizeF(200, 60));
        check(changed(baseline, drawn, box, *view) > 40,
              "Dimension label actually draws in framebuffer");
        check(view->renderStats().bodyMeshBuilds == meshes,
              "Annotation graphics do not rebuild native meshes");
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        const auto millimetres = frame(*view);
        check(changed(drawn, millimetres, box, *view) > 20, "Units change displayed text pixels");
        check(annotationText(*doc.annotations().at(id),
                             measureAnnotation(doc, *doc.annotations().at(id)), doc.displayUnits(),
                             doc.displayPrecision())
                  .contains("4000 mm"),
              "Unit text contract");
        doc.undo();
        doc.undo();
        check(changed(baseline, frame(*view), box, *view) == 0,
              "Undo removes annotation graphics exactly");
        doc.redo();
        frame(*view);
        AnnotationRecord label;
        label.kind = AnnotationKind::Label;
        label.name = "Joint";
        label.text = "Joint\nKeep clear";
        label.anchors = {edgeAnchor(doc, body, edge, .5)};
        label.offset = {0, 2, 0};
        const auto labelId = createAnnotation(doc, label);
        frame(*view);
        doc.splitEdge(body, edge, .5);
        const auto broken = frame(*view);
        int red{};
        for (int y = 0; y < broken.height(); ++y)
            for (int x = 0; x < broken.width(); ++x) {
                const auto c = broken.pixelColor(x, y);
                if (c.red() > 120 && c.green() < 90 && c.blue() < 90)
                    ++red;
            }
        check(red > 40, "Broken label draws red marker and explicit state");
        check(annotationText(*doc.annotations().at(labelId),
                             measureAnnotation(doc, *doc.annotations().at(labelId)),
                             doc.displayUnits(), doc.displayPrecision())
                  .contains("Ambiguous reference"),
              "Ambiguity text explicit");
        const auto capture = qEnvironmentVariable("SKETCHYUP_ANNOTATION_VIEW_EVIDENCE");
        if (!capture.isEmpty())
            check(broken.save(capture), "Viewport evidence saved");
        doc.erase(body);
        frame(*view);
        check(!measureAnnotation(doc, *doc.annotations().at(id)).distance,
              "Deleted geometry has no numeric dimension");
        doc.undo();
        doc.undo();
        doc.undo(); // restore unsplit span only
        frame(*view);
        view->selectEntities({{body, SelectionKind::Body, 0}});
        view->hideSelection();
        const auto hidden = frame(*view);
        eraseAnnotation(doc, id);
        const auto absent = frame(*view);
        check(changed(hidden, absent, box, *view) == 0,
              "Hidden attached geometry suppresses annotation");
        doc.undo();
        view->revealHiddenGeometry();
        const auto cut = createSection(doc, "Remove anchors", 0, {{0, 1, 0}, -.5});
        setActiveSection(doc, 0, cut);
        const auto clipped = frame(*view);
        eraseAnnotation(doc, id);
        check(changed(clipped, frame(*view), box, *view) == 0,
              "Clipped attachment suppresses annotation");
        host.close();
        std::cout
            << "Native annotation graphics, units, broken states, visibility and Undo passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
