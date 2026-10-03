#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurfaceFormat>
#include <QTest>
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
        // Window manager may constrain top-level dimensions: nested viewport checks
        // still use actual logical coordinates; report actual window size below.
        window.resize(640, 600);
        QTest::qWait(100);
        check(field->isVisible() &&
                  window.rect().contains(field->mapTo(&window, field->rect().bottomRight())),
              "Measurements remain inside narrow window");
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
