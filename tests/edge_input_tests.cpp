#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/components.hpp"
#include "core/consolidation.hpp"
#include "core/edge_appearance.hpp"
#include "core/groups.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void action(Window &window, const char *name) {
    auto *entry = window.findChild<QAction *>(name);
    check(entry && entry->isEnabled(), "Native edge action exists and is enabled");
    entry->trigger();
    QCoreApplication::processEvents();
    if (qEnvironmentVariableIsSet("SKETCHYUP_EDGE_CAPTURE"))
        QTest::qWait(200);
}
void move(Viewport &view, QPointF point) {
    QMouseEvent event(QEvent::MouseMove, point, view.mapToGlobal(point.toPoint()), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
    QCoreApplication::processEvents();
}
QImage frame(Viewport &view) {
    QCoreApplication::processEvents();
    const auto image = view.grabFramebuffer();
    check(!image.isNull() && view.rendererReady(), "Edge framebuffer available");
    return image;
}
double strokeFraction(Viewport &view) {
    view.selectEntities({});
    move(view, {30, 40});
    const auto image = frame(view);
    int marked = 0;
    for (int i = 0; i < 100; ++i) {
        const auto point = view.project({0, -.8 + 1.6 * i / 99, 1});
        const auto x = qRound(point.x() * image.width() / view.width());
        const auto y = qRound(point.y() * image.height() / view.height());
        bool dark = false;
        for (int dx = -1; dx <= 1; ++dx)
            dark |= image.pixelColor(x + dx, y).red() < 180;
        marked += dark;
    }
    return marked / 100.;
}
SelectedEntity plane(Document &doc) {
    doc = Document{};
    doc.addFace({{{-2, -1, 1}, {0, -1, 1}, {0, 1, 1}, {-2, 1, 1}}});
    doc.addFace({{{0, -1, 1}, {2, -1, 1}, {2, 1, 1}, {0, 1, 1}}});
    const auto body = consolidateContext(doc).destination;
    doc.paint(body, {.95f, .95f, .95f});
    for (const auto &[edge, record] : doc.bodies().at(body)->topology.edges) {
        const auto &surface = doc.bodies().at(body)->surface;
        if (surface.vertices.at(record.a).x == 0 && surface.vertices.at(record.b).x == 0)
            return {body, SelectionKind::Edge, edge};
    }
    throw std::runtime_error("Missing native seam");
}
void inferred(Viewport &view, SelectedEntity edge, bool expected) {
    view.setDrawingPlane(DrawingPlane::make({0, 0, 1}, {0, 0, 1}, {1, 0, 0}));
    view.setTool(Viewport::Tool::Line);
    QElapsedTimer timer;
    timer.start();
    while (!view.inferenceReady() && timer.elapsed() < 10000)
        QTest::qWait(10);
    check(view.inferenceReady(), "Edge inference index ready");
    move(view, view.project({0, 0, 1}));
    bool found = false;
    for (const auto &candidate : view.inference().candidates)
        found |= candidate.body == edge.body && candidate.entityType == InferenceEntity::Edge &&
                 candidate.entity == edge.entity;
    check(found == expected, "Inference follows explicit edge visibility mode");
    view.setTool(Viewport::Tool::Select);
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Window window;
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Edge window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Edge window active");
        auto &doc = window.document();
        auto &view = *window.viewport();
        const auto edge = plane(doc);
        const auto original = doc.bodies().at(edge.body);
        view.refresh();
        view.standardView(1);
        view.fit();
        const auto point = view.project({0, .27, 1});
        check(view.selectionAt(point) == edge, "Ordinary seam is pickable");
        check(strokeFraction(view) > .9, "Ordinary seam draws a solid stroke");
        inferred(view, edge, true);
        view.selectEntities({edge});
        action(window, "edge.smooth");
        check(edgeAppearance(*doc.bodies().at(edge.body), edge.entity) ==
                  EdgeAppearance{false, false, true},
              "Native smooth action changes only smooth");
        check(strokeFraction(view) > .9, "Smooth alone retains visible seam");
        view.selectEntities({edge});
        action(window, "edge.flat");
        const auto history = doc.history().total;
        action(window, "edge.hide");
        check(
            doc.history().total == history + 1 && view.selectionState().entities().empty() &&
                view.selectionAt(point) != edge,
            "Hide is one Undo item and immediately removes suppressed edge from selection/picking");
        check(strokeFraction(view) < .05, "Hidden seam is absent from framebuffer");
        inferred(view, edge, false);
        action(window, "edit.undo");
        check(view.selectionAt(point) == edge && strokeFraction(view) > .9,
              "Undo restores native edge picking and solid stroke");
        view.selectEntities({edge});
        action(window, "edge.hide");
        action(window, "selection.showHidden");
        const auto revision = doc.revision();
        check(view.selectionAt(point) == edge, "Explicit reveal mode makes hidden edge pickable");
        const auto dashed = strokeFraction(view);
        check(dashed > .1 && dashed < .85, "Revealed hidden seam has a distinct dashed stroke");
        inferred(view, edge, true);
        check(doc.revision() == revision, "Reveal and inference do not edit the model");
        const auto capture = qEnvironmentVariable("SKETCHYUP_EDGE_CAPTURE");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(frame(view).save(capture + "/revealed-edges.png"),
                  "Capture explicit edge reveal");
        }
        view.selectEntities({edge});
        action(window, "edge.soften");
        action(window, "edge.reveal");
        check(edgeAppearance(*doc.bodies().at(edge.body), edge.entity) ==
                  EdgeAppearance{false, true, false},
              "Clearing hidden does not accidentally clear soft");
        action(window, "selection.showHidden");
        check(view.selectionAt(point) != edge && strokeFraction(view) < .05,
              "Soft alone suppresses stroke and ordinary picking");
        action(window, "selection.showHidden");
        view.selectEntities({edge});
        action(window, "edge.smooth");
        action(window, "edge.harden");
        check(edgeAppearance(*doc.bodies().at(edge.body), edge.entity) ==
                  EdgeAppearance{false, false, true},
              "Harden leaves requested smooth shading intact");
        action(window, "selection.showHidden");
        check(view.selectionAt(point) == edge && strokeFraction(view) > .9,
              "Harden restores ordinary stroke");
        check(doc.bodies().at(edge.body)->surface == original->surface &&
                  doc.bodies().at(edge.body)->topology == original->topology,
              "All six native appearance actions preserve topology and face geometry");
        const auto saved = encodeContainer(doc);
        doc = decodeContainer(saved);
        view.refresh();
        check(view.selectionAt(point) == edge &&
                  edgeAppearance(*doc.bodies().at(edge.body), edge.entity).smooth,
              "Native appearance and pick identities survive reopen");

        // Exercise shared canonical publication through the actual menu actions.
        const auto group = createGroup(doc, {edge.body});
        const auto component = createComponent(doc, group);
        const auto other =
            placeComponent(doc, component.definition,
                           Transform::translation({6, 0, 0}) * Transform::scaling({-1, 1, 1}));
        view.refresh();
        view.enterContext(group);
        view.selectEntities({edge});
        action(window, "edge.soften");
        for (const auto &[id, record] : doc.bodies())
            if (!record->surface.faces.empty())
                check(edgeAppearance(*record, edge.entity).soft,
                      "Native scoped edge action updates reflected shared instance");
        view.showHiddenGeometry(true);
        view.selectEntities({edge});
        view.makeComponentUnique(true);
        view.selectEntities({edge});
        const auto before = doc.bodies();
        action(window, "edge.harden");
        for (const auto &[member, scene] : doc.instances().at(other.instance)->members)
            check(doc.bodies().at(scene) == before.at(scene),
                  "Make Unique preserves other instance edge records");
        check(!edgeAppearance(*doc.bodies().at(edge.body), edge.entity).soft,
              "Unique native edge edit changes only the active instance");
        view.leaveContext();
        view.selectEntities({edge});
        check(view.selectionState().entities().empty(),
              "Reveal cannot bypass component editing boundary");
        view.enterContext(group);
        setEntityState(doc, group, {}, true);
        view.refresh();
        view.selectEntities({edge});
        check(view.selectionState().entities().empty(),
              "Reveal cannot bypass persistent ancestor lock");
        // Hidden loose-wire endpoints must not leak through vertex inference.
        doc = Document{};
        const auto wireBody = doc.addWire(0, {-1, 0, 1}, {1, 0, 1});
        const auto wireEdge = doc.bodies().at(wireBody)->topology.edges.begin()->first;
        const SelectedEntity wire{wireBody, SelectionKind::Edge, wireEdge};
        setEdgeAppearance(doc, {wire}, 0, true, {}, {});
        view.refresh();
        view.showHiddenGeometry(false);
        view.standardView(1);
        view.fit();
        inferred(view, wire, false);
        for (bool reveal : {false, true}) {
            view.showHiddenGeometry(reveal);
            view.setTool(Viewport::Tool::Line);
            move(view, view.project({-1, 0, 1}));
            bool endpoint = false;
            for (const auto &candidate : view.inference().candidates)
                endpoint |= candidate.body == wireBody && candidate.kind == InferenceKind::Endpoint;
            check(endpoint == reveal, "Loose hidden-wire endpoint inference obeys reveal mode");
        }
        view.setTool(Viewport::Tool::Select);
        check(view.renderStats().glError == 0, "Edge display produces no GL errors");
        std::cout << "Native edge actions, dashed reveal, picking/inference, Undo, reopen and "
                     "shared scope passed; DPR="
                  << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
