#include "app/surface_format.hpp"
#include "app/unit_display.hpp"
#include "app/window.hpp"
#include "automation/measurements.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
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
          "Units window focus settled");
    widget->setFocus();
    check(QTest::qWaitFor([&] { return widget->hasFocus(); }), "Units input owns keyboard focus");
}
void modal(Window &window, const QString &name, const std::function<void()> &open,
           const std::function<void(QDialog *)> &operation) {
    bool handled = false;
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>(name);
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            operation(dialog);
        } catch (...) {
            failure = std::current_exception();
        }
        if (dialog->isVisible())
            dialog->reject();
    });
    timer.start(10);
    open();
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled, "Expected units modal opened");
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}
void enter(Window &window, const QString &text) {
    auto *field = window.findChild<QLineEdit *>("measurements");
    focus(field);
    field->selectAll();
    QTest::keyClicks(field, text);
    QTest::keyClick(field, Qt::Key_Return);
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Units window exposed");
        modal(
            window, "documentUnitsDialog", [&] { window.startUnits(); },
            [&](QDialog *dialog) {
                dialog->findChild<QComboBox *>("documentUnitsChoice")->setCurrentIndex(1);
                accept(dialog);
            });
        check(doc.displayUnits() == DisplayUnit::Millimeters && !doc.dirty() && !doc.canUndo() &&
                  window.findChild<QLabel *>("measurementUnits")->text().contains("mm"),
              "First run creates clean millimeter document and visible unit label");
        window.startUnits(); // A remembered choice opens no second first-run dialog.
        view->setTool(Viewport::Tool::Rectangle);
        enter(window, "[0,0,0]");
        enter(window, "2000,3000");
        check(doc.bodies().size() == 1 && std::abs(doc.worldArea(1, 5) - 6) < tolerance,
              "Bare rectangle dimensions use document millimeters");
        doc.markSaved();
        const auto saved = doc.saveStamp();
        const auto model = doc.bodies();
        auto choose = [&](int index, bool makeDefault = false) {
            modal(
                window, "documentUnitsDialog",
                [&] { window.findChild<QAction *>("file.units")->trigger(); },
                [&](QDialog *dialog) {
                    dialog->findChild<QComboBox *>("documentUnitsChoice")->setCurrentIndex(index);
                    dialog->findChild<QCheckBox *>("documentUnitsDefault")->setChecked(makeDefault);
                    accept(dialog);
                });
        };
        choose(2);
        check(doc.displayUnits() == DisplayUnit::FeetInches && doc.bodies() == model && doc.dirty(),
              "Existing model units change without resizing");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.displayUnits() == DisplayUnit::Millimeters && doc.isCurrentSnapshot(saved) &&
                  !doc.dirty(),
              "Undo unit choice restores saved model and footer");
        window.findChild<QAction *>("edit.redo")->trigger();
        view->setSelection(1);
        view->setTool(Viewport::Tool::Move);
        enter(window, "[0,0,0]");
        enter(window, "2,0,0");
        check(std::abs(doc.worldTransform(1).point(doc.bodies().at(1)->surface.vertices.at(1)).x -
                       .6096) < tolerance,
              "Bare feet displacement uses feet, not meters or inches");
        enter(window, "1000mm,0,0");
        check(std::abs(doc.worldTransform(1).point(doc.bodies().at(1)->surface.vertices.at(1)).x -
                       1) < tolerance,
              "Explicit suffix overrides document units during amendment");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Scale);
        view->setSelection(1);
        check(view->measurements("[0,0,0]") && view->measurements("2"),
              "Unitless scale remains available");
        check(std::abs(doc.worldArea(1, 5) - 24) < tolerance,
              "Scale factor stays dimensionless in imperial document");
        doc.undo();
        view->refresh();
        view->setTool(Viewport::Tool::Rotate);
        view->setSelection(1);
        check(view->measurements("[0,0,0]") && view->measurements("90"),
              "Angle default remains degrees");
        check(length(doc.worldTransform(1).point(doc.bodies().at(1)->surface.vertices.at(2)) -
                     Vec3{0, 2, 0}) < tolerance,
              "Document units do not alter angle interpretation");
        doc.undo();
        view->refresh();
        view->setSelection(1);
        QMetaObject::invokeMethod(view, "changed");
        // The real compositor may tile this window below the automatic tray threshold.
        window.findChild<QAction *>("view.history")->trigger();
        window.findChild<QTabWidget *>("organizationTabs")->setCurrentIndex(2);
        check(QTest::qWaitFor(
                  [&] { return window.findChild<QLabel *>("entityInfoDimensions")->isVisible(); }),
              "Info tray is visible on the compositor-selected window size");
        check(window.findChild<QLabel *>("entityInfoDimensions")->text().contains("'") &&
                  window.findChild<QLabel *>("entityInfoArea")->text().contains("ft²"),
              "Info uses preferred lengths and area units");
        modal(
            window, "entityInfoDialog",
            [&] { window.findChild<QPushButton *>("entityInfoEdit")->click(); },
            [&](QDialog *dialog) {
                auto *field = dialog->findChild<QLineEdit *>("entityEditPosition0");
                check(field->text().contains("'"), "Entity editor formats preferred units");
                focus(field);
                field->selectAll();
                QTest::keyClicks(field, "2");
                accept(dialog);
            });
        check(std::abs(doc.worldTransform(1).point({}).x - .6096) < tolerance,
              "Entity editor interprets bare values in preferred units");
        saveDocument(doc, files.filePath("imperial.sketchyup"));
        choose(0);
        doc.markSaved();
        window.openPath(files.filePath("imperial.sketchyup"));
        check(doc.displayUnits() == DisplayUnit::FeetInches &&
                  window.findChild<QLabel *>("measurementUnits")->text().contains("ft-in"),
              "Reopening chooses stored document units over application defaults");
        // R084.r: keyboard-only display precision change updates Info readouts.
        view->setSelection(1);
        QMetaObject::invokeMethod(view, "changed");
        auto *infoDimensions = window.findChild<QLabel *>("entityInfoDimensions");
        auto *infoArea = window.findChild<QLabel *>("entityInfoArea");
        check(QTest::qWaitFor([&] { return infoDimensions->text().contains("'"); }),
              "Info shows the reopened selection");
        const auto fullDimensions = infoDimensions->text();
        const auto area = doc.worldArea(1, 5);
        check(infoArea->text() == displayMeasure(area, 2, DisplayUnit::FeetInches,
                                                 fullDisplayPrecision),
              "Full precision keeps the historical area readout");
        const auto beforePrecision = doc.saveStamp();
        modal(
            window, "documentUnitsDialog",
            [&] { window.findChild<QAction *>("file.units")->trigger(); },
            [&](QDialog *dialog) {
                auto *units = dialog->findChild<QComboBox *>("documentUnitsChoice");
                auto *precision = dialog->findChild<QComboBox *>("documentPrecisionChoice");
                check(precision && precision->isVisible() &&
                          precision->accessibleName() == "Display precision" &&
                          precision->count() == 5 && precision->currentIndex() == 0 &&
                          precision->itemText(0) == "Full (4' 0.605031\")" &&
                          precision->itemText(2) == "4' 0.6\"",
                      "Precision choices show real ft-in samples with Full selected");
                units->setCurrentIndex(0);
                check(precision->count() == 8 && precision->currentIndex() == 0 &&
                          precision->itemText(3) == "1.23 m",
                      "Changing units repopulates samples and selects Full");
                units->setCurrentIndex(2);
                focus(units);
                QTest::keyClick(units, Qt::Key_Tab);
                check(QTest::qWaitFor([&] { return precision->hasFocus(); }),
                      "Precision follows units in tab order");
                QTest::keyClick(precision, Qt::Key_Down);
                QTest::keyClick(precision, Qt::Key_Down);
                check(precision->currentData().toInt() == 1, "Keyboard selects one decimal");
                QTest::keyClick(precision, Qt::Key_Return);
                check(QTest::qWaitFor([&] { return !dialog->isVisible(); }),
                      "Return accepts the units dialog");
            });
        check(doc.displayUnits() == DisplayUnit::FeetInches && doc.displayPrecision() == 1 &&
                  doc.dirty() && doc.history().entries.back().label == "Change display precision",
              "Dialog applies one labeled precision edit");
        check(QTest::qWaitFor([&] { return infoDimensions->text() != fullDimensions; }) &&
                  infoArea->text() == displayMeasure(area, 2, DisplayUnit::FeetInches, 1) &&
                  infoArea->text().endsWith(" ft²") &&
                  infoDimensions->text().contains(QRegularExpression("^\\d+' \\d+\\.\\d\"")),
              "Info lengths and area follow display precision");
        window.findChild<QAction *>("edit.undo")->trigger();
        check(doc.displayPrecision() == fullDisplayPrecision &&
                  doc.isCurrentSnapshot(beforePrecision) &&
                  QTest::qWaitFor([&] { return infoDimensions->text() == fullDimensions; }),
              "Undo restores Full precision readouts");
        if (app.arguments().contains("--capture"))
            window.grab().save("/capture/R084r-precision.png");
        view->setTool(Viewport::Tool::Select);
        view->setSelection(1);
        QMetaObject::invokeMethod(view, "changed");
        QTest::qWait(50);
        if (app.arguments().contains("--capture"))
            window.grab().save("/capture/R039-units-x11.png");
        const auto opened = doc.saveStamp();
        QSettings("SketchyUp", "SketchyUp").remove("defaultUnits");
        modal(
            window, "documentUnitsDialog", [&] { window.startUnits(); },
            [&](QDialog *dialog) {
                dialog->findChild<QComboBox *>("documentUnitsChoice")->setCurrentIndex(1);
                accept(dialog);
            });
        check(doc.isCurrentSnapshot(opened) && doc.displayUnits() == DisplayUnit::FeetInches,
              "First-run default choice never changes an already opened model");
        window.findChild<QAction *>("file.new")->trigger();
        check(doc.displayUnits() == DisplayUnit::Millimeters && !doc.dirty(),
              "New document uses remembered default independently of opened model");
        for (auto unit : {DisplayUnit::Meters, DisplayUnit::Millimeters, DisplayUnit::FeetInches})
            for (double length : {0., -.00001, -1.25, .3048, .609599999999, 1000000.})
                check(
                    std::abs(parseLength(displayLength(length, unit, fullDisplayPrecision), inputUnit(unit), QLocale()) -
                             length) < tolerance,
                    "Formatted length parses back within modeling tolerance");
        const auto originalLocale = QLocale();
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        for (auto unit : {DisplayUnit::Meters, DisplayUnit::Millimeters, DisplayUnit::FeetInches})
            check(
                std::abs(parseLength(displayLength(-1234.56789, unit, fullDisplayPrecision), inputUnit(unit), QLocale()) +
                         1234.56789) < tolerance,
                "Localized display omits grouping and retains parseable decimal units");
        QLocale::setDefault(originalLocale);
        view->setTool(Viewport::Tool::Tape);
        view->setGuideCreation(false);
        QString measured;
        const auto connection = QObject::connect(
            view, &Viewport::message, [&](const QString &message) { measured = message; });
        check(view->measurements("[0,0,0]") && view->measurements("[3000,0,0]") &&
                  measured.contains("3000 mm"),
              "Tape reports measured lengths in document units");
        QObject::disconnect(connection);
        doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        view->setSelection(1);
        view->makeComponent("Unit panel");
        modal(
            window, "componentDialog",
            [&] { window.findChild<QAction *>("component.instance")->trigger(); },
            [&](QDialog *dialog) {
                dialog->findChild<QLineEdit *>("componentOrigin")->setText("3000,0,0");
                accept(dialog);
            });
        check(doc.instances().size() == 2 &&
                  std::abs(doc.worldTransform(view->selectedBody()).point({}).x - 3) < tolerance,
              "Component placement interprets bare coordinates in preferred units");
        const auto beforeCancel = doc.saveStamp();
        modal(
            window, "documentUnitsDialog",
            [&] { window.findChild<QAction *>("file.units")->trigger(); },
            [&](QDialog *dialog) {
                dialog->findChild<QComboBox *>("documentUnitsChoice")->setCurrentIndex(2);
                dialog->reject();
            });
        check(doc.isCurrentSnapshot(beforeCancel), "Cancel preserves document units and state");
        modal(
            window, "documentUnitsDialog",
            [&] { window.findChild<QAction *>("file.units")->trigger(); },
            [&](QDialog *dialog) {
                doc.setDisplayUnits(DisplayUnit::Meters);
                dialog->findChild<QComboBox *>("documentUnitsChoice")->setCurrentIndex(2);
                accept(dialog);
                check(dialog->isVisible() &&
                          !dialog->findChild<QLabel *>("documentUnitsError")->text().isEmpty(),
                      "Stale units dialog retains choices with an inline error");
            });
        check(doc.displayUnits() == DisplayUnit::Meters,
              "Stale dialog preserves intervening preference edit");
        doc.markSaved();
        std::cout << "Native first-run/default/document units, numeric tools, Info, undo/reopen, "
                     "display precision, cancellation and stale guards passed; DPR="
                  << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        doc.markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
