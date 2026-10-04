#include "app/window.hpp"
#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QSurfaceFormat>
#include <QTemporaryDir>
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
    QTemporaryDir preferences;
    qputenv("XDG_CONFIG_HOME", preferences.path().toUtf8());
    QApplication app(argc, argv);
    Window window;
    window.show();
    try {
        check(QTest::qWaitForWindowExposed(&window), "Numeric window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window, 5000), "Numeric window active");
        auto *view = window.viewport();
        auto *field = window.findChild<QLineEdit *>("measurements");
        check(field, "Measurements field");
        view->setFocus();
        QTest::keyClick(view, Qt::Key_R);
        QTest::keyClick(view, Qt::Key_BracketLeft);
        check(field->hasFocus() && field->text() == "[",
              "Coordinate key transfers keyboard ownership");
        QTest::keyClicks(field, "0,0,0]");
        QTest::keyClick(field, Qt::Key_Return);
        check(view->hasFocus() && view->operationAnchor() == std::optional<Vec3>{{0, 0, 0}} &&
                  window.document().bodies().empty(),
              "Keyboard-only first coordinate");
        QTest::keyClick(view, Qt::Key_1);
        check(field->hasFocus() && field->text() == "1",
              "Digit input takes precedence over view shortcut during drawing");
        QTest::keyClicks(field, "2'6\",3/4\"");
        QTest::keyClick(field, Qt::Key_Return);
        check(window.document().bodies().size() == 1, "Imperial rectangle committed");
        auto body = window.document().bodies().begin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 3.81 * .01905) <
                  1e-8,
              "Feet and fractional inches dimensions");
        const auto firstId = body->id;
        auto revision = window.document().revision();
        QTest::keyClick(view, Qt::Key_1);
        QTest::keyClicks(field, "m,2m");
        QTest::keyClick(field, Qt::Key_Return);
        check(window.document().bodies().size() == 1 &&
                  window.document().revision() == revision + 1,
              "Numeric re-entry replaces the completed operation");
        body = window.document().bodies().begin()->second;
        check(body->id > firstId &&
                  std::abs(body->surface.area(body->surface.faces.begin()->first) - 2) < 1e-8,
              "Replacement has fresh IDs and revised dimensions");
        view->standardView(2);
        revision = window.document().revision();
        QTest::keyClick(view, Qt::Key_3);
        QTest::keyClicks(field, "m,2m");
        QTest::keyClick(field, Qt::Key_Return);
        check(window.document().revision() == revision + 1,
              "Camera movement does not invalidate amendment");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        check(window.document().bodies().empty() && !window.document().canUndo(),
              "All re-entry revisions share one undo item");
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(window.document().bodies().size() == 1, "Redo restores latest replacement");
        auto unchanged = encodeDocument(window.document());
        QTest::keyClick(view, Qt::Key_4);
        QTest::keyClicks(field, "m,2m");
        QTest::keyClick(field, Qt::Key_Return);
        check(encodeDocument(window.document()) == unchanged && field->hasFocus() &&
                  field->property("invalid").toBool(),
              "Undo/redo invalidates amendment and retains invalid input");
        QTest::keyClick(field, Qt::Key_Escape);
        check(view->hasFocus() && field->text().isEmpty(),
              "Measurements Escape returns viewport focus");
        view->setTool(Viewport::Tool::Rectangle);
        check(view->measurements("[10,0,0]") && view->measurements("2m,2m"),
              "Second keyboard rectangle");
        auto &doc = window.document();
        const auto id = doc.bodies().rbegin()->first;
        executeBatch(doc,
                     {{"apiVersion", 1},
                      {"documentId", QString::fromStdString(doc.identity())},
                      {"expectedRevision", QString::number(doc.revision())},
                      {"commands", QJsonArray{QJsonObject{{"command", "material.color"},
                                                          {"body", QString::number(id)},
                                                          {"color", QJsonArray{.2, .3, .4}}}}}});
        unchanged = encodeDocument(doc);
        check(!view->measurements("3m,3m") && encodeDocument(doc) == unchanged,
              "Intervening command cannot be rolled back by numeric re-entry");
        view->setTool(Viewport::Tool::Rectangle);
        const auto priorLocale = QLocale();
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        check(view->measurements("[20;0;0]") && view->measurements("1,5;2,5"),
              "Comma-decimal keyboard construction");
        body = doc.bodies().rbegin()->second;
        check(std::abs(body->surface.area(body->surface.faces.begin()->first) - 3.75) < 1e-8,
              "Locale dimensions");
        QLocale::setDefault(priorLocale);
        view->setTool(Viewport::Tool::Line);
        check(view->measurements("[0,10,0]") && view->measurements("<2,1,0>"),
              "Relative keyboard line endpoint");
        check(view->measurements("5m"), "Line length re-entry retains previous direction");
        body = doc.bodies().rbegin()->second;
        const auto edge = body->surface.wires[0];
        check(std::abs(
                  length(body->surface.vertices.at(edge[0]) - body->surface.vertices.at(edge[1])) -
                  5) < 1e-7,
              "Revised line length");
        auto *centerArc = window.findChild<QAction *>("tool.10");
        check(centerArc, "Center arc action registered");
        centerArc->trigger();
        check(view->tool() == Viewport::Tool::CenterArc && centerArc->isChecked(),
              "Draw menu selects center arc");
        check(view->measurements("[0,0,0]"), "Center arc origin");
        QTest::keyClick(view, Qt::Key_2);
        QTest::keyClicks(field, "m,-90deg");
        QTest::keyClick(field, Qt::Key_Return);
        check(view->hasFocus() && doc.bodies().rbegin()->second->curves.size() == 1 &&
                  doc.bodies().rbegin()->second->curves.begin()->second.sweepAngle < 0,
              "Measurements commits signed arc angle and returns focus");
        QTest::keyClick(view, Qt::Key_1);
        QTest::keyClicks(field, "2s");
        QTest::keyClick(field, Qt::Key_Return);
        check(doc.bodies().rbegin()->second->curves.begin()->second.segments == 12,
              "Measurements segment re-entry amends arc");
        const auto arcBytes = encodeDocument(doc);
        QTest::keyClick(view, Qt::Key_2);
        QTest::keyClicks(field, "m,360deg");
        QTest::keyClick(field, Qt::Key_Return);
        check(field->hasFocus() && field->property("invalid").toBool() &&
                  encodeDocument(doc) == arcBytes,
              "Invalid arc angle stays in Measurements");
        std::cout << "Keyboard coordinates, units, locale dimensions, amendment guards and "
                     "retained invalid input passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
