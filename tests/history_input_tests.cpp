#include "app/window.hpp"
#include "automation/commands.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>
#include <iostream>
using namespace sketchy;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
bool sameModel(const std::map<Id, BodyPtr> &actual, const std::map<Id, BodyPtr> &expected) {
    if (actual.size() != expected.size())
        return false;
    for (const auto &[id, body] : expected) {
        if (!actual.contains(id))
            return false;
        auto normalized = *actual.at(id);
        if (normalized.surface.nextId < body->surface.nextId ||
            normalized.topology.nextId < body->topology.nextId)
            return false;
        normalized.surface.nextId = body->surface.nextId;
        normalized.topology.nextId = body->topology.nextId;
        if (normalized != *body)
            return false;
    }
    return true;
}
void focus(QWidget *widget) {
    widget->window()->activateWindow();
    check(QTest::qWaitFor(
              [&] { return QGuiApplication::focusWindow() == widget->window()->windowHandle(); }),
          "History focus settled");
    widget->setFocus();
    check(QTest::qWaitFor([&] { return widget->hasFocus(); }),
          "History widget owns keyboard focus");
}
int main(int argc, char **argv) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QTemporaryDir files;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    QApplication app(argc, argv);
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    Window window;
    window.resize(1280, 850);
    auto &doc = window.document();
    auto *view = window.viewport();
    try {
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}}, "Window panel");
        saveDocument(doc, files.filePath("baseline.sketchyup"));
        const auto saved = doc.bodies();
        doc.extrude(1, doc.bodies().at(1)->surface.faces.begin()->first, .2);
        executeBatch(doc,
                     {{"apiVersion", 1},
                      {"documentId", QString::fromStdString(doc.identity())},
                      {"expectedRevision", QString::number(doc.revision())},
                      {"history", QJsonObject{{"label", "Tint & inspect window"},
                                              {"taskId", "window-study"},
                                              {"request", "Tint the window <without moving it>"},
                                              {"assistant", true}}},
                      {"commands", QJsonArray{QJsonObject{{"command", "material.color"},
                                                          {"body", "1"},
                                                          {"color", QJsonArray{.2, .4, .7}}}}}});
        const auto final = doc.bodies();
        QMetaObject::invokeMethod(view, "changed");
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "History window exposed");
        view->fit();
        window.startRecovery(files.filePath("recovery"));
        auto *controller = window.findChild<RecoveryController *>("recoveryController");
        controller->checkpoint();
        check(QTest::qWaitFor([&] { return !controller->busy(); }, 10000),
              "History recovery baseline captured");
        window.findChild<QAction *>("view.history")->trigger();
        auto *tree = window.findChild<QTreeWidget *>("historySteps");
        auto *details = window.findChild<QPlainTextEdit *>("historyDetails");
        check(tree && tree->isVisible() && tree->topLevelItemCount() == 4,
              "History includes baseline and three labeled steps");
        check(tree->topLevelItem(3)->text(0).startsWith("AI · Tint &") &&
                  details->toPlainText().contains("<without moving it>"),
              "Assistant entry and request are displayed as inert plain text");
        auto *undo = window.findChild<QAction *>("edit.undo");
        auto *redo = window.findChild<QAction *>("edit.redo");
        check(undo->text().contains("Tint && inspect") &&
                  undo->toolTip().contains("Tint & inspect"),
              "Menu label names the undo action and escapes mnemonic text");
        if (app.arguments().contains("--capture")) {
            view->repaint();
            QTest::qWait(100);
            window.grab().save("/capture/R039-history-x11.png");
        }
        // Perspective interpolation must not punch holes into an entirely opaque face.
        const auto pixels = view->grabFramebuffer();
        int holes = 0;
        for (int x = 1; x < 40; ++x)
            for (int y = 1; y < 60; ++y) {
                const auto point = view->project({x * .05, y * .05, .2});
                const auto color =
                    pixels.pixelColor(qRound(point.x() * pixels.width() / view->width()),
                                      qRound(point.y() * pixels.height() / view->height()));
                holes += color.blue() <= color.red() * 1.5;
            }
        if (holes)
            std::cerr << "Opaque face background probes: " << holes << '\n';
        check(!holes, "Perspective opaque face has no background holes");
        focus(tree);
        const auto before = doc.revision();
        tree->scrollToItem(tree->topLevelItem(1));
        check(QTest::qWaitFor([&] {
                  return tree->viewport()->rect().contains(
                      tree->visualItemRect(tree->topLevelItem(1)).center());
              }),
              "Saved history row scrolled into view");
        const auto point = tree->visualItemRect(tree->topLevelItem(1)).center();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        check(QTest::qWaitFor([&] { return doc.revision() == before + 2; }) &&
                  sameModel(doc.bodies(), saved) && !doc.dirty() && view->hasFocus(),
              "Clicking history atomically rewinds to saved content and returns keyboard focus to "
              "model");
        check(window.findChild<QLabel *>("recoveryStatus")->text().contains("No unsaved edits"),
              "Undo to saved state does not falsely warn of unprotected edits");
        check(redo->text().contains("Extrude"), "Redo menu follows history cursor");
        redo->trigger();
        check(doc.history().position == 2 && view->hasFocus(),
              "Menu redo advances same history cursor");
        focus(view);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(QTest::qWaitFor([&] { return doc.history().position == 3; }) &&
                  sameModel(doc.bodies(), final),
              "Keyboard redo agrees with menu/history navigation");
        window.findChild<QPushButton *>("historyUndo")->click();
        check(QTest::qWaitFor([&] { return doc.history().position == 2; }),
              "Panel Undo agrees with shared stack");
        window.findChild<QPushButton *>("historyRedo")->click();
        check(QTest::qWaitFor([&] { return doc.history().position == 3; }) &&
                  sameModel(doc.bodies(), final),
              "Panel Redo restores same task result");
        // Queue a stale row activation, then edit before Qt delivers the deferred navigation.
        tree->setCurrentItem(tree->topLevelItem(1));
        QMetaObject::invokeMethod(tree, "itemActivated", Qt::DirectConnection,
                                  Q_ARG(QTreeWidgetItem *, tree->currentItem()), Q_ARG(int, 0));
        doc.move(1, {.5, 0, 0});
        const auto afterMove = doc.bodies();
        check(QTest::qWaitFor(
                  [&] { return !window.findChild<QLabel *>("historyError")->text().isEmpty(); }) &&
                  doc.bodies() == afterMove,
              "Deferred stale history click rejects without undoing newer edits");
        QMetaObject::invokeMethod(view, "changed");
        focus(tree);
        tree->setCurrentItem(tree->topLevelItem(2));
        QTest::keyClick(tree, Qt::Key_Return);
        check(QTest::qWaitFor([&] { return doc.history().position == 2; }) && view->hasFocus(),
              "Keyboard history activation returns focus to model");
        focus(view);
        QTest::keyClick(view, Qt::Key_Z, Qt::ControlModifier);
        check(QTest::qWaitFor([&] { return doc.history().position == 1; }) && !doc.dirty(),
              "Keyboard Undo shares saved-state marker");
        doc.paint(1, {.8f, .2f, .1f});
        QMetaObject::invokeMethod(view, "changed");
        check(!doc.canRedo() && tree->topLevelItemCount() == 3 && !redo->isEnabled(),
              "New edit removes redo rows and updates menu eligibility");
        for (int i = 0; i < 220; ++i)
            doc.paint(1, {float(i % 2), .2f, .3f});
        QMetaObject::invokeMethod(view, "changed");
        check(tree->topLevelItemCount() <= 201 &&
                  window.findChild<QPushButton *>("historyOlder")->isEnabled(),
              "Large history is paged within the native panel");
        const auto unchanged = doc.revision();
        window.findChild<QPushButton *>("historyOlder")->click();
        check(doc.revision() == unchanged &&
                  window.findChild<QPushButton *>("historyNewer")->isEnabled(),
              "History pagination is read-only");
        saveDocument(doc, files.filePath("after-history.sketchyup"));
        QMetaObject::invokeMethod(view, "changed");
        window.findChild<QPushButton *>("historyNewer")->click();
        bool savedMarker = false;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            savedMarker |= tree->topLevelItem(i)->text(1) == "Current · Saved";
        check(savedMarker && !doc.dirty(),
              "Save updates current history marker without a new content revision");
        doc.markSaved();
        std::cout
            << "Native labeled history, assistant requests, saved markers, menu/keyboard/panel "
               "agreement, model focus, stale clicks, redo branching and paging passed; DPR="
            << window.devicePixelRatioF() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        doc.markSaved();
        return 1;
    }
}
