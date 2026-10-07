#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "integrations/glb_export.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
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
        const QByteArray bytes = "0\nSECTION\n2\nHEADER\n9\n$ACADVER\n1\nAC1032\n9\n$"
                                 "INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLINE\n10"
                                 "\n0\n20\n0\n11\n2000\n21\n0\n0\nARC\n10\n0\n20\n0\n40\n1000\n50\n"
                                 "0\n51\n90\n0\nTEXT\n1\nOmitted note\n0\nENDSEC\n0\nEOF\n";
        const auto source = files.filePath("source.dxf");
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
            } else if (auto *report = window.findChild<QDialog *>("dxfReport");
                       report && report->isVisible()) {
                focus(report);
                reportSeen = true;
                const auto detail =
                    report->findChild<QPlainTextEdit *>("dxfDetails")->toPlainText();
                check(
                    detail.contains("TEXT") && detail.contains("Source bytes remain unchanged") &&
                        report->findChild<QLabel *>("dxfSummary")->text().contains("0.001 meters"),
                    "Native report explains conversion losses and units");
                const auto capture = qEnvironmentVariable("SKETCHYUP_DXF_EVIDENCE");
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
            if (auto *file = window.findChild<QFileDialog *>("dxfImportFileDialog");
                file && file->isVisible()) {
                auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                check(name, "DXF filename field available");
                name->setText(source);
                QMetaObject::invokeMethod(file, "accept");
            } else if (auto *dialog = window.findChild<QDialog *>("dxfOptionsDialog");
                       dialog && dialog->isVisible()) {
                optionsSeen = true;
                check(dialog->findChild<QComboBox *>("dxfUnits")->currentData().toDouble() == 0 &&
                          dialog->findChild<QSpinBox *>("dxfSegments")->value() == 96,
                      "DXF defaults use declared units and bounded curve resolution");
                optionsTimer.stop();
                dialog->reject();
            }
        });
        std::cerr << "DXF stage: cancel options\n";
        optionsTimer.start();
        window.findChild<QAction *>("file.importDxf")->trigger();
        optionsTimer.stop();
        check(optionsSeen && prompts == 0 && encodeDocument(doc) == old,
              "Canceling explicit DXF options preserves current model");
        std::cerr << "DXF stage: cancel replacement\n";
        window.importDxfPath(source, {.001});
        check(prompts == 1 && !reportSeen && encodeDocument(doc) == old,
              "Canceling replacement preserves current model");
        const auto bad = files.filePath("bad.dxf");
        QFile invalid(bad);
        check(invalid.open(QIODevice::WriteOnly) && invalid.write("bad") == 3,
              "Invalid source fixture");
        invalid.close();
        bool rejected = false;
        try {
            window.importDxfPath(bad, {.001});
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected && prompts == 1 && encodeDocument(doc) == old,
              "Corrupt import rejects before discard prompt or mutation");
        cancel = false;
        QTimer choose;
        choose.setInterval(10);
        QObject::connect(&choose, &QTimer::timeout, &window, [&] {
            if (auto *file = window.findChild<QFileDialog *>("dxfImportFileDialog");
                file && file->isVisible()) {
                auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                check(name, "DXF filename field available");
                name->setText(source);
                QMetaObject::invokeMethod(file, "accept");
            } else if (auto *dialog = window.findChild<QDialog *>("dxfOptionsDialog");
                       dialog && dialog->isVisible()) {
                dialog->findChild<QComboBox *>("dxfUnits")->setCurrentIndex(0);
                dialog->findChild<QComboBox *>("dxfUnits")->setCurrentIndex(1);
                choose.stop();
                dialog->accept();
            }
        });
        std::cerr << "DXF stage: accept import options\n";
        choose.start();
        window.findChild<QAction *>("file.importDxf")->trigger();
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
            if (auto *dialog = window.findChild<QDialog *>("dxfOptionsDialog");
                dialog && dialog->isVisible()) {
                dialog->findChild<QComboBox *>("dxfUnits")->setCurrentIndex(0);
                dialog->accept();
            } else if (auto *file = window.findChild<QFileDialog *>("dxfExportFileDialog");
                       file && file->isVisible()) {
                auto *name = file->findChild<QLineEdit *>("fileNameEdit");
                check(name, "DXF package folder field available");
                name->setText(files.filePath("export.dxf"));
                QMetaObject::invokeMethod(file, "accept");
            } else if (auto *dialog = window.findChild<QDialog *>("dxfReport");
                       dialog && dialog->isVisible()) {
                exportSeen = true;
                exportScopeExplained = dialog->findChild<QPlainTextEdit *>("dxfDetails")
                                           ->toPlainText()
                                           .contains("including hidden geometry");
                exportTimer.stop();
                dialog->reject();
            }
        });
        std::cerr << "DXF stage: export report\n";
        exportTimer.start();
        window.findChild<QAction *>("file.exportDxf")->trigger();
        exportTimer.stop();
        check(exportSeen && exportScopeExplained && QFile::exists(files.filePath("export.dxf")) &&
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
        std::cerr << "DXF stage: native save\n";
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
        std::cerr << "DXF stage: edit imported geometry\n";
        auto geometry =
            std::find_if(doc.bodies().begin(), doc.bodies().end(), [](const auto &entry) {
                return entry.second->kind == BodyKind::Geometry &&
                       entry.second->surface.wires.size() > 1;
            });
        check(geometry != doc.bodies().end(), "Imported geometry is editable");
        const auto id = geometry->first;
        view->enterContext(geometry->second->parent);
        view->selectEntities(
            {{id, SelectionKind::Edge, geometry->second->topology.edges.begin()->first}});
        const auto before = doc.bodies().at(id);
        view->deleteSelection();
        check(*doc.bodies().at(id) != *before && doc.dirty(),
              "Native erase edits imported DXF wire geometry");
        doc.undo();
        check(*doc.bodies().at(id) == *before, "Imported DXF edits undo normally");
        std::cout << "Native DXF report, corrupt/cancel preservation, unsaved-copy lifecycle, "
                     "source-safe save, group editing and undo passed; DPR="
                  << view->devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
