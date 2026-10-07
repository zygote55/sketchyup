#include "app/window.hpp"
#include "core/components.hpp"
#include "core/scenes.hpp"
#include "io/library_bundle.hpp"
#include "io/texture_image.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <iostream>
using namespace sketchy;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
QByteArray read(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    try {
        Document source(DisplayUnit::Millimeters);
        const auto body = source.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {0, 1, 0}}});
        const auto component = createComponent(source, body, "Panel");
        SceneSnapshot scene;
        scene.camera = SceneCamera{{1, .5, 0}, 25, 50, 7, 40, true};
        scene.style = source.style();
        scene.style->axesVisible = false;
        const auto defaultScene = createScene(source, "Template view", scene);
        const auto png = encodeTexturePng(TextureImage(2, 1, {255, 0, 0, 255, 0, 255, 0, 255}));
        const auto templatePath = files.filePath("room.sketchylib");
        const auto componentPath = files.filePath("panel.sketchylib");
        const auto templateBytes = encodeTemplateBundle(
            source, {"Metric room", "Default view", {"room"}, defaultScene}, png);
        writeTemplateBundle(templateBytes, templatePath);
        const auto componentBytes = encodeComponentBundle(
            source, component.definition, {"Reusable panel", "A flat panel", {"panel"}}, png);
        writeComponentBundle(componentBytes, componentPath);
        QFile broken(files.filePath("Broken.sketchylib"));
        check(broken.open(QIODevice::WriteOnly), "Malformed library file");
        broken.write("invalid");
        broken.close();
        QSettings("SketchyUp", "SketchyUp").setValue("libraryFolder", files.path());
        Window window;
        window.resize(1180, 850);
        auto &doc = window.document();
        auto &view = *window.viewport();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Library window exposed");
        check(QTest::qWaitFor([&] { return view.rendererReady(); }), "Library renderer ready");
        window.openTemplatePath(templatePath);
        check(doc.identity() != source.identity() && doc.dirty() && !doc.canUndo() &&
                  doc.displayUnits() == DisplayUnit::Millimeters && !doc.style().axesVisible,
              "Template creates fresh unsaved model with default scene and empty history");
        const auto camera = *view.captureSceneSnapshot(true, false, false, false).camera;
        check(camera.orthographic && std::abs(camera.yaw - 25) < 1e-5 &&
                  std::abs(camera.distance - 7) < 1e-5,
              "Saved default camera applied without animation");
        const auto original = encodeContainer(doc);
        QString failure;
        bool cancelPrompt = true, sawPrompt = false, sawLibrary = false, sawInvalid = false;
        bool scheduled = false;
        int mode = 0;
        const auto savedPath = files.filePath("Saved.sketchylib");
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            try {
                if (auto *message = window.findChild<QMessageBox *>();
                    message && message->isVisible()) {
                    sawPrompt = true;
                    message->button(cancelPrompt ? QMessageBox::Cancel : QMessageBox::Discard)
                        ->click();
                } else if (auto *file = window.findChild<QFileDialog *>("librarySaveFileDialog");
                           file && file->isVisible()) {
                    if (file->property("handled").toBool())
                        return;
                    file->setProperty("handled", true);
                    const auto field = file->findChild<QLineEdit *>("fileNameEdit");
                    check(field, "Library filename field");
                    field->setText(savedPath);
                    QMetaObject::invokeMethod(file, "accept", Qt::QueuedConnection);
                } else if (auto *save = window.findChild<QDialog *>("saveLibraryDialog");
                           save && save->isVisible()) {
                    if (mode == 3) {
                        save->reject();
                        return;
                    }
                    if (mode == 4 &&
                        !save->findChild<QLabel *>("saveLibraryError")->text().isEmpty()) {
                        save->reject();
                        return;
                    }
                    if (scheduled)
                        return;
                    scheduled = true;
                    save->findChild<QLineEdit *>("libraryName")->setText("Saved panel");
                    save->findChild<QLineEdit *>("libraryLabels")->setText("Saved, native");
                    const auto buttons = save->findChild<QDialogButtonBox *>();
                    QMetaObject::invokeMethod(buttons->button(QDialogButtonBox::Save), "click",
                                              Qt::QueuedConnection);
                } else if (auto *dialog = window.findChild<QDialog *>("libraryDialog");
                           dialog && dialog->isVisible()) {
                    if (scheduled)
                        return;
                    scheduled = true;
                    sawLibrary = true;
                    dialog->activateWindow();
                    auto *search = dialog->findChild<QLineEdit *>("librarySearch");
                    search->setFocus();
                    QTest::keyClicks(search, mode == 2 ? "Broken" : "reusable panel");
                    auto *list = dialog->findChild<QListWidget *>("libraryEntries");
                    check(list && list->count() == 1, "Keyboard library search filters entries");
                    list->setFocus();
                    QTest::keyClick(list, Qt::Key_Down);
                    QTest::keyClick(list, Qt::Key_Up);
                    const auto buttons = dialog->findChild<QDialogButtonBox *>();
                    if (mode == 2) {
                        sawInvalid =
                            !buttons->button(QDialogButtonBox::Ok)->isEnabled() &&
                            !dialog->findChild<QLabel *>("libraryDetails")->text().isEmpty();
                        dialog->reject();
                    } else {
                        dialog->findChild<QLineEdit *>("libraryPosition")->setText("3000, 0, 0");
                        if (const auto evidence =
                                qEnvironmentVariable("SKETCHYUP_LIBRARY_EVIDENCE");
                            !evidence.isEmpty())
                            check(dialog->grab().save(evidence), "Library screenshot");
                        QTest::keyClick(list, Qt::Key_Return);
                    }
                }
            } catch (const std::exception &error) {
                failure = error.what();
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                    dialog->reject();
            }
        });
        timer.start();
        window.openTemplatePath(templatePath);
        check(failure.isEmpty() && sawPrompt && encodeContainer(doc) == original,
              "Cancel dirty template replacement preserves model");
        mode = 1;
        scheduled = false;
        const auto beforeInsert = doc.history().total;
        window.findChild<QAction *>("component.browseLibrary")->trigger();
        check(failure.isEmpty() && sawLibrary && doc.history().total == beforeInsert + 1,
              "Keyboard library selection inserts one undoable edit");
        const auto placed = view.selectedBody();
        check(placed && std::abs(doc.worldTransform(placed).point({}).x - 3) < 1e-9,
              "Library insertion interprets native document units");
        doc.undo();
        view.refresh();
        check(!doc.bodies().contains(placed), "Native placement undo");
        doc.redo();
        view.refresh();
        check(doc.bodies().contains(placed), "Native placement redo");
        mode = 2;
        scheduled = false;
        window.findChild<QAction *>("component.browseLibrary")->trigger();
        check(failure.isEmpty() && sawInvalid,
              "Malformed library entry reports error and cannot insert");
        mode = 3;
        scheduled = false;
        const auto beforeSave = encodeContainer(doc);
        window.findChild<QAction *>("component.saveLibrary")->trigger();
        check(!QFile::exists(savedPath) && encodeContainer(doc) == beforeSave,
              "Cancel bundle save produces no file");
        mode = 4;
        scheduled = false;
        window.findChild<QAction *>("component.saveLibrary")->trigger();
        check(failure.isEmpty() && QFile::exists(savedPath), "Native component bundle published");
        const auto savedBytes = read(savedPath);
        check(decodeComponentBundle(savedBytes).metadata.name == "Saved panel" &&
                  encodeContainer(doc) == beforeSave,
              "Native save preserves document and metadata");
        scheduled = false;
        window.findChild<QAction *>("component.saveLibrary")->trigger();
        check(read(savedPath) == savedBytes && encodeContainer(doc) == beforeSave,
              "Native bundle save rejects an existing destination");
        timer.stop();
        check(read(templatePath) == templateBytes && read(componentPath) == componentBytes,
              "Native workflows leave original library sources unchanged");
        check(failure.isEmpty(), qPrintable(failure));
        doc.markSaved();
        window.close();
        std::cout << "Native library: templates, default camera, keyboard search/insertion, undo, "
                     "errors, save and source preservation passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
