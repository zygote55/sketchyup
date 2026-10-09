#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void focus(QWidget *widget) {
    widget->window()->activateWindow();
    check(QTest::qWaitFor(
              [&] { return QGuiApplication::focusWindow() == widget->window()->windowHandle(); }),
          "Import native focus settled");
    widget->setFocus();
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        Document fixture;
        fixture.addFace({{{0, 0, 0}, {2, 0, 0}, {0, 3, 0}}}, "Imported triangle");
        const auto bytes = exportGlb(RenderSnapshot::capture(fixture)).glb;
        const auto source = files.filePath("source.glb");
        QFile copy(source);
        check(copy.open(QIODevice::WriteOnly) && copy.write(bytes) == bytes.size(),
              "Create source copy");
        copy.close();
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}}, "Existing model");
        QMetaObject::invokeMethod(view, "changed");
        const auto old = encodeDocument(doc);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Import window exposed");
        bool cancel = true, handling = false, reportSeen = false;
        int prompts = 0;
        QTimer timer;
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            if (handling)
                return;
            handling = true;
            if (auto *message = window.findChild<QMessageBox *>();
                message && message->isVisible()) {
                focus(message);
                ++prompts;
                message->button(cancel ? QMessageBox::Cancel : QMessageBox::Discard)->click();
            } else if (auto *report = window.findChild<QDialog *>("gltfImportReport");
                       report && report->isVisible()) {
                focus(report);
                reportSeen = true;
                const auto detail =
                    report->findChild<QPlainTextEdit *>("gltfImportDetails")->toPlainText();
                check(
                    detail.contains("Normals are recomputed") &&
                        detail.contains("External edits") &&
                        report->findChild<QLabel *>("gltfImportSummary")->text().contains("Meters"),
                    "Native report explains conversion losses and units");
                const auto capture = qEnvironmentVariable("SKETCHYUP_GLTF_EVIDENCE");
                if (!capture.isEmpty())
                    check(report->grab().save(capture), "Save import report evidence");
                report->reject();
            }
            handling = false;
        });
        timer.start();
        window.importGltfPath(source);
        check(prompts == 1 && !reportSeen && encodeDocument(doc) == old,
              "Canceling replacement preserves current model");
        const auto bad = files.filePath("bad.glb");
        QFile invalid(bad);
        check(invalid.open(QIODevice::WriteOnly) && invalid.write("bad") == 3,
              "Invalid source fixture");
        invalid.close();
        bool rejected = false;
        try {
            window.importGltfPath(bad);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && prompts == 1 && encodeDocument(doc) == old,
              "Corrupt import rejects before discard prompt or mutation");
        cancel = false;
        window.importGltfPath(source);
        timer.stop();
        check(prompts == 2 && reportSeen && doc.dirty() && !doc.instances().empty() &&
                  !doc.scenes().empty(),
              "Native import publishes an unsaved component model with cameras");
        check(copy.open(QIODevice::ReadOnly) && copy.readAll() == bytes,
              "Native import leaves original bytes intact");
        copy.close();
        const auto target = files.filePath("native-copy");
        bool saveSeen = false;
        QTimer saveTimer;
        saveTimer.setInterval(10);
        QObject::connect(&saveTimer, &QTimer::timeout, &window, [&] {
            auto *dialog = window.findChild<QFileDialog *>("saveModelDialog");
            if (!dialog || !dialog->isVisible())
                return;
            saveTimer.stop();
            focus(dialog);
            saveSeen = true;
            auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
            check(name, "Native save filename field available");
            name->setFocus();
            name->selectAll();
            QTest::keyClicks(name, target);
            QMetaObject::invokeMethod(dialog, "accept");
        });
        saveTimer.start();
        window.findChild<QAction *>("file.save")->trigger();
        check(saveSeen && !doc.dirty() && QFile::exists(target + ".sketchyup"),
              "First save after import chooses a native destination");
        check(copy.open(QIODevice::ReadOnly) && copy.readAll() == bytes &&
                  !QFile::exists(source + ".bak"),
              "Native save cannot silently overwrite the imported source");
        copy.close();
        check(encodeDocument(loadDocument(target + ".sketchyup")) == encodeDocument(doc),
              "Native import survives save/reopen");
        auto geometry =
            std::find_if(doc.bodies().begin(), doc.bodies().end(), [](const auto &entry) {
                return entry.second->kind == BodyKind::Geometry &&
                       !entry.second->surface.faces.empty();
            });
        check(geometry != doc.bodies().end(), "Imported geometry is editable");
        const auto id = geometry->first;
        view->enterContext(geometry->second->parent);
        view->selectEntities(
            {{id, SelectionKind::Face, geometry->second->surface.faces.begin()->first}});
        const auto before = doc.bodies().at(id);
        view->paintSelection({.2f, .3f, .4f});
        check(*doc.bodies().at(id) != *before && doc.dirty(),
              "Native commands edit imported component geometry");
        doc.undo();
        check(*doc.bodies().at(id) == *before, "Imported component edits undo normally");
        std::cout << "Native GLB/glTF report, corrupt/cancel preservation, unsaved-copy lifecycle, "
                     "source-safe save, component editing and undo passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
