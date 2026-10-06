#include "app/styles_panel.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QPushButton>
#include <QSurfaceFormat>
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
    QMetaObject::invokeMethod(window.viewport(), "changed");
    QCoreApplication::processEvents();
}
void click(Window &window, const char *name) {
    auto *button = window.findChild<QPushButton *>(name);
    check(button, "Style action exists");
    button->click();
}
void type(QDialog *dialog, const char *name, const QString &text) {
    auto *field = dialog->findChild<QLineEdit *>(name);
    check(field, "Style input exists");
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, text);
}
void accept(QDialog *dialog) {
    dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
}
void modal(Window &window, const std::function<void(QDialog *)> &operation) {
    bool opened = false;
    std::exception_ptr failure;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("styleDialog");
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
    click(window, "styleCustomize");
    check(opened, "Style editor opened");
    if (failure)
        std::rethrow_exception(failure);
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
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.setDisplayUnits(DisplayUnit::Millimeters);
        doc.markSaved();
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Style window exposed");
        auto *action = window.findChild<QAction *>("view.styles");
        check(action, "View menu has Model styles");
        action->trigger();
        sync(window);
        auto *panel = window.findChild<QWidget *>("stylesPanel");
        check(panel && panel->isVisible(), "Styles tab is visible");
        auto *mode = panel->findChild<QComboBox *>("styleMode");
        check(mode, "Model mode selector exists");
        const auto bodies = doc.bodies();
        const auto saved = doc.saveStamp();
        const auto history = doc.history().total;
        mode->setCurrentIndex(2);
        check(doc.style().mode == ModelStyleMode::Monochrome && doc.bodies() == bodies &&
                  doc.dirty() && doc.history().total == history + 1,
              "Mode change is one saved model edit");
        doc.undo();
        sync(window);
        check(mode->currentIndex() == 0 && !doc.dirty() && doc.isCurrentSnapshot(saved),
              "Undo refreshes native controls and saved state");
        doc.redo();
        sync(window);
        check(mode->currentIndex() == 2, "Redo refreshes native controls");
        auto *profiles = panel->findChild<QCheckBox *>("styleProfiles");
        check(profiles, "Profile toggle exists");
        profiles->click();
        check(doc.style().profiles, "Quick profile toggle updates document");
        auto *ground = panel->findChild<QCheckBox *>("styleGround");
        ground->click();
        check(doc.style().groundVisible, "Quick ground toggle updates document");
        const auto before = encodeContainer(doc);
        const auto beforeHistory = doc.history().total;
        modal(window, [&](QDialog *dialog) {
            type(dialog, "styleProfileWidth", "0");
            accept(dialog);
            check(dialog->isVisible() &&
                      !dialog->findChild<QLabel *>("styleDialogError")->text().isEmpty(),
                  "Invalid width stays open with inline error");
            check(encodeContainer(doc) == before, "Invalid style changes no model bytes");
            type(dialog, "styleProfileWidth", "4.25");
            type(dialog, "styleGroundHeight", "250mm");
            type(dialog, "styleXrayOpacity", "35");
            dialog->findChild<QComboBox *>("styleDialogMode")->setCurrentIndex(4);
            accept(dialog);
        });
        check(doc.style().mode == ModelStyleMode::XRay && doc.style().profileWidth == 4.25 &&
                  doc.style().groundHeight == .25 &&
                  std::abs(doc.style().xrayOpacity - .35) < 1e-12 &&
                  doc.history().total == beforeHistory + 1,
              "Details apply atomically with document units");
        const auto changed = doc.style();
        doc.undo();
        sync(window);
        check(doc.style().profileWidth == 2 && doc.style().groundHeight == 0 &&
                  doc.style().mode == ModelStyleMode::Monochrome,
              "Undo restores complete style");
        doc.redo();
        sync(window);
        check(doc.style() == changed, "Redo restores complete style");
        const auto beforeCancel = encodeContainer(doc);
        modal(window, [&](QDialog *dialog) {
            type(dialog, "styleGroundHeight", "1m");
            dialog->reject();
        });
        check(encodeContainer(doc) == beforeCancel, "Cancel leaves saved presentation unchanged");
        auto precise = doc.style();
        precise.background = {.1234567f, .2345678f, .3456789f};
        precise.profileWidth = 3.12345678912345;
        precise.xrayOpacity = .234567891234567;
        precise.groundHeight = .123456789123456;
        doc.setStyle(precise);
        sync(window);
        const auto exact = encodeContainer(doc);
        modal(window, [&](QDialog *dialog) {
            QTimer chooser;
            chooser.setInterval(10);
            bool shown = false;
            QObject::connect(&chooser, &QTimer::timeout, dialog, [&] {
                auto *colors = dialog->findChild<QColorDialog *>();
                if (!colors || !colors->isVisible())
                    return;
                chooser.stop();
                shown = true;
                colors->accept();
            });
            chooser.start();
            dialog->findChild<QPushButton *>("styleBackgroundColor")->click();
            check(shown, "Unchanged native color chooser opened");
            accept(dialog);
        });
        check(encodeContainer(doc) == exact,
              "Untouched numeric and color acceptance preserves full precision and history");
        modal(window, [&](QDialog *dialog) {
            type(dialog, "styleProfileWidth", "5");
            doc.move(1, {1, 0, 0});
            const auto moved = encodeContainer(doc);
            accept(dialog);
            check(dialog->isVisible() && encodeContainer(doc) == moved,
                  "Stale style dialog cannot overwrite newer document state");
        });
        // Exercise the real color chooser and its cancellation path.
        modal(window, [&](QDialog *dialog) {
            QTimer chooser;
            chooser.setInterval(10);
            bool shown = false;
            QObject::connect(&chooser, &QTimer::timeout, dialog, [&] {
                auto *colors = dialog->findChild<QColorDialog *>();
                if (!colors || !colors->isVisible())
                    return;
                chooser.stop();
                shown = true;
                colors->setCurrentColor(QColor("#17263b"));
                colors->accept();
            });
            chooser.start();
            dialog->findChild<QPushButton *>("styleBackgroundColor")->click();
            check(shown, "Native color chooser opens");
            accept(dialog);
        });
        const auto rgb = doc.style().background;
        check(std::abs(rgb[0] - 23.f / 255) < .0001 && std::abs(rgb[1] - 38.f / 255) < .0001 &&
                  std::abs(rgb[2] - 59.f / 255) < .0001,
              "Chosen background is stored");
        const auto themeStamp = encodeContainer(doc);
        window.findChild<QAction *>("view.theme.2")->trigger();
        sync(window);
        check(encodeContainer(doc) == themeStamp && panel->isVisible(),
              "Dark application theme leaves model style unchanged");
        const auto capture = qEnvironmentVariable("SKETCHYUP_STYLE_EDITOR_EVIDENCE");
        if (!capture.isEmpty())
            modal(window, [&](QDialog *dialog) {
                check(dialog->grab().save(capture), "Raw style editor captured");
                dialog->reject();
            });
        const auto stored = encodeContainer(doc);
        check(decodeContainer(stored).style() == doc.style(),
              "Native authored style reopens exactly");
        click(window, "styleReset");
        check(doc.style() == ModelStyle{}, "Reset restores default model style");
        const auto revision = doc.revision();
        click(window, "styleReset");
        check(doc.revision() == revision, "Repeated reset is a no-op");
        doc.undo();
        sync(window);
        check(doc.style().background == rgb, "Reset is undoable");
        doc.markSaved();
        window.close();
        std::cout << "Native styles, history, precise units, color chooser, cancellation and stale "
                     "edits passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
