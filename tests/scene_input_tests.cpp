#include "app/scenes_panel.hpp"
#include "app/window.hpp"
#include "core/scenes.hpp"
#include "core/sections.hpp"
#include "core/tags.hpp"
#include "io/document_io.hpp"
#include "io/scenes_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void sync(Window &window) {
    window.viewport()->refresh();
    QMetaObject::invokeMethod(window.viewport(), "changed");
    QCoreApplication::processEvents();
}
void click(Window &window, const char *name) {
    auto *button = window.findChild<QPushButton *>(name);
    check(button, "Scene button exists");
    button->click();
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
}
void type(QDialog *dialog, const QString &text) {
    auto *field = dialog->findChild<QLineEdit *>("savedSceneName");
    check(field, "Scene name input exists");
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
}
void modal(Window &window, const char *button, const std::function<void(QDialog *)> &operation) {
    bool opened = false;
    std::exception_ptr failure;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("savedSceneDialog");
        if (!dialog || !dialog->isVisible())
            return;
        timer.stop();
        dialog->activateWindow();
        if (!QTest::qWaitFor(
                [&] { return QGuiApplication::focusWindow() == dialog->windowHandle(); })) {
            dialog->reject();
            return;
        }
        opened = true;
        try {
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start();
    click(window, button);
    check(opened, "Scene editor opened");
    if (failure)
        std::rethrow_exception(failure);
}
void choose(Window &window, Id id) {
    auto *list = window.findChild<QListWidget *>("savedScenesList");
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toULongLong() == id) {
            list->setCurrentRow(i);
            return;
        }
    throw std::runtime_error("Missing scene list row");
}
} // namespace
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir isolated;
    qputenv("XDG_CONFIG_HOME", isolated.path().toUtf8());
    QApplication app(argc, argv);
    try {
        Window window;
        window.resize(1280, 950);
        auto &doc = window.document();
        auto *view = window.viewport();
        const auto body = doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 2);
        const auto tag = createTag(doc, "Scene tag");
        const auto namedSection = createSection(doc, "Scene cut", 0, {{1, 0, 0}, -1});
        setActiveSection(doc, 0, namedSection);
        doc.markSaved();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Scene window exposed");
        auto *action = window.findChild<QAction *>("view.scenes");
        check(action, "View menu exposes scenes");
        action->trigger();
        view->setReducedMotion(true);
        sync(window);
        auto *tabs = view->findChild<QTabBar *>("viewportSceneTabs");
        check(tabs && !tabs->isVisible(), "Empty scene tabs stay hidden");
        const auto initial = view->captureSceneSnapshot(true, true, true, true);
        const auto initialRevision = doc.revision();
        modal(window, "sceneNewButton", [&](QDialog *dialog) {
            type(dialog, "Overview");
            accept(dialog);
        });
        check(doc.scenes().size() == 1 && doc.revision() == initialRevision + 1,
              "Native capture uses one shared scene edit");
        const auto first = doc.scenes().begin()->first;
        check(doc.scenes().at(first)->snapshot == initial,
              "Native capture records chosen properties");
        check(tabs->isVisible() && tabs->count() == 1, "Scene appears at viewport bottom");
        view->standardView(1);
        view->setFieldOfView(70);
        view->setClipPlane(std::array<double, 4>{0, 0, 1, -1});
        auto style = doc.style();
        style.mode = ModelStyleMode::Monochrome;
        style.background = {.06f, .08f, .12f};
        style.profiles = true;
        doc.setStyle(style);
        editTag(doc, tag, {}, {}, false);
        sync(window);
        setActiveSection(doc, 0, std::nullopt);
        const auto beforeRename = doc.scenes().at(first)->snapshot;
        choose(window, first);
        modal(window, "sceneRenameButton", [&](QDialog *dialog) {
            type(dialog, "Original view");
            accept(dialog);
        });
        check(doc.scenes().at(first)->snapshot == beforeRename,
              "Rename never recaptures the current view");
        modal(window, "sceneNewButton", [&](QDialog *dialog) {
            type(dialog, "Top camera");
            for (const auto *name :
                 {"savedSceneVisibility", "savedSceneStyle", "savedSceneSection"})
                dialog->findChild<QCheckBox *>(name)->setChecked(false);
            accept(dialog);
        });
        const auto second = doc.scenes().rbegin()->first;
        const auto top = *doc.scenes().at(second)->snapshot.camera;
        check(!doc.scenes().at(second)->snapshot.style &&
                  !doc.scenes().at(second)->snapshot.visibility,
              "Unchecked properties omitted from scene");
        const auto history = doc.history().total;
        view->recallSavedScene(first);
        check(view->captureSceneSnapshot(true, true, true, true) == initial &&
                  doc.history().total == history + 1,
              "Recall restores opted-in camera, visibility, style and section");
        doc.undo();
        sync(window);
        check(doc.style() == style && !doc.tags().at(tag)->visible && doc.activeSections().empty(),
              "Recall model state has one Undo");
        view->setClipPlane(std::array<double, 4>{0, 0, 1, -.5});
        const auto beforeCamera = view->captureSceneSnapshot(false, true, true, true);
        const auto revision = doc.revision();
        view->recallSavedScene(second);
        check(*view->captureSceneSnapshot(true, false, false, false).camera == top,
              "Camera-only scene recalls its exact native pose");
        check(view->captureSceneSnapshot(false, true, true, true) == beforeCamera &&
                  doc.revision() == revision,
              "Camera-only recall leaves unowned state and model history alone");
        view->standardView(2);
        int index = -1;
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabData(i).toULongLong() == second)
                index = i;
        QTest::mouseClick(tabs, Qt::LeftButton, Qt::NoModifier, tabs->tabRect(index).center());
        check(*view->captureSceneSnapshot(true, false, false, false).camera == top,
              "Clicking selected tab recalls again after navigation");
        tabs->setFocus();
        QTest::keyClick(tabs, Qt::Key_Left);
        check(view->captureSceneSnapshot(true, true, true, true) == initial,
              "Viewport scene tabs support keyboard recall");
        choose(window, second);
        click(window, "sceneEarlierButton");
        check(orderedScenes(doc).front() == second, "Native ordering preserves stable IDs");
        click(window, "sceneDeleteButton");
        check(!doc.scenes().contains(second), "Native delete removes selected scene");
        doc.undo();
        sync(window);
        check(doc.scenes().contains(second) && orderedScenes(doc).front() == second,
              "Delete Undo restores record and order");
        choose(window, first);
        const auto beforeRejected = encodeContainer(doc);
        modal(window, "sceneUpdateButton", [&](QDialog *dialog) {
            for (const auto *name : {"savedSceneCamera", "savedSceneVisibility", "savedSceneStyle",
                                     "savedSceneSection"})
                dialog->findChild<QCheckBox *>(name)->setChecked(false);
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("savedSceneDialogError")->text().isEmpty(),
                  "Empty capture keeps editor and useful error");
            check(encodeContainer(doc) == beforeRejected,
                  "Rejected native capture leaves bytes intact");
            dialog->findChild<QCheckBox *>("savedSceneCamera")->setChecked(true);
            accept(dialog);
        });
        check(doc.scenes().at(first)->snapshot.camera &&
                  !doc.scenes().at(first)->snapshot.visibility,
              "Update replaces selected property ownership");
        doc.undo();
        sync(window);
        choose(window, first);
        const auto unchanged = doc.revision();
        modal(window, "sceneRenameButton", [&](QDialog *dialog) { accept(dialog); });
        check(doc.revision() == unchanged, "Unchanged scene name is a no-op");
        modal(window, "sceneUpdateButton", [&](QDialog *dialog) {
            doc.setDisplayUnits(DisplayUnit::Millimeters);
            accept(dialog);
            check(dialog->isVisible() && dialog->findChild<QLabel *>("savedSceneDialogError")
                                             ->text()
                                             .contains("Document changed"),
                  "Stale scene draft rejected in place");
        });
        view->setReducedMotion(false);
        view->standardView(3);
        view->recallSavedScene(second);
        check(QTest::qWaitFor(
                  [&] {
                      return *view->captureSceneSnapshot(true, false, false, false).camera == top;
                  },
                  1500),
              "Animated scene transition reaches exact saved pose");
        view->standardView(3);
        view->recallSavedScene(second);
        QTest::qWait(30);
        view->standardView(2);
        const auto interrupted = view->captureSceneSnapshot(true, false, false, false);
        QTest::qWait(220);
        check(view->captureSceneSnapshot(true, false, false, false) == interrupted,
              "Manual camera motion cancels scene transition");
        view->setReducedMotion(true);
        view->recallSavedScene(first);
        choose(window, first);
        const auto evidence = qEnvironmentVariable("SKETCHYUP_SCENE_EVIDENCE");
        if (!evidence.isEmpty()) {
            view->update();
            QTest::qWait(100);
            check(window.grab().save(evidence + "/window.png"), "Raw scene window capture");
            modal(window, "sceneUpdateButton", [&](QDialog *dialog) {
                check(dialog->grab().save(evidence + "/editor.png"), "Raw scene editor capture");
                dialog->reject();
            });
        }
        const auto expected = encodeScenes(doc.scenes());
        const auto path = isolated.filePath("scenes.sketchyup");
        saveDocument(doc, path);
        auto reopened = loadDocument(path);
        check(encodeScenes(reopened.scenes()) == expected,
              "Native captured scenes save and reopen exactly");
        doc.erase(body);
        eraseTag(doc, tag);
        sync(window);
        choose(window, first);
        check(window.findChild<QLabel *>("sceneDetails")->text().contains("Missing references"),
              "Scenes panel diagnoses missing refs");
        view->recallSavedScene(first);
        check(doc.scenes().at(first)->snapshot == initial,
              "Recalling missing geometry does not rewrite snapshot");
        const auto currentScenes = doc.scenes().size();
        doc = Document{};
        sync(window);
        check(currentScenes > 0 && !tabs->isVisible() &&
                  window.findChild<QListWidget *>("savedScenesList")->count() == 0,
              "Replacing document clears scene UI");
        check(view->renderStats().glError == 0, "Scene workflow leaves OpenGL error-free");
        std::cout
            << "Native scenes: selective capture/recall, tab keyboard/reclick, rename/update, "
               "Undo/save, missing refs and reduced-motion transitions passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
