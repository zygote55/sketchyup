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
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(Document &doc, F operation) {
    const auto before = encodeContainer(doc);
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected && encodeContainer(doc) == before,
          "Native adoption rejects protected geometry without an edit");
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
    QApplication app(argc, argv);
    Window window;
    auto &doc = window.document();
    auto &view = *window.viewport();
    try {
        const auto result = executeBatch(
            doc, {{"apiVersion", 1},
                  {"documentId", QString::fromStdString(doc.identity())},
                  {"expectedRevision", QString::number(doc.revision())},
                  {"commands", QJsonArray{QJsonObject{{"command", "assembly.room"}}}}});
        const auto report = result["recipeOperations"].toArray().first().toObject();
        const auto room = report["room"].toString().toULongLong();
        const auto wall = report["wall"].toString().toULongLong();
        const auto first =
            report["windows"].toArray()[0].toObject()["body"].toString().toULongLong();
        view.refresh();
        view.setSelection(room);
        view.fit();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Recipe room is exposed");
        window.activateWindow();
        check(QTest::qWaitFor(
                  [&] { return QGuiApplication::focusWindow() == window.windowHandle(); }, 5000),
              "Recipe room receives native focus");
        auto *action = window.findChild<QAction *>("assembly.room.adopt_hosted");
        check(action && action->isEnabled() &&
                  action->property("command") == "assembly.room.adopt_hosted",
              "Edit menu exposes the shared adoption command for an authored room");
        const auto before = encodeContainer(doc);
        const auto depth = doc.history().total;
        view.enterContext(room);
        view.setSelection(wall);
        view.lockSelection();
        view.enterContext(0);
        view.setSelection(room);
        rejects(doc, [&] { view.adoptSelectedRecipeRoom(); });
        view.unlockContexts();
        view.enterContext(room);
        view.setSelection(first);
        view.lockSelection();
        view.enterContext(0);
        view.setSelection(room);
        rejects(doc, [&] { view.adoptSelectedRecipeRoom(); });
        view.unlockContexts();
        check(encodeContainer(doc) == before, "Native editor locks do not mutate the room");
        view.setSelection(room);
        check(action->isEnabled(), "Unlocked room enables native adoption");
        action->trigger();
        if (doc.hostedComponents().attachments.size() != 2 || doc.history().total != depth + 1 ||
            action->isEnabled())
            std::cerr << "Adoption result: bindings=" << doc.hostedComponents().attachments.size()
                      << " history=" << doc.history().total << "/" << depth + 1
                      << " enabled=" << action->isEnabled() << '\n';
        check(doc.hostedComponents().attachments.size() == 2 && doc.history().total == depth + 1 &&
                  !action->isEnabled(),
              "Native menu upgrades both windows in one Undo and disables repeat adoption");
        const auto saved = encodeContainer(doc);
        check(encodeContainer(decodeContainer(saved)) == saved,
              "Native adopted room persists exactly");
        view.enterContext(room);
        view.setSelection(first);
        view.setTool(Viewport::Tool::Move);
        check(view.measurements("[0,0,0]") && view.measurements("0.2m,0,0"),
              "Native Move updates an adopted window and its opening");
        const auto volume = measureEntity(doc, {wall, SelectionKind::Body, 0}).local.volume;
        check(volume && std::abs(*volume - 9.888) < 1e-6,
              "Moved native recipe retains the expected closed wall volume");
        const auto capture = qEnvironmentVariable("SKETCHYUP_CAPTURE_DIR");
        if (!capture.isEmpty()) {
            QDir().mkpath(capture);
            view.setTool(Viewport::Tool::Select);
            view.standardView(0);
            view.fit();
            check(view.grabFramebuffer().save(capture + "/adopted-room.png"),
                  "Capture actual native adopted room");
        }
        window.findChild<QAction *>("edit.undo")->trigger();
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.hostedComponents().attachments.empty(),
              "Undo movement and adoption restores the original recipe relationship policy");
        view.enterContext(0);
        view.setSelection(room);
        check(action->isEnabled(), "Undo makes native adoption available again");
        std::cout << "Native recipe adoption action, indirect editor locks, Move, persistence "
                     "and Undo passed; DPR "
                  << view.devicePixelRatioF() << '\n';
        doc = Document{};
        view.refresh();
        window.close();
        check(QTest::qWaitFor([&] { return !window.isVisible(); }, 2000),
              "Native recipe window completes shutdown");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
