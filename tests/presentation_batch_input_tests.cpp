// R099.d3 (UX_DESIGN §17.4 to §17.8, §17.10, §17.12): the batch dialog, the Jobs rows for batches,
// the results sheet and the quit prompt, driven the way a person drives them. Blender is a fake: a
// launcher script runs blender_job_tests as the worker, so the probe, the frames and the encode
// behave as the worker does. Every file is made in a temporary folder; HOME and the XDG folders
// point there.
#include "app/presentation_batch_dialog.hpp"
#include "app/presentation_batches.hpp"
#include "app/presentation_results.hpp"
#include "app/render_jobs.hpp"
#include "app/render_panel.hpp"
#include "app/surface_format.hpp"
#include "app/viewport.hpp"
#include "app/window.hpp"
#include "core/presentation_records.hpp"
#include "integrations/presentation_store.hpp"
#include "io/scenes_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
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

QWidget *focused() { return QApplication::focusWidget(); }

// Makes `dialog` the active window and gives `first` focus, as the first Tab of a keyboard user
// would find it. Offscreen, nothing gives focus back on its own.
void focusIn(QWidget *dialog, QWidget *first) {
    QApplication::setActiveWindow(dialog);
    dialog->activateWindow();
    first->setFocus(Qt::OtherFocusReason);
    QTest::qWaitFor([&] { return focused() == first; }, 2000);
}

// Presses Tab until `target` has focus, as a keyboard user reaches it.
bool tabTo(QWidget *target, int limit = 160) {
    for (int step = 0; step < limit; ++step) {
        if (focused() == target || target->isAncestorOf(focused()))
            return true;
        QTest::keyClick(focused() ? focused() : target, Qt::Key_Tab);
    }
    return focused() == target || target->isAncestorOf(focused());
}

// Types into a line edit by keyboard: Tab to it, select all, type, then Enter commits the field.
void typeInto(QLineEdit *field, const QString &text) {
    check(tabTo(field), "Keyboard reaches the field");
    QTest::keyClick(focused(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(focused(), text);
}

// Waits for `done`, up to `ms`, and fails with `message` when it does not come.
void waitFor(std::function<bool()> done, int ms, const char *message) {
    check(QTest::qWaitFor(done, ms), message);
}

// Clicks the button named `text` of the message box that is open, from a timer while the box's own
// loop runs. The quit prompt and the unsaved-changes prompt are answered this way.
void answerPrompt(const QString &text) {
    QTimer::singleShot(150, [text] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        for (auto *button : box->buttons())
            if (button->text() == text) {
                button->click();
                return;
            }
    });
}

// A launcher script that runs blender_job_tests as the worker. The probe always answers; a render
// request answers with `renderMode` ("success", or "hang" to keep a frame running until it is
// canceled).
QString launcher(const QTemporaryDir &files, const QString &name, const QString &binary,
                 const QString &renderMode) {
    const auto path = files.filePath(name);
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Launcher fixture");
    // The probe is the request whose operation is "probe"; every other request takes renderMode.
    file.write(("#!/bin/sh\nfor last in \"$@\"; do :; done\nmode=" + renderMode.toUtf8() +
                "\nif grep -q '\"operation\":\"probe\"' \"$last\"; then mode=success; fi\nexec '" +
                binary.toUtf8() + "' --fake \"$mode\" \"$@\"\n"));
    file.close();
    check(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                          QFileDevice::ExeOwner),
          "Launcher is executable");
    return path;
}

void setBlender(const QString &path) {
    QSettings("SketchyUp", "SketchyUp").setValue("render/blenderPath", path);
}

QLabel *label(QWidget &root, const char *name) {
    auto *found = root.findChild<QLabel *>(name);
    check(found, name);
    return found;
}

QPushButton *button(QWidget &root, const char *name) {
    auto *found = root.findChild<QPushButton *>(name);
    check(found, name);
    return found;
}

