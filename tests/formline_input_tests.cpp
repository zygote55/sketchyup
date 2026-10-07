#include "app/surface_format.hpp"
#include "app/window.hpp"
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
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        QFile fixture("tests/fixtures/formline-v1.formline");
        check(fixture.open(QIODevice::ReadOnly),
              "Open synthetic import fixture from repository root");
        const auto bytes = fixture.readAll();
        const auto source = files.filePath("source.formline");
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
            } else if (auto *report = window.findChild<QDialog *>("formlineImportReport");
                       report && report->isVisible()) {
                focus(report);
                reportSeen = true;
                const auto detail =
                    report->findChild<QPlainTextEdit *>("formlineImportDetails")->toPlainText();
                check(detail.contains("Hidden cylinder") && detail.contains("Depth: 5 m") &&
                          report->findChild<QLabel *>("formlineImportSummary")
                              ->text()
                              .contains("3 objects"),
                      "Native report describes preserved features and conversion notices");
                if (app.arguments().contains("--capture"))
                    report->grab().save("/capture/R037-formline-report.png");
                report->reject();
            }
            handling = false;
        });
        timer.start();
        window.importFormlinePath(source);
        check(prompts == 1 && !reportSeen && encodeDocument(doc) == old,
              "Canceling replacement preserves current model");
        const auto bad = files.filePath("bad.formline");
        QFile invalid(bad);
        check(invalid.open(QIODevice::WriteOnly) && invalid.write("bad") == 3,
              "Invalid source fixture");
        invalid.close();
        bool rejected = false;
        try {
            window.importFormlinePath(bad);
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && prompts == 1 && encodeDocument(doc) == old,
              "Corrupt import rejects before discard prompt or mutation");
        cancel = false;
        window.importFormlinePath(source);
        timer.stop();
        check(prompts == 2 && reportSeen && doc.dirty() && doc.bodies().size() == 4 &&
                  doc.bodies().at(1)->name == "Migration study",
              "Native import publishes an unsaved copy after explicit replacement choice");
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
        view->enterContext(1);
        view->standardView(1);
        view->fit();
        focus(view);
        const auto selected = view->selectionAt(view->project({2.3, -5.2, 5}));
        check(selected && selected->body == 2 && selected->kind == SelectionKind::Face,
              "Imported faces are selectable in their native group");
        view->selectEntities({*selected});
        const auto before = doc.bodies().at(2);
        view->paintSelection({.2f, .3f, .4f});
        check(doc.bodies().at(2) != before && doc.dirty(),
              "Native commands edit imported geometry records");
        doc.undo();
        check(*doc.bodies().at(2) == *before, "Imported record edits undo normally");
        std::cout << "Native Formline report, corrupt/cancel preservation, unsaved-copy lifecycle, "
                     "source-safe save, face selection and undo passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
