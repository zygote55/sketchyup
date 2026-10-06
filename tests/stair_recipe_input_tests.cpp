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
        const auto room = run(doc, "assembly.room");
        const QJsonValue roomId = room["recipeOperations"].toArray()[0].toObject()["room"];
        run(doc, "assembly.room.adopt_hosted", {{"body", roomId}});
        run(doc, "assembly.roof");
        const auto original = doc.bodies();
        const auto made = run(doc, "assembly.stairs", {{"origin", QJsonArray{7, 0, 0}}});
        const auto report = made["recipeOperations"].toArray()[0].toObject();
        const auto stairs = report["stairs"].toString().toULongLong();
        view.refresh();
        view.setSelection(stairs);
        view.fit();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Stair room is exposed");
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }),
              "Stair room receives native focus");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Stair renderer is ready");
        view.setTool(Viewport::Tool::Move);
        check(view.measurements("[0,0,0]") && view.measurements("0,0,1m"),
              "Native Move can manipulate the created stair group");
        check(doc.worldTransform(stairs).point({}) == Vec3{7, 0, 1},
              "Native stairs move uses exact document units");
        for (const auto &[id, body] : original)
            check(*doc.bodies().at(id) == *body, "Moving the stairs preserves every room record");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.worldTransform(stairs).point({}) == Vec3{7, 0, 0},
              "Native Undo restores the stairs pose");
        const auto volume = measureEntity(doc, {stairs, SelectionKind::Body, 0}).world.volume;
        check(volume && std::abs(*volume - 4.368) < 1e-5,
              "Native stair flight remains a validated solid");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved, "Native stair flight persists exactly");
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
            check(view.grabFramebuffer().save(capture + "/room-with-stairs.png"),
                  "Save actual native stair framebuffer");
        }
        check(view.renderStats().glError == 0, "Stair rendering leaves clean GL state");
        std::cout << "Native stairs rendering, grouped Move, preserved room, Undo and persistence "
                     "passed; DPR "
                  << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }),
              "Stair window completes shutdown");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