// Saves the current view as a saved scene, with or without a camera.
void saveScene(Viewport &view, const std::string &name, bool camera) {
    const auto snapshot = view.captureSceneSnapshot(camera, !camera, false, false);
    view.editSavedScenes({QJsonObject{{"command", "saved_scene.create"},
                                      {"name", QString::fromStdString(name)},
                                      {"snapshot", encodeSceneSnapshot(snapshot)}}});
}

Id sceneNamed(const Document &doc, const std::string &name) {
    for (const auto &[id, scene] : doc.scenes())
        if (scene->name == name)
            return id;
    throw std::runtime_error("Scene " + name + " is missing");
}

// A small path and two presets, as the mockup uses (64 px frames keep the fake batch fast).
void makeModel(Window &window) {
    auto &doc = window.document();
    auto &view = *window.viewport();
    window.demo();
    view.refresh();
    view.fit();
    for (const char *name : {"Front", "Entrance", "Side"}) {
        view.fit();
        saveScene(view, name, true);
    }
    saveScene(view, "Section study", false);
    QJsonArray keyframes;
    for (const char *name : {"Front", "Entrance", "Side"})
        keyframes.append(QString::number(qulonglong(sceneNamed(doc, name))));
    view.editPresentations(QJsonArray{QJsonObject{{"command", "camera_path.create"},
                                                  {"name", "Tour"},
                                                  {"keyframes", keyframes},
                                                  {"timing", QJsonObject{{"framesPerSecond", 24},
                                                                         {"transitionSteps", 1},
                                                                         {"holdSteps", 1}}}}},
                           "Create camera path");
    view.editPresentations(QJsonArray{QJsonObject{{"command", "render_preset.create"},
                                                  {"name", "Final"},
                                                  {"width", 64},
                                                  {"height", 64},
                                                  {"samples", 2},
                                                  {"seed", 0},
                                                  {"engine", "cycles"}},
                                      QJsonObject{{"command", "render_preset.create"},
                                                  {"name", "Draft"},
                                                  {"width", 96},
                                                  {"height", 64},
                                                  {"samples", 1},
                                                  {"seed", 0},
                                                  {"engine", "cycles"}},
                                      QJsonObject{{"command", "render_preset.create"},
                                                  {"name", "Preview"},
                                                  {"width", 64},
                                                  {"height", 64},
                                                  {"samples", 1},
                                                  {"seed", 0},
                                                  {"engine", "eevee"}},
                                      QJsonObject{{"command", "render_preset.create"},
                                                  {"name", "Odd"},
                                                  {"width", 65},
                                                  {"height", 64},
                                                  {"samples", 1},
                                                  {"seed", 0},
                                                  {"engine", "cycles"}}},
                           "Create render presets");
}

// The batch dialog's path combo, presets and folder are set by the same keyboard a person uses; the
// dialog is looked up by name, as the audit does.
QDialog *openBatchDialog(Window &window) {
    window.findChild<QAction *>("camera.presentationBatch")->trigger();
    auto *dialog = window.findChild<QDialog *>("presentationBatchDialog");
    check(dialog && dialog->isVisible(), "Export presentation batch… opens the batch dialog");
    return dialog;
}

// The Jobs list, found by name: its batch rows are the top-level rows with the batch role.
QTreeWidget *jobList(Window &window) {
    auto *jobs = window.findChild<QDialog *>("renderJobs");
    check(jobs, "The Jobs list exists");
    return jobs->findChild<QTreeWidget *>("renderJobList");
}

QTreeWidgetItem *batchRow(QTreeWidget *list, const QString &id) {
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        auto *row = list->topLevelItem(i);
        if (row->data(0, Qt::UserRole).toString() == id &&
            row->data(0, Qt::UserRole + 1).toString() == "batch")
            return row;
    }
    return nullptr;
}

// The one batch in this process's store.
StoredPresentationJob onlyJob(PresentationBatches &batches) {
    const auto jobs = batches.jobs();
    check(jobs.size() == 1, "One batch is stored");
    return jobs.front();
}

// The stored batch with this ID.
StoredPresentationJob jobNamed(PresentationBatches &batches, const QString &id) {
    for (const auto &job : batches.jobs())
        if (job.id == id)
            return job;
    throw std::runtime_error("The batch is not stored");
}

