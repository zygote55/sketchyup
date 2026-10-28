// R099.d2 (UX_DESIGN §17.1 to §17.3, §17.8, §17.10): the Presentations dialog, driven the way a
// person drives it. The Camera menu opens it; the path editor, the draft create flow, the scene
// picker and the preset editor are operated from the keyboard. Each edit is checked as one undo
// step, the live count and the refusal wording are checked as shown, broken paths are checked as
// displayed, and the dialog is checked at a large text size. The model changes only through the
// dialog's commands.
#include "app/presentations_dialog.hpp"
#include "app/render_panel.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "core/presentation_records.hpp"
#include "core/scenes.hpp"
#include "io/scenes_io.hpp"
#include "shortcut_focus.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFontInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <exception>
#include <iostream>

using namespace sketchy;

namespace {

void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

void checkText(const QString &actual, const QString &expected, const char *message) {
    if (!actual.contains(expected))
        throw std::runtime_error(std::string(message) + ": expected \"" + expected.toStdString() +
                                 "\" in \"" + actual.toStdString() + "\"");
}

// The widget that has keyboard focus, which is the inner line edit of a spin box.
QWidget *focused() { return QApplication::focusWidget(); }

// Presses Tab until `target` (or one of its children, such as a spin box's line edit) has focus,
// as a keyboard user reaches a control. A control that cannot be reached fails the check.
bool tabTo(QWidget *target, int limit = 120) {
    for (int step = 0; step < limit; ++step) {
        if (focused() == target || target->isAncestorOf(focused()))
            return true;
        if (!focused())
            std::cerr << "no focus while looking for " << target->objectName().toStdString()
                      << '\n';
        QTest::keyClick(focused() ? focused() : target, Qt::Key_Tab);
    }
    const auto name = focused()
                          ? focused()->objectName() + "/" + focused()->metaObject()->className()
                          : QString("none");
    std::cerr << "tabTo stopped at " << name.toStdString() << " looking for "
              << target->objectName().toStdString() << '\n';
    return focused() == target || target->isAncestorOf(focused());
}

// Selects all text in the focused field, types `text` and presses Enter (the commit of a field).
void typeInto(QWidget *target, const char *text) {
    check(tabTo(target), "Keyboard reaches the field");
    QTest::keyClick(focused(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(focused(), text);
    QTest::keyClick(focused(), Qt::Key_Return);
}

// Makes the window of `widget` the active window and puts focus on `widget`. Undo, Redo, Cancel and
// a closing modal picker move focus elsewhere, and offscreen nothing gives it back on its own.
void refocusWidget(QWidget *widget) {
    // Offscreen, activateWindow alone does not make the window the application's active window.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(widget->window());
    QT_WARNING_POP
    widget->window()->activateWindow();
    widget->setFocus(Qt::OtherFocusReason);
    check(QTest::qWaitFor([&] { return focused() == widget; }, 2000), "The dialog takes focus");
}

// Every control a Tab stop reaches has an accessible name (the audit's rule).
void checkNamed(QWidget &dialog) {
    for (auto *widget : dialog.findChildren<QWidget *>()) {
        if (!widget->isVisibleTo(&dialog) || !widget->isEnabled() ||
            !(widget->focusPolicy() & Qt::TabFocus))
            continue;
        auto *accessible = QAccessible::queryAccessibleInterface(widget);
        const auto name = accessible ? accessible->text(QAccessible::Name) : QString();
        check(!name.trimmed().isEmpty(),
              ("Unnamed control: " + widget->objectName()).toUtf8().constData());
    }
}

QAction *actionNamed(QWidget &window, const char *name) {
    auto *found = window.findChild<QAction *>(name);
    check(found, name);
    return found;
}

// Runs `act` on the scene picker while it is open. The picker is modal, so the test waits inside
// it; an exception from `act` is kept and thrown again after the picker has closed.
void pickScenes(QPushButton *addScene, std::function<void(QDialog &)> act) {
    check(tabTo(addScene), "Keyboard reaches Add scene");
    std::exception_ptr failure;
    QTimer::singleShot(0, [&] {
        auto *picker = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        try {
            check(picker && picker->objectName() == "presentationScenePicker",
                  "Scene picker is open");
            act(*picker);
        } catch (...) {
            failure = std::current_exception();
            if (picker)
                picker->reject();
        }
    });
    QTest::keyClick(addScene, Qt::Key_Space);
    // The picker's close hands focus back to nothing offscreen; the control that opened it takes
    // it.
    refocusWidget(addScene);
    if (failure)
        std::rethrow_exception(failure);
}

Id sceneNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, scene] : doc.scenes())
        if (scene->name == name)
            return id;
    throw std::runtime_error("Saved scene exists: " + name);
}

