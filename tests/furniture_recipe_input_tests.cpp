#include "app/window.hpp"
#include "automation/commands.hpp"
#include "core/entity_measure.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <iostream>
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
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir settings;
    qputenv("XDG_CONFIG_HOME", settings.path().toUtf8());
    qputenv("XDG_DATA_HOME", settings.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    auto &doc = window.document();
    auto &view = *window.viewport();
    try {
        run(doc, "geometry.face",
            {{"name", "Existing study base"},
             {"loops",
              QJsonArray{QJsonArray{QJsonArray{-.15, -.15, -.01}, QJsonArray{2.65, -.15, -.01},
                                    QJsonArray{2.65, 1, -.01}, QJsonArray{-.15, 1, -.01}}}}});
        const auto made = run(doc, "assembly.table");
        run(doc, "assembly.cabinet", {{"origin", QJsonArray{1.6, 0, 0}}});
        const auto report = made["recipeOperations"].toArray()[0].toObject();
        const auto furniture = report["assembly"].toString().toULongLong();
        const auto original = doc.bodies();
        view.refresh();
        view.setSelection(furniture);
        view.fit();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Furniture study is exposed");
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }),
              "Furniture study receives native focus");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Furniture renderer is ready");
        view.setTool(Viewport::Tool::Move);
        check(view.measurements("[0,0,0]") && view.measurements("0,0,1m"),
              "Native Move can manipulate the created furniture group");
        check(doc.worldTransform(furniture).point({}) == Vec3{0, 0, 1},
              "Native furniture move uses exact document units");
        for (const auto &[id, body] : original)
            if (id != furniture)
                check(*doc.bodies().at(id) == *body,
                      "Moving the furniture preserves every existing record");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.worldTransform(furniture).point({}) == Vec3{},
              "Native Undo restores the furniture pose");
        const auto top = report["members"].toArray()[0].toObject()["body"].toString().toULongLong();
        const auto volume = measureEntity(doc, {top, SelectionKind::Body, 0}).world.volume;
        check(volume && std::abs(*volume - .0384) < 1e-7,
              "Table top retains independently expected solid volume");
        const auto assembly = measureEntity(doc, {furniture, SelectionKind::Body, 0});
        check(assembly.local.bounds &&
                  length(assembly.local.bounds->dimensions() - Vec3{1.2, .8, .75}) < 1e-6,
              "Native table retains its exact envelope");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved,
              "Native furniture persists exactly");
        view.setTool(Viewport::Tool::Select);
        view.setSelection(0);
        view.standardView(0);
        view.fit();
        const auto previousFrames = view.renderStats().frames;
        view.update();
        check(QTest::qWaitFor([&] { return view.renderStats().frames > previousFrames; }),
              "Final scene is repainted after tool and camera changes");
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(view.grabFramebuffer().save(capture + "/table-and-cabinet.png"),
                  "Save actual native furniture framebuffer");
        }
        check(view.renderStats().glError == 0, "Furniture rendering leaves clean GL state");
        std::cout
            << "Native furniture rendering, grouped Move, preserved scene, Undo and persistence "
               "passed; DPR "
            << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }),
              "Furniture window completes shutdown");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
