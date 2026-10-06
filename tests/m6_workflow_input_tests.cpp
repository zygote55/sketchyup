#include "app/window.hpp"
#include "m6_fixture.hpp"
#include <QAction>
#include <QApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <iostream>
using namespace sketchy;
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    auto &doc = window.document();
    auto &view = *window.viewport();
    try {
        auto study = m6::study();
        doc = study.placed.readSnapshot();
        view.refresh();
        window.show();
        m6::check(QTest::qWaitForWindowExposed(&window), "Integrated native window exposed");
        window.activateWindow();
        m6::check(QTest::qWaitFor(
                      [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }),
                  "Integrated native focus");
        m6::check(QTest::qWaitFor([&] { return view.rendererReady(); }),
                  "Integrated renderer ready");
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        auto frame = [&](Id body, const QString &name) {
            const auto bounds = measureEntity(doc, {body, SelectionKind::Body, 0}).world.bounds;
            m6::check(bounds.has_value(), "Integrated view has finite bounds");
            view.standardView(0);
            view.frameBounds(bounds->low, bounds->high);
            const auto frames = view.renderStats().frames;
            view.update();
            m6::check(QTest::qWaitFor([&] { return view.renderStats().frames > frames; }),
                      "Integrated view repaints");
            if (!capture.isEmpty()) {
                QDir().mkpath(capture);
                m6::check(view.grabFramebuffer().save(capture + "/" + name + ".png"),
                          "Retain actual integrated framebuffer");
            }
        };
        frame(study.site, "m6-study");
        view.enterContext(study.site);
        view.enterContext(study.joint);
        view.setSelection(study.right);
        const auto before = doc.readSnapshot();
        const auto origin = doc.worldTransform(study.right).point({});
        view.setTool(Viewport::Tool::Move);
        m6::check(view.measurements("[0,0,0]") && view.measurements("0,0,120mm"),
                  "Native Move exposes the upper joint member");
        m6::check(length(doc.worldTransform(study.right).point({}) - origin - Vec3{0, 0, .12}) <
                      1e-8,
                  "Native joint separation is exactly 120 mm at distant coordinates");
        for (const auto &[bodyId, body] : before.bodies())
            if (bodyId != study.right)
                m6::check(*doc.bodies().at(bodyId) == *body,
                          "Exploding the joint preserves every other study record");
        view.setTool(Viewport::Tool::Select);
        view.setSelection(0);
        frame(study.joint, "m6-joint-exploded");
        window.findChild<QAction *>("edit.undo")->trigger();
        m6::check(doc.bodies() == before.bodies(),
                  "Native Undo restores exact joined-study records");
        const auto bytes = encodeContainer(doc);
        m6::check(encodeContainer(decodeContainer(bytes)) == bytes,
                  "Native integrated result persists exactly");
        m6::assemblies(doc);
        m6::check(view.renderStats().glError == 0,
                  "Integrated native rendering has clean GL state");
        std::cout << "Native integrated study, 120 mm joint separation, preservation, Undo and "
                     "exact reopen passed; DPR "
                  << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        m6::check(QTest::qWaitFor([&] { return !window.isVisible(); }),
                  "Integrated window closes cleanly");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
