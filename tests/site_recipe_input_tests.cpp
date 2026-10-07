#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <iostream>
#include <numbers>
using namespace sketchy;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QJsonObject run(Document &doc, const char *command, QJsonObject fields = {}) {
    fields["command"] = command;
    return executeBatch(doc, {{"apiVersion", 1},
                              {"documentId", QString::fromStdString(doc.identity())},
                              {"expectedRevision", QString::number(doc.revision())},
                              {"commands", QJsonArray{fields}}});
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    auto &doc = window.document();
    auto &view = *window.viewport();
    try {
        doc = loadDocument(QStringLiteral(SOURCE_DIR "/examples/m6-site-before.sketchyup"));
        Id site{};
        for (const auto &[bodyId, body] : doc.bodies())
            if (body->name == "Site study" && !body->parent) {
                check(!site, "Site fixture has exactly one named assembly");
                site = bodyId;
            }
        check(site != 0, "Site fixture contains its authored assembly");
        const auto original = doc.readSnapshot();
        run(doc, "assembly.site_place",
            {{"body", QString::number(site)},
             {"position", QJsonArray{100000125, 200000250, 12500}},
             {"positionUnit", "mm"},
             {"frame", "world"},
             {"yawDeltaRadians", std::numbers::pi / 6}});
        const Vec3 origin{100000.125, 200000.25, 12.5};
        const auto bounds = measureEntity(doc, {site, SelectionKind::Body, 0}).world.bounds;
        check(bounds.has_value(), "Placed site has finite world bounds");
        view.refresh();
        view.setSelection(site);
        view.frameBounds(bounds->low, bounds->high);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Site study is exposed");
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }),
              "Site study receives native focus");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Site renderer is ready");
        view.setTool(Viewport::Tool::Move);
        check(view.measurements("[0,0,0]") && view.measurements("1mm,0,0"),
              "Native Move can manipulate the created site group");
        check(length(doc.worldTransform(site).point({}) - (origin + Vec3{.001, 0, 0})) < 1e-8,
              "Native Move retains a one-millimetre increment at distant world coordinates");
        for (const auto &[bodyId, body] : original.bodies())
            if (bodyId != site)
                check(*doc.bodies().at(bodyId) == *body,
                      "Site placement and native Move preserve every local body record");
        check(doc.hostedComponents() == original.hostedComponents() &&
                  doc.definitions() == original.definitions() &&
                  doc.instances() == original.instances(),
              "Native site workflow preserves hosted and shared component relationships");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(length(doc.worldTransform(site).point({}) - origin) < 1e-8,
              "Native Undo restores the placed site origin");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved, "Native site persists exactly");
        view.setTool(Viewport::Tool::Select);
        view.setSelection(0);
        view.standardView(0);
        view.frameBounds(bounds->low, bounds->high);
        const auto previousFrames = view.renderStats().frames;
        view.update();
        check(QTest::qWaitFor([&] { return view.renderStats().frames > previousFrames; }),
              "Final scene is repainted after tool and camera changes");
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(view.grabFramebuffer().save(capture + "/placed-site.png"),
                  "Save actual native placed-site framebuffer");
            // A translated comparison separates model precision from GPU projection precision.
            run(doc, "assembly.site_place",
                {{"body", QString::number(site)},
                 {"position", QJsonArray{0, 0, 0}},
                 {"positionUnit", "m"},
                 {"frame", "world"},
                 {"yawDeltaRadians", 0}});
            const auto nearBounds = measureEntity(doc, {site, SelectionKind::Body, 0}).world.bounds;
            view.refresh();
            view.frameBounds(nearBounds->low, nearBounds->high);
            const auto farFrames = view.renderStats().frames;
            check(QTest::qWaitFor([&] { return view.renderStats().frames > farFrames; }),
                  "Translated comparison repaints");
            check(view.grabFramebuffer().save(capture + "/near-site.png"),
                  "Save translated native comparison framebuffer");
        }
        check(view.renderStats().glError == 0, "Site rendering leaves clean GL state");
        std::cout << "Native site rendering, grouped Move, preserved scene, Undo and persistence "
                     "passed; DPR "
                  << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }),
              "Site window completes shutdown");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
