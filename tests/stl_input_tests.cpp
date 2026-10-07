#include "app/window.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSurfaceFormat>
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
    QSurfaceFormat::setDefaultFormat(format);
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
        const QByteArray bytes =
            "solid native\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 2000 0 0\nvertex 0 "
            "3000 0\nendloop\nendfacet\nendsolid native\n";
        const auto source = files.filePath("source.stl");
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
            } else if (auto *report = window.findChild<QDialog *>("stlReport");
                       report && report->isVisible()) {
                focus(report);
                reportSeen = true;
                const auto detail =
                    report->findChild<QPlainTextEdit *>("stlDetails")->toPlainText();
                check(
                    detail.contains("Welding: exact") &&
                        detail.contains("Source bytes remain unchanged") &&
                        report->findChild<QLabel *>("stlSummary")->text().contains("0.001 meters"),
                    "Native report explains conversion losses and units");
                const auto capture = qEnvironmentVariable("SKETCHYUP_STL_EVIDENCE");
                if (!capture.isEmpty())
                    check(report->grab().save(capture), "Save import report evidence");
                report->reject();
            }
            handling = false;
        });
        timer.start();
        bool optionsSeen = false;
        QTimer optionsTimer;
        optionsTimer.setInterval(10);
        QObject::connect(&optionsTimer, &QTimer::timeout, &window, [&] {
            if (auto *file = window.findChild<QFileDialog *>("stlImportFileDialog");
                file && file->isVisible()) {
                auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                check(name, "STL filename field available");
                name->setText(source);
                QMetaObject::invokeMethod(file, "accept");
            } else if (auto *dialog = window.findChild<QDialog *>("stlOptionsDialog");
                       dialog && dialog->isVisible()) {
                optionsSeen = true;
                const auto weld = dialog->findChild<QComboBox *>("stlWeld");
                const auto tolerance = dialog->findChild<QDoubleSpinBox *>("stlTolerance");
                check(weld->currentIndex() == 0 && !tolerance->isEnabled() &&
                          !dialog->findChild<QCheckBox *>("stlDiscardDegenerate")->isChecked(),
                      "No import repair is preselected");
                weld->setCurrentIndex(2);
                check(tolerance->isEnabled(), "Tolerance welding exposes bounded distance");

                optionsTimer.stop();
                dialog->reject();
            }
        });
        std::cerr << "STL stage: cancel options\n";
        optionsTimer.start();
        window.findChild<QAction *>("file.importStl")->trigger();
        optionsTimer.stop();
        check(optionsSeen && prompts == 0 && encodeDocument(doc) == old,
              "Canceling explicit STL options preserves current model");
        std::cerr << "STL stage: cancel replacement\n";
        window.importStlPath(source, {.001, StlUpAxis::Z}, {StlWeld::Exact});
        check(prompts == 1 && !reportSeen && encodeDocument(doc) == old,
              "Canceling replacement preserves current model");
        const auto bad = files.filePath("bad.stl");
        QFile invalid(bad);
        check(invalid.open(QIODevice::WriteOnly) && invalid.write("bad") == 3,
              "Invalid source fixture");
        invalid.close();
        bool rejected = false;
        try {
            window.importStlPath(bad, {.001, StlUpAxis::Z}, {StlWeld::Exact});
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && prompts == 1 && encodeDocument(doc) == old,
              "Corrupt import rejects before discard prompt or mutation");
        cancel = false;
        QTimer choose;
        choose.setInterval(10);
        QObject::connect(&choose, &QTimer::timeout, &window, [&] {
            if (auto *file = window.findChild<QFileDialog *>("stlImportFileDialog");
                file && file->isVisible()) {
                auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                check(name, "STL filename field available");
                name->setText(source);
                QMetaObject::invokeMethod(file, "accept");
            } else if (auto *dialog = window.findChild<QDialog *>("stlOptionsDialog");
                       dialog && dialog->isVisible()) {
                dialog->findChild<QComboBox *>("stlUnits")->setCurrentIndex(0);
                dialog->findChild<QComboBox *>("stlUpAxis")->setCurrentIndex(1);
                dialog->findChild<QComboBox *>("stlWeld")->setCurrentIndex(1);
                choose.stop();
                dialog->accept();
            }
        });
        std::cerr << "STL stage: accept import options\n";
        choose.start();
        window.findChild<QAction *>("file.importStl")->trigger();
        choose.stop();
        timer.stop();
        check(prompts == 2 && reportSeen && doc.dirty() && !doc.bodies().empty(),
              "Native import publishes an unsaved editable model with explicit units");
        check(copy.open(QIODevice::ReadOnly) && copy.readAll() == bytes,
              "Native import leaves original bytes intact");
        copy.close();
        const auto beforeExport = encodeDocument(doc);
        const auto beforeHistory = doc.history().total;
        QTimer exportTimer;
        exportTimer.setInterval(10);
        bool exportSeen = false, exportScopeExplained = false;
        QObject::connect(&exportTimer, &QTimer::timeout, &window, [&] {
            if (auto *dialog = window.findChild<QDialog *>("stlOptionsDialog");
                dialog && dialog->isVisible()) {
                dialog->findChild<QComboBox *>("stlUnits")->setCurrentIndex(0);
                dialog->findChild<QComboBox *>("stlUpAxis")->setCurrentIndex(1);
                dialog->accept();
            } else if (auto *file = window.findChild<QFileDialog *>("stlExportFileDialog");
                       file && file->isVisible()) {
                auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                check(name, "STL package folder field available");
                name->setText(files.filePath("export.stl"));
                QMetaObject::invokeMethod(file, "accept");
            } else if (auto *dialog = window.findChild<QDialog *>("stlReport");
                       dialog && dialog->isVisible()) {
                exportSeen = true;
                exportScopeExplained = dialog->findChild<QPlainTextEdit *>("stlDetails")
                                           ->toPlainText()
                                           .contains("including hidden surfaces");
                exportTimer.stop();
                dialog->reject();
            }
        });
        std::cerr << "STL stage: export report\n";
        exportTimer.start();
        window.findChild<QAction *>("file.exportStl")->trigger();
        exportTimer.stop();
        check(exportSeen && exportScopeExplained && QFile::exists(files.filePath("export.stl")) &&
                  encodeDocument(doc) == beforeExport && doc.history().total == beforeHistory,
              "Native export leaves model and history unchanged");
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
        std::cerr << "STL stage: native save\n";
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
        std::cerr << "STL stage: edit imported geometry\n";
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
              "Native commands edit imported STL geometry");
        doc.undo();
        check(*doc.bodies().at(id) == *before, "Imported STL edits undo normally");
        std::cout << "Native STL report, corrupt/cancel preservation, unsaved-copy lifecycle, "
                     "source-safe save, group editing and undo passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