// Each window gets its own data folder, so one block's batches are not in another block's store.
void isolateStore(const QTemporaryDir &files, const char *name) {
    qputenv("XDG_DATA_HOME", files.filePath(name).toUtf8());
}

BlenderJob::Options batchOptions(const QString &executable) {
    BlenderJob::Options options;
    options.executable = executable;
    options.timeoutMs = 60000;
    return options;
}

} // namespace

int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("HOME", files.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.setValue("recoverySeconds", 0);
    setBlender(files.path() + "/absent");
    try {
        const auto binary = app.applicationDirPath() + "/blender_job_tests";
        const auto success = launcher(files, "blender-success", binary, "success");
        const auto hanging = launcher(files, "blender-hang", binary, "hang");

        // §17.1 and Ctrl+K: the palette lists the batch dialog, which opens it directly.
        {
            isolateStore(files, "store-1");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Batch window exposed");
            auto *palette = window.findChild<QAction *>("camera.presentationBatch");
            check(palette && palette->text() == "Export presentation batch…" &&
                      palette->property("category").toString() == "camera",
                  "The palette lists Export presentation batch… under Camera");
            makeModel(window);
            setBlender(success);

            // §17.5: the dialog runs the probes when it opens. The encoder check and the Blender
            // line arrive later; until they do, Start waits and Write video is disabled.
            auto *dialog = openBatchDialog(window);
            // Start closes the dialog, which then deletes itself: the test watches it through a
            // pointer.
            QPointer<QDialog> dialogPointer(dialog);
            check(dialog->isModal(), "The batch dialog is modal");
            auto *video = dialog->findChild<QCheckBox *>("batchVideo");
            auto *start = dialog->findChild<QPushButton *>("batchStart");
            check(!video->isEnabled() && !start->isEnabled(), "Start waits for the probe");
            checkText(label(*dialog, "batchVideoState")->text(), "Checking the video encoder",
                      "The probe is in progress");
            waitFor(
                [&] {
                    return label(*dialog, "batchVideoState")->text().contains("available") &&
                           label(*dialog, "batchBlenderValue")->text().contains("ready");
                },
                60000, "The encoder check and the device check arrive");
            checkText(label(*dialog, "batchVideoState")->text(),
                      "✓ H.264 available in Blender 5.2.2. Each video encode stops after 15:00.",
                      "The available state wording");
            checkText(label(*dialog, "batchBlenderValue")->text(),
                      "Blender 5.2.2 · CPU device ready",
                      "The Blender line names the version and the device");
            check(video->isEnabled(), "Write video is enabled once H.264 is available");
            check(!dialog->findChild<QPushButton *>("batchVideoRecheck")->isVisible(),
                  "Check again is not offered when the answer is known");
            // The start line says which rule stops Start: no folder yet.
            checkText(label(*dialog, "batchStartLine")->text(), "Choose a new batch folder.",
                      "Start names the folder rule first");

            // §17.8 refusals, each with its wording, one at a time.
            auto *folder = dialog->findChild<QLineEdit *>("batchFolder");
            folder->setText(files.path());
            checkText(label(*dialog, "batchStartLine")->text(),
                      "Choose a new batch folder that does not exist yet.",
                      "An existing folder is refused");
            folder->setText(files.path() + "/missing-parent/out");
            checkText(label(*dialog, "batchStartLine")->text(),
                      "Choose a new batch folder within an existing directory.",
                      "A folder with no parent is refused");
            folder->setText(files.path() + "/refusals");
            check(start->isEnabled(), "Start is enabled with a valid folder, path and presets");
            // Odd sizes are refused before Start, on the preset's own row (§17.5).
            auto *presets = dialog->findChild<QListWidget *>("batchPresets");
            video->setChecked(true);
            for (int row = 0; row < presets->count(); ++row)
                if (presets->item(row)->text().startsWith("Odd"))
                    presets->item(row)->setCheckState(Qt::Checked);
            checkText(label(*dialog, "batchStartLine")->text(),
                      "A video needs even width and height in every preset: Odd is 65 × 64.",
                      "Odd sizes are refused with video");
            for (int row = 0; row < presets->count(); ++row)
                if (presets->item(row)->text().startsWith("Odd"))
                    presets->item(row)->setCheckState(Qt::Unchecked);
            video->setChecked(false);
            // A preset that cannot use the device is off, with its reason on the row (§17.4).
            dialog->findChild<QComboBox *>("batchBackend")->setCurrentText("OPENGL");
            checkText(label(*dialog, "batchPresetNote")->text(),
                      "Off: Final needs the CPU or a compute device, and the device is OPENGL.",
                      "A Cycles preset is off on the OpenGL device");
            check(!start->isEnabled(),
                  "Start stays disabled while the presets mismatch the device");
            // A new backend asks Blender for its devices again, and Start waits for that answer.
            dialog->findChild<QComboBox *>("batchBackend")->setCurrentText("CPU");
            waitFor(
                [&] {
                    return label(*dialog, "batchBlenderValue")->text().contains("ready") &&
                           start->isEnabled();
                },
                60000, "The device check after a backend change finishes");

            // The live budget (§17.4): frames, the worst-case ceiling, and "Time: not estimated".
            checkText(label(*dialog, "batchFramesValue")->text(), "12",
                      "Two presets of six frames");
            checkText(label(*dialog, "batchTimeDetail")->text(),
                      "Each frame is a separate Blender render.", "Time is not estimated");
            checkText(label(*dialog, "batchOutputDetail")->text(),
                      "Largest possible; real frames are usually smaller.",
                      "The output is the largest possible");

            // Keyboard only (§17.10): Tab reaches Start, and Enter starts the batch.
            focusIn(dialog, dialog->findChild<QComboBox *>("batchPath"));
            check(tabTo(start), "Keyboard reaches Start batch");
            check(focused() == start, "Start has focus");
            QTest::keyClick(focused(), Qt::Key_Return);
            waitFor([&] { return !window.renderPanel()->batches().jobs().empty(); }, 10000,
                    "Enter starts the batch");
            check(!dialogPointer || !dialogPointer->isVisible(), "Start closes the batch dialog");
            const auto started = onlyJob(window.renderPanel()->batches());
            check(started.plan.presets.size() == 2 && started.plan.keyframes.size() == 3,
                  "The batch has the chosen path and presets");
            waitFor(
                [&] {
                    return onlyJob(window.renderPanel()->batches()).state ==
                           PresentationJobState::Completed;
                },
                120000, "The batch completes");

            // §17.6: the Jobs list opened with the batch selected, and its rows say what happened.
            auto *list = jobList(window);
            auto *row = batchRow(list, started.id);
            check(row, "The batch has a parent row");
            checkText(row->text(0), "✓ Completed · 2 presets · 12 frames",
                      "The parent row names the state");
            check(row->childCount() == 2, "One child row per preset");
            checkText(row->child(0)->text(0), "Final · 6 frames finished, no video",
                      "Final's child row");
            checkText(row->child(1)->text(0), "Draft · 6 frames finished, no video",
                      "Draft's child row");
            checkText(row->text(1), "Revision", "The captured-model column names the revision");

            // §17.7: the results sheet is not opened by itself; Show results… opens it.
            check(!window.findChild<QDialog *>("presentationResults"),
                  "Results do not open by themselves");
            list->setCurrentItem(row);
            button(*window.findChild<QDialog *>("renderJobs"), "openStoredRender")->click();
            auto *results = window.findChild<QDialog *>("presentationResults");
            check(results && results->isVisible(), "Show results… opens the results sheet");
            checkText(results->findChild<QLabel *>("resultsBanner")->text(),
                      "✓ Completed. 2 presets, 12 frames, 0 videos. The model is unchanged.",
                      "The banner");
            check(results->findChildren<QPushButton *>("resultStill").size() == 6,
                  "The stills grid has one cell per keyframe and preset");
            check(results->findChildren<QPushButton *>("resultPlayVideo").isEmpty(),
                  "Without video no video is offered");
            button(*results, "resultsClose")->click();

            // §17.6 Remove keeps the output folder, and removes the record and its log.
            list->setCurrentItem(batchRow(list, started.id));
            button(*window.findChild<QDialog *>("renderJobs"), "removeStoredRender")->click();
            check(window.renderPanel()->batches().jobs().empty(), "Remove drops the batch record");
            check(QFileInfo(started.output).isDir() &&
                      QFileInfo(started.output + "/manifest.json").exists(),
                  "Remove keeps the output folder and its frames");
        }

        // §17.7: a batch with video offers Play video for each preset, once the video is written.
        {
            isolateStore(files, "store-2");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Video window exposed");
            makeModel(window);
            setBlender(success);
            auto &batches = window.renderPanel()->batches();
            auto *dialog = openBatchDialog(window);
            waitFor([&] { return label(*dialog, "batchVideoState")->text().contains("available"); },
                    60000, "The encoder check arrives");
            waitFor([&] { return label(*dialog, "batchBlenderValue")->text().contains("ready"); },
                    60000, "The device check arrives");
            dialog->findChild<QLineEdit *>("batchFolder")->setText(files.path() + "/with-video");
            dialog->findChild<QCheckBox *>("batchVideo")->setChecked(true);
            dialog->findChild<QPushButton *>("batchStart")->click();
            waitFor(
                [&] {
                    return !batches.jobs().empty() &&
                           batches.jobs().front().state == PresentationJobState::Completed;
                },
                120000, "The video batch completes");
            const auto job = onlyJob(batches);
            window.renderPanel()->showBatchResults(job.id);
            auto *results = window.findChild<QDialog *>("presentationResults");
            check(results, "The results sheet opens");
            check(results->findChildren<QPushButton *>("resultPlayVideo").size() == 2,
                  "Each preset with a written video offers Play video");
            checkText(results->findChild<QLabel *>("resultsBanner")->text(), "2 videos",
                      "Two videos are counted");
            check(QFileInfo(job.output + "/01-Final/video.mp4").isFile() &&
                      QFileInfo(job.output + "/02-Draft/video.mp4").isFile(),
                  "Each video file is written in its preset's folder");
            results->deleteLater();
        }

        // §17.6 Cancel and Resume, and the announcements: one per milestone, never per frame
        // (§17.10). The batch is started through the controller, so the frames can be made to hang
        // and then be canceled; the Jobs list drives Cancel and Resume.
        {
            isolateStore(files, "store-3");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Cancel window exposed");
            makeModel(window);
            auto &batches = window.renderPanel()->batches();
            QStringList announced;
            QObject::connect(&batches, &PresentationBatches::announced, &window,
                             [&](const QString &text) { announced << text; });
            PresentationPlan plan;
            {
                // The plan of the path and both presets, read from the model as the dialog reads
                // it.
                auto &document = window.document();
                Id path{}, presetFinal{}, presetDraft{};
                for (const auto &[id, record] : document.cameraPaths())
                    if (record->name == "Tour")
                        path = id;
                for (const auto &[id, record] : document.renderPresets()) {
                    if (record->name == "Final")
                        presetFinal = id;
                    if (record->name == "Draft")
                        presetDraft = id;
                }
                plan = batches.planFor(path, {presetFinal, presetDraft}, false,
                                       PresentationVideoCodec::H264);
            }
            const auto output = files.filePath("cancel-resume");
            const auto id = batches.start(plan, output, batchOptions(hanging));
            waitFor([&] { return batches.running(); }, 20000, "The batch is running");
            // A frame is in flight, so the list shows the running row with its progress.
            window.renderPanel()->showJobs();
            auto *jobs = window.findChild<QDialog *>("renderJobs");
            check(jobs, "The Jobs list is open");
            auto *list = jobList(window);
            waitFor([&] { return batchRow(list, id) != nullptr; }, 5000, "The batch has a row");
            list->setCurrentItem(batchRow(list, id));
            check(button(*jobs, "cancelStoredRender")->isEnabled() &&
                      button(*jobs, "cancelStoredRender")->text() == "Cancel batch",
                  "A running batch offers Cancel batch");
            check(!button(*jobs, "retryStoredRender")->isEnabled(),
                  "A running batch cannot Resume");
            check(!button(*jobs, "removeStoredRender")->isEnabled(),
                  "A running batch cannot be removed");
            check(batchRow(list, id)->child(0)->childCount() == 0 &&
                      list->itemWidget(batchRow(list, id)->child(0), 1),
                  "The running preset has a progress bar");
            // Cancel stops the frame in flight and discards it (§17.6); finished frames stay.
            button(*jobs, "cancelStoredRender")->click();
            waitFor([&] { return jobNamed(batches, id).state == PresentationJobState::Canceled; },
                    20000, "Cancel ends the batch");
            checkText(batchRow(list, id)->text(0), "■ Canceled · 0 of 12 kept, no video",
                      "The canceled parent row");
            check(button(*jobs, "retryStoredRender")->isEnabled(), "A canceled batch can Resume");
            check(QFileInfo(output).isDir(), "Cancel keeps the folder");

            // Resume continues at the first unfinished frame, with the same captured model (§17.6).
            setBlender(success);
            list->setCurrentItem(batchRow(list, id));
            button(*jobs, "retryStoredRender")->click();
            waitFor([&] { return jobNamed(batches, id).state == PresentationJobState::Completed; },
                    120000, "Resume completes the batch");
            checkText(batchRow(list, id)->text(0), "✓ Completed", "The resumed batch is complete");
            check(announced.count() >= 3, "Announcements name the start, each preset and the end");
            check(announced.count() <= 8, "Announcements never come once per frame");
            checkText(announced.join("\n"), "Batch started: Tour, 2 presets, 12 frames",
                      "The start is announced");
            checkText(announced.join("\n"), "Frames finished for Final.",
                      "A finished preset is announced by name");
            checkText(announced.join("\n"), "Batch completed: Tour.",
                      "The completion is announced");
            (void)id;
        }

        // §17.12 choice 5: closing with a batch running asks first. Declining keeps the batch and
        // the window; confirming stops the batch, which is left Interrupted and resumable.
        {
            isolateStore(files, "store-4");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Quit window exposed");
            makeModel(window);
            setBlender(success);
            auto &batches = window.renderPanel()->batches();
            PresentationPlan plan;
            {
                auto &document = window.document();
                Id path{}, presetFinal{};
                for (const auto &[id, record] : document.cameraPaths())
                    if (record->name == "Tour")
                        path = id;
                for (const auto &[id, record] : document.renderPresets())
                    if (record->name == "Final")
                        presetFinal = id;
                plan = batches.planFor(path, {presetFinal}, false, PresentationVideoCodec::H264);
            }
            batches.start(plan, files.filePath("quit-keep"), batchOptions(hanging));
            waitFor([&] { return batches.running(); }, 20000, "The batch is running");

            answerPrompt("Keep running");
            window.close();
            check(window.isVisible(), "Declining the prompt keeps the window open");
            check(batches.running(), "Declining the prompt keeps the batch running");

            // The unsaved-changes prompt is answered as well: the model was edited, so Discard
            // closes.
            answerPrompt("Stop batch and close");
            // The stop runs before the unsaved-changes prompt appears, and a fixed delay raced it.
            // So the Discard button is looked for until it is on screen. Its text differs by Qt version
            // ("Discard", or "Close without Saving" on Qt 6.12), so the standard button decides.
            auto *discard = new QTimer(&window);
            discard->setInterval(50);
            QObject::connect(discard, &QTimer::timeout, discard, [discard] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (!box)
                    return;
                for (auto *candidate : box->buttons())
                    if (box->standardButton(candidate) == QMessageBox::Discard) {
                        discard->stop();
                        candidate->click();
                        return;
                    }
            });
            discard->start();
            window.close();
            check(!window.isVisible(), "Confirming the prompt closes the window");
            check(!batches.running(), "The batch is no longer running");
            check(onlyJob(batches).state == PresentationJobState::Interrupted,
                  "The stopped batch is left Interrupted");
            check(!onlyJob(batches).message.isEmpty(), "The interrupted batch says how to resume");
        }

        // UX_DESIGN 17.8: with no Blender, the setup card shows, the controls are greyed and Start
        // says why. The video probe reports that no Blender was found.
        {
            isolateStore(files, "store-5");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Setup window exposed");
            makeModel(window);
            setBlender(files.path() + "/absent");
            auto *dialog = openBatchDialog(window);
            check(label(*dialog, "batchSetupText")->parentWidget()->isVisible(),
                  "The setup card is shown");
            checkText(label(*dialog, "batchSetupText")->text(), "is not an executable file",
                      "The setup card says the configured Blender is not an executable file");
            checkText(label(*dialog, "batchStartLine")->text(),
                      "Presentation batches need an installed Blender 5.2 LTS; install it or "
                      "choose its executable.",
                      "Start names the missing Blender");
            check(!dialog->findChild<QPushButton *>("batchStart")->isEnabled(),
                  "Start is disabled");
            checkText(label(*dialog, "batchVideoState")->text(),
                      "No Blender found, so video cannot be checked",
                      "The video line says no Blender was found");
            check(dialog->findChild<QPushButton *>("batchVideoRecheck")->isVisible(),
                  "Check again is offered for the video");
            dialog->deleteLater();
        }

        // UX_DESIGN 17.12 choice 1: H.264 missing offers MPEG-4 Part 2 only as an explicit choice.
        {
            isolateStore(files, "store-6");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Fallback window exposed");
            makeModel(window);
            const auto noH264 = launcher(files, "blender-no-h264", binary, "success");
            qputenv("SKETCHYUP_FAKE_CODECS", "no-h264");
            setBlender(noH264);
            auto *dialog = openBatchDialog(window);
            waitFor(
                [&] {
                    return label(*dialog, "batchVideoState")->text().contains("unavailable") ||
                           label(*dialog, "batchVideoState")
                               ->text()
                               .contains("not in this Blender");
                },
                60000, "The probe reports H.264 missing");
            checkText(
                label(*dialog, "batchVideoState")->text(),
                "⚠ H.264 is not in this Blender's FFmpeg build. Frames still render; video is off.",
                "The unavailable wording");
            auto *codec = dialog->findChild<QComboBox *>("batchCodec");
            check(codec->count() == 2 && codec->itemText(1).startsWith("MPEG-4 Part 2"),
                  "MPEG-4 Part 2 is offered when H.264 is missing");
            check(!dialog->findChild<QCheckBox *>("batchVideo")->isEnabled(),
                  "Write video is disabled while H.264 is missing");
            codec->setCurrentIndex(1);
            check(dialog->findChild<QCheckBox *>("batchVideo")->isEnabled(),
                  "Choosing MPEG-4 Part 2 enables Write video");
            dialog->deleteLater();
            qunsetenv("SKETCHYUP_FAKE_CODECS");
            setBlender(success);
        }

        // UX_DESIGN 17.10: at 200% text the batch dialog's columns stack, the budget under the
        // form.
        {
            isolateStore(files, "store-7");
            QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 200);
            Window large;
            large.resize(1280, 900);
            large.show();
            check(QTest::qWaitForWindowExposed(&large), "The 200% window is exposed");
            makeModel(large);
            auto *dialog = openBatchDialog(large);
            dialog->resize(1040, 760);
            auto *left = dialog->findChild<QWidget *>("batchLeft");
            auto *right = dialog->findChild<QWidget *>("batchRight");
            check(left && right, "The two columns are named");
            check(right->mapTo(dialog, QPoint(0, 0)).y() >=
                          left->mapTo(dialog, QPoint(0, 0)).y() + left->height() - 2 ||
                      right->mapTo(dialog, QPoint(0, 0)).x() >
                          left->mapTo(dialog, QPoint(0, 0)).x(),
                  "The columns lay out for 200% text");
            check(!dialog->findChild<QPushButton *>("batchStart")->geometry().isEmpty(),
                  "The footer stays inside the dialog at 200%");
            dialog->deleteLater();
            QSettings("SketchyUp", "SketchyUp").remove("interfaceTextPercent");
        }

        // UX_DESIGN 17.10: Esc closes the batch dialog, and no batch starts.
        {
            isolateStore(files, "store-8");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Escape window exposed");
            makeModel(window);
            setBlender(success);
            auto *dialog = openBatchDialog(window);
            QPointer<QDialog> dialogPointer(dialog);
            focusIn(dialog, dialog->findChild<QComboBox *>("batchPath"));
            QTest::keyClick(focused(), Qt::Key_Escape);
            waitFor([&] { return !dialogPointer || !dialogPointer->isVisible(); }, 10000,
                    "Esc closes the batch dialog");
            check(window.renderPanel()->batches().jobs().empty(), "Esc starts no batch");
        }

        // UX_DESIGN 17.6 to 17.8: a batch whose frames finish but whose video is refused ends Failed,
        // with its frames kept. The status chip opens its results, and "Resume: encode video" writes
        // the missing video from the kept frames.
        {
            isolateStore(files, "store-9");
            Window window;
            window.resize(1280, 900);
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Chip window exposed");
            makeModel(window);
            setBlender(success);
            auto &batches = window.renderPanel()->batches();
            const auto options = batchOptions(success);
            batches.videoChecks().refresh(options);
            waitFor([&] { return batches.videoChecks().cached(options).has_value(); }, 60000,
                    "The codec answer is cached before the batch starts");
            PresentationPlan plan;
            {
                auto &document = window.document();
                Id path{}, presetFinal{};
                for (const auto &[id, record] : document.cameraPaths())
                    if (record->name == "Tour")
                        path = id;
                for (const auto &[id, record] : document.renderPresets())
                    if (record->name == "Final")
                        presetFinal = id;
                plan = batches.planFor(path, {presetFinal}, true, PresentationVideoCodec::H264);
            }
            const auto output = files.filePath("chip-resume");
            // The fake encoder refuses H.264 at run time, after the frames have rendered.
            qputenv("SKETCHYUP_FAKE_ENCODE", "codec_unavailable");
            const auto id = batches.start(plan, output, options);
            waitFor([&] { return jobNamed(batches, id).state == PresentationJobState::Failed; },
                    120000, "The refused video fails the batch");
            qunsetenv("SKETCHYUP_FAKE_ENCODE");
            check(QFileInfo(output + "/manifest.json").exists(), "The failed batch keeps its manifest");

            auto *chip = window.findChild<QPushButton *>("renderStatusChip");
            waitFor([&] { return chip && chip->isVisible() && chip->text().contains("Show results"); },
                    5000, "The status chip offers the results");
            chip->click();
            auto *results = window.findChild<QDialog *>("presentationResults");
            check(results && results->isVisible(), "The chip opens the batch's results");
            check(results->findChild<QLabel *>("resultsBanner")->text().contains("Its frames are kept") &&
                      results->findChild<QLabel *>("resultsBanner")->text().contains("Video not written"),
                  "The results say the video was not written and the frames are kept");
            auto *resume = results->findChild<QPushButton *>("resultResumeVideo");
            check(resume && resume->isEnabled() && resume->text() == "Resume: encode video",
                  "The results offer Resume: encode video");
            resume->click();
            waitFor([&] { return jobNamed(batches, id).state == PresentationJobState::Completed; },
                    120000, "Resume: encode video completes the batch");
            QDirIterator videos(output, {"video.mp4"}, QDir::Files, QDirIterator::Subdirectories);
            int written = 0;
            while (videos.hasNext()) {
                videos.next();
                ++written;
            }
            check(written == 1, "Resume writes the one video the refusal left out");
        }

    } catch (const std::exception &error) {
        std::cerr << "presentation_batch_input_tests: " << error.what() << '\n';
        return 1;
    }
    std::cout << "presentation_batch_input_tests: all checks passed\n";
    return 0;
}
