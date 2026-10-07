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
        const auto room = run(doc, "assembly.room");
        const QJsonValue roomId = room["recipeOperations"].toArray()[0].toObject()["room"];
        run(doc, "assembly.room.adopt_hosted", {{"body", roomId}});
        const auto original = doc.bodies();
        const auto made = run(doc, "assembly.roof");
        const auto report = made["recipeOperations"].toArray()[0].toObject();
        const auto roof = report["roof"].toString().toULongLong();
        view.refresh();
        view.setSelection(roof);
        view.fit();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Roof room is exposed");
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }),
              "Roof room receives native focus");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Roof renderer is ready");
        view.setTool(Viewport::Tool::Move);
        check(view.measurements("[0,0,0]") && view.measurements("0,0,1m"),
              "Native Move can manipulate the created roof group");
        check(doc.worldTransform(roof).point({}) == Vec3{0, 0, 1},
              "Native roof move uses exact document units");
        for (const auto &[id, body] : original)
            check(*doc.bodies().at(id) == *body, "Moving the roof preserves every room record");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.worldTransform(roof).point({}) == Vec3{}, "Native Undo restores the roof pose");
        const auto volume = measureEntity(doc, {roof, SelectionKind::Body, 0}).world.volume;
        check(volume && std::abs(*volume - 4.554) < 1e-5, "Native roof remains a validated solid");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved, "Native roof persists exactly");
        view.setTool(Viewport::Tool::Select);
        view.setSelection(0);
        view.standardView(0);
        view.fit();
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            check(view.grabFramebuffer().save(capture + "/room-with-roof.png"),
                  "Save actual native pitched roof framebuffer");
        }
        check(view.renderStats().glError == 0, "Roof rendering leaves clean GL state");
        std::cout << "Native roof rendering, grouped Move, preserved room, Undo and persistence "
                     "passed; DPR "
                  << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }),
              "Roof window completes shutdown");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