Id pathNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, path] : doc.cameraPaths())
        if (path->name == name)
            return id;
    throw std::runtime_error("Camera path exists: " + name);
}

bool hasPreset(const Document &doc, const std::string &name) {
    for (const auto &[id, preset] : doc.renderPresets())
        if (preset->name == name)
            return true;
    return false;
}

Id presetNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, preset] : doc.renderPresets())
        if (preset->name == name)
            return id;
    throw std::runtime_error("Render preset exists: " + name);
}

void saveScene(Viewport &view, const std::string &name, bool camera) {
    const auto snapshot = view.captureSceneSnapshot(camera, !camera, false, false);
    view.editSavedScenes({QJsonObject{{"command", "saved_scene.create"},
                                      {"name", QString::fromStdString(name)},
                                      {"snapshot", encodeSceneSnapshot(snapshot)}}});
}

// The applied history: each edit adds exactly one step, and Undo takes one away (the total also
// counts undone steps, which a new edit replaces).
size_t steps(const Document &doc) { return doc.history().position; }

} // namespace

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.setValue("recoverySeconds", 0);
    preferences.setValue("render/blenderPath", files.path() + "/absent");
    try {
        // The rules of §17.2 and §17.3 and the planner's arithmetic, without a window.
        check(transitionStepsFor(1.0, 24) == 24, "A one-second transition is 24 steps at 24 fps");
        check(holdStepsFor(0.5, 24) == 12, "A half-second hold is 12 steps at 24 fps");
        check(transitionStepsFor(0.01, 24) == 1, "A transition is at least one step");
        check(scenesThatFit(CameraPathTiming{}) == 7, "The defaults fit seven scenes, not eight");
        check(cameraPathFrameCount(8, CameraPathTiming{}) == 265, "Eight scenes plan 265 frames");
        check(!presentationNameProblem("", {}, "dup").isEmpty(), "An empty name is refused");
        check(presentationNameProblem(" Tour", {}, "dup").contains("space"),
              "A leading space is refused");
        check(presentationNameProblem("Tour", {"Tour"}, "Camera path names must be unique") ==
                  "Camera path names must be unique",
              "A clash uses the engine's words");
        check(presentationNameProblem(QString("x").repeated(1025), {}, "dup").contains("1024"),
              "A name is at most 1024 bytes");
        check(presentationNameProblem("Tour", {}, "dup").isEmpty(), "A plain name is accepted");

        Window window;
        window.resize(1280, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Presentation window exposed");
        // The dialog and the model need no GL renderer, so the suite does not wait for one: the
        // package check runs it where no GL renderer starts (as the IFC dialog suites do).
        auto &doc = window.document();
        auto &view = *window.viewport();
        window.demo();
        view.refresh();
        view.fit();

        // §17.1: Camera > Presentations… is in the Camera menu, right after Render….
        auto *open = actionNamed(window, "camera.presentations");
        check(open->text() == "Presentations…", "The Camera menu entry is Presentations…");
        QStringList cameraItems;
        for (auto *menu : window.menuBar()->findChildren<QMenu *>())
            if (menu->title() == "&Camera")
                for (auto *item : menu->actions())
                    cameraItems << item->text();
        check(cameraItems.indexOf("Presentations…") == cameraItems.indexOf("Render…") + 1,
              "Presentations… follows Render… in the Camera menu");
        check(open->text().endsWith(QChar(0x2026)) &&
                  open->property("category").toString() == "camera",
              "The command palette lists it under Camera");

        // Five camera scenes and one scene without a camera, in the order §17.2 uses.
        for (const char *name : {"Front", "Entrance", "Side", "Garden", "Roof"}) {
            view.fit();
            saveScene(view, name, true);
        }
        saveScene(view, "Section study", false);

        // Opening the dialog: the Camera paths tab, empty, New path enabled, the editor disabled.
        open->trigger();
        auto *dialog = window.findChild<QDialog *>("presentationsDialog");
        check(dialog && dialog->isVisible(), "Camera > Presentations… opens the dialog");
        check(!dialog->isModal(), "The Presentations dialog is non-modal");
        auto *tabs = dialog->findChild<QTabBar *>("presentationsTabs");
        check(tabs && tabs->count() == 2 && tabs->currentIndex() == 0,
              "Two tabs, Camera paths first");
        check(tabs->tabText(0) == "Camera paths" && tabs->tabText(1) == "Render presets",
              "The tab names are Camera paths and Render presets");
        auto *paths = dialog->findChild<QListWidget *>("cameraPathList");
        auto *keyframes = dialog->findChild<QListWidget *>("cameraPathKeyframes");
        auto *newPath = dialog->findChild<QPushButton *>("cameraPathNew");
        auto *deletePath = dialog->findChild<QPushButton *>("cameraPathDelete");
        auto *pathName = dialog->findChild<QLineEdit *>("cameraPathName");
        auto *addScene = dialog->findChild<QPushButton *>("cameraPathAddScene");
        auto *removeKeyframe = dialog->findChild<QPushButton *>("cameraPathRemoveKeyframe");
        auto *fps = dialog->findChild<QSpinBox *>("cameraPathFramesPerSecond");
        auto *transition = dialog->findChild<QDoubleSpinBox *>("cameraPathTransition");
        auto *hold = dialog->findChild<QDoubleSpinBox *>("cameraPathHold");
        auto *count = dialog->findChild<QLabel *>("cameraPathCount");
        auto *refusal = dialog->findChild<QLabel *>("cameraPathError");
        auto *createPath = dialog->findChild<QPushButton *>("cameraPathCreate");
        auto *cancelDraft = dialog->findChild<QPushButton *>("cameraPathCancelDraft");
        auto *status = dialog->findChild<QLabel *>("cameraPathDraftStatus");
        auto *split = dialog->findChild<QSplitter *>("cameraPathSplit");
        auto *close = dialog->findChild<QPushButton *>("presentationsClose");
        auto *exportBatch = dialog->findChild<QPushButton *>("presentationsExportBatch");
        auto *presets = dialog->findChild<QListWidget *>("renderPresetList");
        // Offscreen there is no window manager to activate the dialog, so the test makes it the
        // active window itself; the keyboard then reaches its controls as it would on a desktop.
        refocusWidget(paths);
        check(paths && keyframes && newPath && deletePath && pathName && addScene &&
                  removeKeyframe && fps && transition && hold && count && refusal && createPath &&
                  cancelDraft && status && split && close && exportBatch && presets,
              "Every named control exists");
        check(paths->count() == 0 && !addScene->isEnabled(), "No path yet, so Add scene is off");
        check(newPath->isEnabled() && !deletePath->isEnabled(),
              "New path enabled, Delete path not");
        check(!pathName->isEnabled() && !keyframes->isEnabled(), "The empty editor is disabled");
        // R099.d3 enables the footer's Export batch…, which opens the batch dialog
        // (presentation_batch_input_tests drives that dialog).
        check(exportBatch->isEnabled(), "Export batch… opens the batch dialog");
        check(!close->isDefault(), "Close is not the default, so Enter in a field does not close");
        checkNamed(*dialog);
        check(!refusal->isVisible() && !cancelDraft->isVisible() && !createPath->isVisible(),
              "No refusal and no draft controls before a draft");

        // Keyboard-only create flow (§17.2): New path, Add scene…, two scenes, Create path.
        check(tabTo(newPath), "Keyboard reaches New path");
        QTest::keyClick(focused(), Qt::Key_Space);
        check(paths->count() == 1 && pathName->text() == "Path 1",
              "New path opens a draft named Path 1");
        check(pathName->selectedText() == "Path 1",
              "The draft name is selected, so typing replaces it");
        check(createPath->isVisible() && !createPath->isEnabled(),
              "Create path waits for two scenes");
        checkText(status->text(), "not saved yet", "The draft says it is not saved");
        check(doc.cameraPaths().empty(), "A draft does not change the model");

        const auto beforePick = steps(doc);
        pickScenes(addScene, [](QDialog &picker) {
            auto *list = picker.findChild<QListWidget *>("presentationSceneList");
            check(list && list->count() == 6, "The picker lists the six saved scenes");
            check(list->item(5)->text().contains("no camera"),
                  "A scene without a camera is listed dimmed");
            check(!(list->item(5)->flags() & Qt::ItemIsEnabled),
                  "A scene without a camera cannot be picked");
            check(list->currentItem() && list->currentItem()->text().startsWith("Front"),
                  "The first saved scene is current");
            QTest::keyClick(list, Qt::Key_Return);
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Return);
            checkText(picker.findChild<QLabel *>("presentationPickerStatus")->text(),
                      "Added Entrance", "The picker reports each add");
            QTest::keyClick(&picker, Qt::Key_Escape);
        });
        check(keyframes->count() == 2, "Enter adds a scene and Esc is Done");
        check(steps(doc) == beforePick, "A draft's scenes are not undo steps");
        check(keyframes->item(0)->data(Qt::AccessibleTextRole).toString() ==
                  "Keyframe 1 of 2, Front, has a camera",
              "A keyframe row reads its position and state");
        checkText(status->text(), "2 scenes, 49 frames",
                  "The draft counts two scenes and 49 frames");
        check(createPath->isEnabled(), "Create path is enabled at two scenes");
        checkNamed(*dialog);

        const auto beforeCreate = steps(doc);
        check(tabTo(createPath), "Keyboard reaches Create path");
        QTest::keyClick(focused(), Qt::Key_Space);
        check(doc.cameraPaths().size() == 1, "Create path adds the path");
        check(steps(doc) == beforeCreate + 1, "Create path is one undo step");
        const auto tour = pathNamed(doc, "Path 1");
        check(doc.cameraPaths().at(tour)->keyframes.size() == 2,
              "The path keeps the two picked scenes");
        check(doc.cameraPaths().at(tour)->keyframes[0] == sceneNamed(doc, "Front"),
              "Front plays first");
        check(doc.cameraPaths().at(tour)->timing == CameraPathTiming{},
              "The defaults are 24 fps, 1.00 s and 0.50 s");
        check(!createPath->isVisible() && !cancelDraft->isVisible(),
              "The draft controls go after Create");
        check(paths->currentItem() &&
                  paths->currentItem()->data(Qt::UserRole).toULongLong() == tour,
              "The new path is selected");
        checkText(count->text(), "49 of 240 frames · 2 scenes · 2.0 s at 24 fps",
                  "The live count for two scenes");
        check(transition->value() == 1.0 && hold->value() == 0.5, "The timing shows seconds");
        checkText(dialog->findChild<QLabel *>("cameraPathTransitionSteps")->text(), "= 24 steps",
                  "The transition shows its steps");
        checkText(dialog->findChild<QLabel *>("cameraPathHoldSteps")->text(), "= 12 steps",
                  "The hold shows its steps");

        // Undo removes the path and Redo restores it, and the dialog follows both (§17.2).
        actionNamed(window, "edit.undo")->trigger();
        refocusWidget(paths);
        check(doc.cameraPaths().empty() && paths->count() == 0,
              "Undo removes the path from the dialog");
        actionNamed(window, "edit.redo")->trigger();
        refocusWidget(paths);
        check(doc.cameraPaths().size() == 1 && paths->count() == 1,
              "Redo restores the path in the dialog");

        // Three more scenes from the picker: one undo step each.
        const auto beforeAdds = steps(doc);
        pickScenes(addScene, [](QDialog &picker) {
            auto *list = picker.findChild<QListWidget *>("presentationSceneList");
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Return);
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Return);
            QTest::keyClick(list, Qt::Key_Down);
            QTest::keyClick(list, Qt::Key_Return);
            QTest::keyClick(&picker, Qt::Key_Escape);
        });
        check(steps(doc) == beforeAdds + 3, "Each picked scene is one undo step");
        check(doc.cameraPaths().at(tour)->keyframes.size() == 5, "Five scenes in the path");
        checkText(count->text(), "157 of 240 frames · 5 scenes · 6.5 s at 24 fps",
                  "The live count for the Tour timing");
        checkText(count->text(), "up to 7 scenes fit in 240 frames", "The count says how many fit");
        check(paths->item(0)->data(Qt::AccessibleTextRole).toString() ==
                  "Path 1, 5 scenes, 157 frames",
              "A path row reads its name, scenes and frames");

        // Keyboard reorder (§17.2): Alt+Down moves Front later, Alt+Up moves it back.
        check(tabTo(keyframes), "Keyboard reaches the keyframe list");
        // The picker closed just before: the Alt shortcuts need the window's keyboard focus back.
        check(waitForShortcutFocus(keyframes), "The keyframe list takes keyboard focus");
        keyframes->setCurrentRow(0);
        const auto beforeMove = steps(doc);
        QTest::keyClick(focused(), Qt::Key_Down, Qt::AltModifier);
        check(doc.cameraPaths().at(tour)->keyframes[1] == sceneNamed(doc, "Front"),
              "Alt+Down moves the scene later");
        check(keyframes->currentRow() == 1, "The moved scene keeps the focus");
        check(steps(doc) == beforeMove + 1, "A move is one undo step");
        QTest::keyClick(focused(), Qt::Key_Up, Qt::AltModifier);
        check(doc.cameraPaths().at(tour)->keyframes[0] == sceneNamed(doc, "Front"),
              "Alt+Up moves the scene earlier");

        // Keyboard remove (§17.2): Delete takes out row 3 (Side).
        keyframes->setCurrentRow(2);
        const auto beforeRemove = steps(doc);
        QTest::keyClick(focused(), Qt::Key_Delete);
        check(doc.cameraPaths().at(tour)->keyframes.size() == 4, "Delete removes the keyframe");
        check(doc.cameraPaths().at(tour)->keyframes[2] == sceneNamed(doc, "Garden"),
              "Garden moves up");
        check(steps(doc) == beforeRemove + 1, "A removal is one undo step");

        // Timing from the keyboard (§17.2): at 30 fps the one-second transition stays one second.
        const auto beforeTiming = steps(doc);
        check(tabTo(fps), "Keyboard reaches frames per second");
        typeInto(fps, "30");
        const auto retimed = doc.cameraPaths().at(tour)->timing;
        check(retimed.framesPerSecond == 30 && retimed.transitionSteps == 30 &&
                  retimed.holdSteps == 15,
              "At 30 fps the one-second transition is 30 steps and the half-second hold 15");
        check(steps(doc) == beforeTiming + 1, "A timing change is one undo step");
        checkText(count->text(), "151 of 240 frames · 4 scenes · 5.0 s at 30 fps",
                  "The count follows the new timing");
        actionNamed(window, "edit.undo")->trigger();
        refocusWidget(paths);
        check(doc.cameraPaths().at(tour)->timing.framesPerSecond == 24 && fps->value() == 24,
              "Undo restores the timing and the field shows it");

        // Over the cap while the path is edited: refused, nothing changes, the field goes back
        // (§17.8).
        check(tabTo(hold), "Keyboard reaches the hold");
        const auto beforeRefusal = steps(doc);
        typeInto(hold, "5");
        check(steps(doc) == beforeRefusal, "A refused change adds no undo step");
        checkText(refusal->text(), "Refused", "The refusal is named");
        checkText(refusal->text(), "Nothing was changed", "The refusal says nothing changed");
        checkText(refusal->text(), "the limit is 240", "The refusal gives the limit");
        check(hold->value() == 0.5 && doc.cameraPaths().at(tour)->timing == CameraPathTiming{},
              "The refused hold goes back to 0.50 s");

        // The draft's own limit (§17.2, §17.8): eight scenes plan 265 frames, so Create path is
        // off.
        check(tabTo(newPath), "Keyboard reaches New path");
        QTest::keyClick(focused(), Qt::Key_Space);
        check(pathName->text() == "Path 2", "The next draft is Path 2");
        pickScenes(addScene, [](QDialog &picker) {
            auto *list = picker.findChild<QListWidget *>("presentationSceneList");
            for (const int row : {0, 1, 2, 3, 4, 0, 1, 3}) {
                list->setCurrentRow(row);
                QTest::keyClick(list, Qt::Key_Return);
            }
            QTest::keyClick(&picker, Qt::Key_Escape);
        });
        checkText(count->text(), "Too long", "The draft shows the limit at eight scenes");
        checkText(count->text(), "265 frames; the limit is 240", "The limit is shown in words");
        checkText(count->text(), "7 scenes is the most", "The draft says how many fit");
        checkText(dialog->findChild<QLabel *>("cameraPathLimit")->text(), "1 + 8 x 12 + 7 x 24",
                  "The planner's formula is shown in small type");
        check(!createPath->isEnabled(), "Create path is disabled over the cap");
        checkText(status->text(), "Create path is disabled", "The draft says why");
        check(tabTo(cancelDraft), "Keyboard reaches Cancel");
        QTest::keyClick(focused(), Qt::Key_Space);
        refocusWidget(paths);
        check(!cancelDraft->isVisible() && paths->count() == 1, "Cancel discards the draft");
        check(doc.cameraPaths().size() == 1, "Cancel changes nothing in the model");

        // Names (§17.2): a rename is one undo step; a clash is refused in the engine's words.
        const auto beforeRename = steps(doc);
        typeInto(pathName, "Orbit");
        check(doc.cameraPaths().at(tour)->name == "Orbit", "Renaming the path is one undo step");
        check(steps(doc) == beforeRename + 1, "The rename is one undo step");
        typeInto(pathName, "Orbit");
        check(steps(doc) == beforeRename + 1, "A rename to the same name adds nothing");

        view.editPresentations({QJsonObject{
            {"command", "camera_path.create"},
            {"name", "Garden walk"},
            {"keyframes", QJsonArray{QString::number(sceneNamed(doc, "Front")),
                                     QString::number(sceneNamed(doc, "Roof"))}},
            {"timing",
             QJsonObject{{"framesPerSecond", 24}, {"transitionSteps", 24}, {"holdSteps", 12}}}}});
        check(QTest::qWaitFor([&] { return paths->count() == 2; }, 2000),
              "A second path is listed");
        const auto beforeClash = steps(doc);
        typeInto(pathName, "Garden walk");
        checkText(refusal->text(), "Camera path names must be unique",
                  "A clash uses the engine's words");
        check(steps(doc) == beforeClash, "A refused name adds no undo step");
        check(doc.cameraPaths().at(tour)->name == "Orbit", "The clashing name is not kept");

        // Broken paths (§17.8): a deleted scene is shown by its position and old ID; a scene
        // without a camera is shown as no camera. A broken path changes only to a valid one.
        const auto garden = sceneNamed(doc, "Garden");
        view.editSavedScenes(
            {QJsonObject{{"command", "saved_scene.delete"}, {"scene", QString::number(garden)}}});
        check(QTest::qWaitFor([&] { return paths->count() == 2; }, 2000),
              "Deleting a scene keeps its paths");
        paths->setCurrentRow(0);
        checkText(paths->item(0)->text(), "✕ broken: keyframe 3 names a missing scene",
                  "The list says which keyframe is broken");
        checkText(paths->item(0)->data(Qt::AccessibleTextRole).toString(), "broken",
                  "The list reads broken");
        check(keyframes->count() == 4, "The broken path keeps its four keyframes");
        checkText(keyframes->item(2)->text(), "Missing scene, old ID " + QString::number(garden),
                  "The missing keyframe shows its old ID");
        check(keyframes->item(2)->data(Qt::AccessibleTextRole).toString() ==
                  "Keyframe 3 of 4, scene missing; remove or replace it",
              "A missing keyframe reads its position and what to do");
        // The saved-scene edit moved activation to the main window; the dialog takes it back, as a
        // person would by clicking the list.
        refocusWidget(keyframes);
        keyframes->setCurrentRow(2);
        const auto beforeRepair = steps(doc);
        QTest::keyClick(focused(), Qt::Key_Delete);
        check(doc.cameraPaths().at(tour)->keyframes.size() == 3,
              "Removing the broken keyframe repairs the path");
        check(steps(doc) == beforeRepair + 1, "The repair is one undo step");
        check(!paths->item(0)->text().contains("broken"), "The repaired path is no longer broken");

        view.editSavedScenes(
            {QJsonObject{{"command", "saved_scene.update"},
                         {"scene", QString::number(sceneNamed(doc, "Roof"))},
                         {"snapshot", encodeSceneSnapshot(
                                          view.captureSceneSnapshot(false, true, false, false))}}});
        check(QTest::qWaitFor([&] { return paths->item(0)->text().contains("no camera"); }, 2000),
              "A scene without a camera breaks the path");
        checkText(paths->item(0)->text(), "✕ broken: keyframe 3 has no camera",
                  "The list says which keyframe has no camera");
        checkText(keyframes->item(2)->text(), "no camera", "The keyframe says it has no camera");

        // Presets (§17.3): the second tab, from the keyboard.
        check(tabTo(tabs), "Keyboard reaches the tabs");
        QTest::keyClick(focused(), Qt::Key_Right);
        check(tabs->currentIndex() == 1, "The arrow keys switch to Render presets");
        auto *presetName = dialog->findChild<QLineEdit *>("renderPresetName");
        auto *width = dialog->findChild<QSpinBox *>("renderPresetWidth");
        auto *height = dialog->findChild<QSpinBox *>("renderPresetHeight");
        auto *samples = dialog->findChild<QSpinBox *>("renderPresetSamples");
        auto *seed = dialog->findChild<QSpinBox *>("renderPresetSeed");
        auto *cycles = dialog->findChild<QRadioButton *>("renderPresetCycles");
        auto *eevee = dialog->findChild<QRadioButton *>("renderPresetEevee");
        auto *newPreset = dialog->findChild<QPushButton *>("renderPresetNew");
        auto *duplicate = dialog->findChild<QPushButton *>("renderPresetDuplicate");
        auto *deletePreset = dialog->findChild<QPushButton *>("renderPresetDelete");
        auto *fromCurrent = dialog->findChild<QPushButton *>("renderPresetFromCurrent");
        auto *presetCap = dialog->findChild<QLabel *>("renderPresetCap");
        auto *presetError = dialog->findChild<QLabel *>("renderPresetError");
        check(presetName && width && height && samples && seed && cycles && eevee && newPreset &&
                  duplicate && deletePreset && fromCurrent && presetCap && presetError,
              "Every preset control exists");
        check(tabTo(newPreset), "Keyboard reaches New preset");
        const auto beforePreset = steps(doc);
        QTest::keyClick(focused(), Qt::Key_Space);
        check(doc.renderPresets().size() == 1 && presetName->text() == "Preset 1",
              "New preset makes Preset 1");
        check(steps(doc) == beforePreset + 1, "A new preset is one undo step");
        check(width->value() == 1024 && height->value() == 768 && samples->value() == 32 &&
                  cycles->isChecked(),
              "A new preset starts from the defaults");
        checkNamed(*dialog);

        typeInto(width, "640");
        check(doc.renderPresets().at(presetNamed(doc, "Preset 1"))->settings.width == 640,
              "The width is saved from the keyboard");
        // A radio group is one Tab stop: Tab reaches the checked choice, and the arrow keys choose.
        check(tabTo(cycles), "Keyboard reaches the engine choices");
        QTest::keyClick(focused(), Qt::Key_Right);
        check(eevee->isChecked(), "The arrow keys choose Eevee");
        check(doc.renderPresets().at(presetNamed(doc, "Preset 1"))->settings.engine ==
                  RenderEngine::Eevee,
              "Eevee is saved");
        typeInto(seed, "7");
        check(doc.renderPresets().at(presetNamed(doc, "Preset 1"))->settings.seed == 7,
              "The seed is saved");

        check(tabTo(duplicate), "Keyboard reaches Duplicate");
        const auto beforeDuplicate = steps(doc);
        QTest::keyClick(focused(), Qt::Key_Space);
        check(doc.renderPresets().size() == 2 && hasPreset(doc, "Preset 1 copy"),
              "Duplicate adds a copy with a new name");
        check(steps(doc) == beforeDuplicate + 1, "Duplicate is one undo step");
        check(doc.renderPresets().at(presetNamed(doc, "Preset 1 copy"))->settings ==
                  doc.renderPresets().at(presetNamed(doc, "Preset 1"))->settings,
              "The copy keeps the settings");

        // Rename the selected preset, the copy (§17.3), with a clash refused in the engine's words.
        typeInto(presetName, "Draft");
        check(hasPreset(doc, "Draft") && hasPreset(doc, "Preset 1") &&
                  !hasPreset(doc, "Preset 1 copy"),
              "The rename changes the selected preset only");
        typeInto(presetName, "Preset 1");
        checkText(presetError->text(), "Render preset names must be unique",
                  "A preset clash is refused");
        check(hasPreset(doc, "Draft"), "The refused rename leaves the name");

        // The limit of 16 (§17.3): fourteen more presets make the list full.
        for (int number = 2; number <= 15; ++number)
            view.editPresentations({QJsonObject{{"command", "render_preset.create"},
                                                {"name", QString("Extra %1").arg(number)},
                                                {"width", 640},
                                                {"height", 360},
                                                {"samples", 16},
                                                {"seed", 0},
                                                {"engine", "cycles"}}});
        check(doc.renderPresets().size() == 16, "Sixteen presets");
        check(QTest::qWaitFor([&] { return !newPreset->isEnabled(); }, 2000),
              "At 16, New preset is disabled");
        check(!fromCurrent->isEnabled() && !duplicate->isEnabled(),
              "At 16, the other creators are disabled");
        checkText(presetCap->text(), "16 of 16 presets. A document holds up to 16 presets",
                  "The cap says why");

        // Delete one by keyboard, then New from current settings adds it back (§17.3).
        check(tabTo(presets), "Keyboard reaches the preset list");
        QTest::keyClick(focused(), Qt::Key_End);
        check(tabTo(deletePreset), "Keyboard reaches Delete");
        QTest::keyClick(focused(), Qt::Key_Space);
        check(doc.renderPresets().size() == 15 && newPreset->isEnabled(), "Delete frees a place");
        check(tabTo(fromCurrent), "Keyboard reaches New from current settings");
        const auto beforeCurrent = steps(doc);
        QTest::keyClick(focused(), Qt::Key_Space);
        const auto sheet = window.renderPanel()->setupSettings();
        const auto current = doc.renderPresets().at(presetNamed(doc, "Preset 2"))->settings;
        check(current.width == sheet.width && current.height == sheet.height &&
                  current.samples == sheet.samples && current.engine == sheet.engine,
              "New from current settings copies the render sheet's values");
        check(hasPreset(doc, "Preset 2") && !hasPreset(doc, "Preset 1 copy"),
              "The new preset takes the next free name");
        check(steps(doc) == beforeCurrent + 1, "New from current settings is one undo step");

        // A draft is in the dialog, not the model, so it survives closing and reopening (§17.2).
        tabs->setCurrentIndex(0);
        refocusWidget(newPath);
        QTest::keyClick(focused(), Qt::Key_Space);
        check(paths->count() == 3 && pathName->text() == "Path 1",
              "A draft is open before the dialog closes");
        const auto draftsBefore = steps(doc);
        // Escape closes; the saved paths are still there when the dialog opens again.
        QTest::keyClick(dialog, Qt::Key_Escape);
        check(!dialog->isVisible(), "Escape closes the dialog");
        open->trigger();
        refocusWidget(paths);
        check(dialog->isVisible() && paths->count() == 3 && pathName->text() == "Path 1",
              "Reopening keeps the draft and shows the saved paths");
        check(steps(doc) == draftsBefore && doc.cameraPaths().size() == 2,
              "The draft is not in the model");
        check(tabTo(cancelDraft), "Keyboard reaches Cancel for the reopened draft");
        QTest::keyClick(focused(), Qt::Key_Space);
        refocusWidget(paths);
        check(paths->count() == 2, "Cancel removes the draft after reopening");
        QTest::mouseClick(close, Qt::LeftButton);
        check(!dialog->isVisible(), "Close closes the dialog");

        // Text size (§17.10): the Interface text size setting is read when a window is built, so a
        // window built at 200% shows the dialog at that size. There the list stacks above its
        // editor and the footer stays inside the dialog.
        QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 200);
        {
            Window large;
            large.resize(1280, 900);
            large.show();
            check(QTest::qWaitForWindowExposed(&large), "The 200% window is exposed");
            large.demo();
            large.viewport()->refresh();
            saveScene(*large.viewport(), "Front", true);
            saveScene(*large.viewport(), "Side", true);
            large.viewport()->editPresentations({QJsonObject{
                {"command", "camera_path.create"},
                {"name", "Tour"},
                {"keyframes", QJsonArray{QString::number(sceneNamed(large.document(), "Front")),
                                         QString::number(sceneNamed(large.document(), "Side"))}},
                {"timing", QJsonObject{{"framesPerSecond", 24},
                                       {"transitionSteps", 24},
                                       {"holdSteps", 12}}}}});
            actionNamed(large, "camera.presentations")->trigger();
            auto *big = large.findChild<QDialog *>("presentationsDialog");
            check(big && big->isVisible(), "The dialog opens at 200%");
            big->resize(900, 740);
            auto *bigSplit = big->findChild<QSplitter *>("cameraPathSplit");
            check(bigSplit && QTest::qWaitFor(
                                  [&] { return bigSplit->orientation() == Qt::Vertical; }, 3000),
                  "At 200% the path list stacks above its editor");
            auto *bigClose = big->findChild<QPushButton *>("presentationsClose");
            check(bigClose && bigClose->geometry().bottom() < big->height() &&
                      bigClose->geometry().right() < big->width(),
                  "At 200% the footer's Close stays inside the dialog");
            check(bigClose->isVisibleTo(big), "At 200% Close is shown");
            big->resize(1600, 740);
            check(QTest::qWaitFor([&] { return bigSplit->orientation() == Qt::Horizontal; }, 3000),
                  "A wide dialog puts the list beside its editor again");
            QTest::keyClick(big, Qt::Key_Escape);
            check(!big->isVisible(), "Escape closes the 200% dialog");
        }
        QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 100);
        check(!dialog->isVisible(), "The first dialog stays closed");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Presentations dialog checks pass: menu, create, edit, undo, keyboard, refusals, "
                 "broken paths, presets and text size\n";
    return 0;
}
