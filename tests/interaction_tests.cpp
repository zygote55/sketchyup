#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
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
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    if (!QTest::qWaitForWindowExposed(&window, 5000))
        return 2;
    window.activateWindow();
    if (!QTest::qWaitForWindowActive(&window, 5000)) {
        std::cerr << "Native interaction test requires an active test window\n";
        return 2;
    }
    try {
        auto *view = window.viewport();
        auto *field = window.findChild<QLineEdit *>("measurements");
        check(field, "Measurements field exists");
        QTest::qWait(300);
        check(view->rendererReady(), "Native GL context initialized");
        view->setFocus();
        QCoreApplication::processEvents();
        check(view->hasFocus(), "Viewport owns keyboard focus");
        QTest::keyClick(view, Qt::Key_R);
        check(view->tool() == Viewport::Tool::Rectangle, "Rectangle shortcut");
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({0, 0, 0}).toPoint());
        field->setFocus();
        QTest::keyClicks(field, "invalid");
        QTest::keyClick(field, Qt::Key_Return);
        check(field->text() == "invalid" && field->hasFocus() && window.document().bodies().empty(),
              "Invalid measurements remain editable without mutating model");
        QTest::keyClicks(field, "4, 3");
        QTest::keyClick(field, Qt::Key_Return);
        QTest::qWait(100);
        check(window.document().bodies().size() == 1, "Numeric rectangle committed");
        auto body = window.document().bodies().begin()->second;
        auto face = body->surface.faces.begin()->first;
        check(std::abs(body->surface.area(face) - 12) < 1e-8, "Exact rectangle dimensions");
        QTest::keyClick(view, Qt::Key_P);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({2, 1.5, 0}).toPoint());
        field->setFocus();
        QTest::keyClicks(field, "2");
        QTest::keyClick(field, Qt::Key_Return);
        QTest::qWait(100);
        check(window.document().bodies().begin()->second->surface.faces.size() == 6,
              "Picked face extrusion");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        check(window.document().bodies().begin()->second->surface.faces.size() == 1,
              "Keyboard undo");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(window.document().bodies().begin()->second->surface.faces.size() == 6,
              "Keyboard redo");
        // Editing a text field must not invoke rectangle/circle/camera shortcuts.
        field->setFocus();
        QTest::keyClicks(field, "rco123");
        check(field->text() == "rco123", "Text field owns letter and number keys");
        check(view->tool() == Viewport::Tool::Extrude, "Text field did not change tool");
        field->clear();
        view->setFocus();
        QCoreApplication::processEvents();
        check(view->hasFocus(), "Viewport owns keyboard focus");
        QTest::keyClick(view, Qt::Key_R);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({-2, -2, 0}).toPoint());
        QTest::keyClick(view, Qt::Key_Escape);
        field->setFocus();
        QTest::keyClicks(field, "1, 1");
        QTest::keyClick(field, Qt::Key_Return);
        check(window.document().bodies().size() == 1, "Escape cancels uncommitted rectangle");
        check(field->text() == "1, 1",
              "Canceled preview rejects measurements without discarding text");
        field->clear();
        view->setFocus();
        QCoreApplication::processEvents();
        QTest::keyClick(view, Qt::Key_C);
        QTest::mouseClick(view, Qt::LeftButton, {}, view->project({-3, -3, 0}).toPoint());
        field->setFocus();
        QTest::keyClicks(field, "1");
        QTest::keyClick(field, Qt::Key_Return);
        check(window.document().bodies().size() == 2, "Circle creation");
        auto saved = encodeDocument(window.document());
        QTest::mousePress(view, Qt::MiddleButton, {}, QPoint(100, 100));
        QTest::mouseMove(view, QPoint(160, 130), 50);
        QTest::mouseRelease(view, Qt::MiddleButton, {}, QPoint(160, 130));
        check(encodeDocument(window.document()) == saved,
              "Camera navigation does not mutate document");
        const Vec3 probe{2, 1, 1};
        auto move = [&](QPointF point, Qt::MouseButtons buttons) {
            QMouseEvent event(QEvent::MouseMove, point, view->mapToGlobal(point), Qt::NoButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(view, &event);
        };
        // Release outside the widget must end the implicit grab's navigation.
        QTest::mousePress(view, Qt::MiddleButton, {}, QPoint(100, 100));
        auto beforeDrag = view->project(probe);
        move(QPointF(-80, 150), Qt::MiddleButton);
        check(view->project(probe) != beforeDrag, "Drag continues beyond viewport boundary");
        QTest::mouseRelease(view, Qt::MiddleButton, {}, QPoint(-80, 150));
        auto afterDrag = view->project(probe);
        move(QPointF(100, 100), Qt::NoButton);
        check(view->project(probe) == afterDrag, "Outside release ends navigation");
        for (auto type :
             {QEvent::UngrabMouse, QEvent::WindowDeactivate, QEvent::FocusOut, QEvent::Hide}) {
            QTest::mousePress(view, Qt::MiddleButton, {}, QPoint(100, 100));
            QEvent lost(type);
            QApplication::sendEvent(view, &lost);
            auto before = view->project(probe);
            move(QPointF(200, 200), Qt::MiddleButton);
            check(view->project(probe) == before, "Lost input cancels navigation");
            QTest::mouseRelease(view, Qt::MiddleButton, {}, QPoint(200, 200));
        }
        QTest::mousePress(view, Qt::MiddleButton, {}, QPoint(100, 100));
        auto beforeLostRelease = view->project(probe);
        move(QPointF(200, 200), Qt::NoButton);
        check(view->project(probe) == beforeLostRelease, "Missing button recovers lost release");
        QTest::mouseRelease(view, Qt::MiddleButton, {}, QPoint(200, 200));
        check(encodeDocument(window.document()) == saved, "Input loss never edits geometry");
        const auto revisionBeforeTheme = window.document().revision();
        auto setTheme = [&](const QString &name) {
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == name) {
                    action->trigger();
                    return;
                }
            throw std::runtime_error("Theme action missing");
        };
        setTheme("Dark theme");
        check(window.styleSheet().contains("#202923"), "Dark tokens applied");
        setTheme("Light theme");
        check(window.styleSheet().contains("#f7f7f2"), "Light tokens applied");
        setTheme("System theme");
        check(window.document().revision() == revisionBeforeTheme &&
                  encodeDocument(window.document()) == saved,
              "Theme changes are view-only");
        // Window manager may constrain top-level dimensions: nested viewport checks
        // still use actual logical coordinates; report actual window size below.
        window.resize(640, 600);
        QTest::qWait(100);
        check(field->isVisible() &&
                  window.rect().contains(field->mapTo(&window, field->rect().bottomRight())),
              "Measurements remain inside narrow window");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000),
              "Window active after resize and input-loss checks");
        field->setFocus();
        QCoreApplication::processEvents();
        check(field->hasFocus(), "Measurements owns focus before region traversal");
        QTest::keyClick(field, Qt::Key_F6);
        check(QApplication::focusWidget() == window.findChild<QWidget *>("commandSearch"),
              "F6 wraps from Measurements to command search");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_F6);
        check(QApplication::focusWidget() == window.findChild<QWidget *>("toolRail"),
              "F6 reaches tool rail");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_F6);
        check(view->hasFocus(), "F6 reaches viewport");
        QTest::keyClick(view, Qt::Key_F6, Qt::ShiftModifier);
        check(QApplication::focusWidget() == window.findChild<QWidget *>("toolRail"),
              "Shift F6 reverses region order");
        check(encodeDocument(window.document()) == saved, "Focus navigation never edits model");
        view->setSelection(0);
        check(!window.findChild<QAction *>("edit.move")->isEnabled(), "Move requires selection");
        bool entityResult = false;
        QTimer::singleShot(50, [&] {
            auto *palette = window.findChild<QDialog *>("commandPalette");
            if (!palette)
                return;
            auto *query = palette->findChild<QLineEdit *>("paletteQuery");
            auto *results = palette->findChild<QListWidget *>("paletteResults");
            query->setText(QString("face 1/%1").arg(face));
            entityResult = results->count() == 1;
            if (entityResult)
                QTest::keyClick(query, Qt::Key_Return);
            else
                palette->reject();
        });
        window.findChild<QAction *>("view.commands")->trigger();
        check(entityResult && view->selectedBody() == 1 && view->selectedFace() == face,
              "Palette resolves scoped face identity");
        check(window.findChild<QAction *>("edit.move")->isEnabled(),
              "Selection enables public action");
        check(encodeDocument(window.document()) == saved, "Palette selection never edits model");
        QJsonObject result{{"passed", true},
                           {"platform", QGuiApplication::platformName()},
                           {"scale", window.devicePixelRatioF()},
                           {"width", window.width()},
                           {"height", window.height()},
                           {"checks", "draw, numeric input, picking, extrusion, undo/redo, text "
                                      "focus, cancel, circle, camera, measurements"}};
        std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).toStdString() << '\n';
        window.document().markSaved();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
