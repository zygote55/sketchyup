#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <iostream>
using namespace sketchy;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QAction *action(Window &window, const char *id) {
    auto *result = window.findChild<QAction *>(id);
    check(result, "Shortcut action exists");
    return result;
}
void click(QDialog &dialog, const char *name) {
    auto *button = dialog.findChild<QPushButton *>(name);
    check(button && button->isEnabled(), "Shortcut operation available");
    button->setFocus();
    check(QTest::qWaitFor([&] { return button->hasFocus(); }), "Shortcut button has focus");
    QSignalSpy clicked(button, &QPushButton::clicked);
    QTest::keyClick(button, Qt::Key_Space);
    check(QTest::qWaitFor([&] { return !clicked.isEmpty(); }),
          "Keyboard activates shortcut operation");
}
void choose(QDialog &dialog, const char *id, const char *sequence) {
    auto *list = dialog.findChild<QTreeWidget *>("shortcutActions");
    check(list, "Command list exists");
    QTreeWidgetItem *row{};
    for (int i = 0; i < list->topLevelItemCount(); ++i)
        if (list->topLevelItem(i)->data(0, Qt::UserRole).toString() == id)
            row = list->topLevelItem(i);
    check(row, "Requested command appears in editor");
    list->setCurrentItem(row);
    dialog.findChild<QKeySequenceEdit *>("shortcutSequence")
        ->setKeySequence(QKeySequence(sequence));
}
void save(QDialog &dialog) {
    for (auto *box : dialog.findChildren<QDialogButtonBox *>())
        if (auto *button = box->button(QDialogButtonBox::Save)) {
            button->setFocus();
            check(QTest::qWaitFor([&] { return button->hasFocus(); }), "Save button has focus");
            QSignalSpy clicked(button, &QPushButton::clicked);
            QTest::keyClick(button, Qt::Key_Space);
            check(QTest::qWaitFor([&] { return !clicked.isEmpty(); }), "Keyboard activates Save");
            return;
        }
    check(false, "Save button exists");
}
QString error(QDialog &dialog) { return dialog.findChild<QLabel *>("shortcutError")->text(); }
template <class F> void editor(Window &window, F exercise) {
    bool handled = false;
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("shortcutDialog");
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            check(QTest::qWaitForWindowActive(dialog), "Shortcut dialog activated");
            exercise(*dialog);
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    timer.start(10);
    action(window, "edit.shortcuts")->trigger();
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled, "Native shortcut editor opened");
    check(QTest::qWaitForWindowActive(&window) &&
              QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
          "Shortcut editor restores parent activation and modeling focus");
}
void focusedKey(Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    check(QTest::qWaitFor([] { return QApplication::focusWidget() != nullptr; }),
          "Shortcut keyboard task has a focus owner");
    QTest::keyClick(QApplication::focusWidget(), code, modifiers);
    QCoreApplication::processEvents();
}
void tabTo(QWidget *target) {
    check(target && target->isVisible() && target->isEnabled(), "Keyboard destination available");
    auto reached = [&] {
        return target->hasFocus() || target->isAncestorOf(QApplication::focusWidget());
    };
    for (int i = 0; !reached() && i < 24; ++i)
        focusedKey(Qt::Key_Tab);
    check(reached(), "Tab reaches shortcut control without assigning focus");
}
void keyboardShortcutTask(Window &window) {
    window.activateWindow();
    check(QTest::qWaitForWindowActive(&window), "Keyboard shortcut window activated");
    // Only initial focus is setup. Dialog discovery, navigation and changes use keys.
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Initial keyboard focus");
    const auto content = encodeContainer(window.document());
    for (bool reset : {false, true}) {
        bool searched{}, edited{};
        std::exception_ptr failure;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || failure)
                return;
            try {
                if (!searched && dialog->objectName() == "commandPalette") {
                    searched = true;
                    auto *query = dialog->findChild<QLineEdit *>("paletteQuery");
                    check(query && QTest::qWaitFor([&] { return query->hasFocus(); }),
                          "Palette search initially focused");
                    QTest::keyClicks(query, "Keyboard shortcuts");
                    auto *results = dialog->findChild<QListWidget *>("paletteResults");
                    check(results && results->count() == 1, "Shortcut command is discoverable");
                    focusedKey(Qt::Key_Return);
                } else if (!edited && dialog->objectName() == "shortcutDialog") {
                    edited = true;
                    // Visibility can precede the compositor's focus handoff from
                    // the palette. Deliver keys only after this dialog owns focus.
                    check(QTest::qWaitForWindowActive(dialog),
                          "Keyboard shortcut dialog activated");
                    check(QTest::qWaitFor(
                              [&] { return dialog->isAncestorOf(QApplication::focusWidget()); }),
                          "Shortcut dialog owns keyboard focus before navigation");
                    if (reset) {
                        tabTo(dialog->findChild<QPushButton *>("shortcutReset"));
                        focusedKey(Qt::Key_Space);
                    } else {
                        auto *search = dialog->findChild<QLineEdit *>("shortcutSearch");
                        check(search && QTest::qWaitFor([&] { return search->hasFocus(); }),
                              "Editor search initially focused");
                        QTest::keyClicks(search, "Rectangle");
                        auto *list = dialog->findChild<QTreeWidget *>("shortcutActions");
                        tabTo(list);
                        focusedKey(Qt::Key_Home);
                        if (!list->currentItem() ||
                            list->currentItem()->data(0, Qt::UserRole).toString() != "tool.1")
                            throw std::runtime_error(
                                QString(
                                    "Keyboard Rectangle selection: query=%1 current=%2 focus=%3")
                                    .arg(search->text(),
                                         list->currentItem() ? list->currentItem()->text(0)
                                                             : "none",
                                         QApplication::focusWidget()->objectName())
                                    .toStdString());
                        auto *sequence = dialog->findChild<QKeySequenceEdit *>("shortcutSequence");
                        tabTo(sequence);
                        focusedKey(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
                        focusedKey(Qt::Key_Tab);
                        check(sequence->keySequence() == QKeySequence("Ctrl+Alt+R"),
                              "Actual key press records the replacement shortcut");
                        tabTo(dialog->findChild<QPushButton *>("shortcutAssign"));
                        focusedKey(Qt::Key_Space);
                        check(error(*dialog).isEmpty(), "Keyboard assignment succeeds");
                    }
                    QPushButton *saveButton{};
                    for (auto *box : dialog->findChildren<QDialogButtonBox *>())
                        if (auto *button = box->button(QDialogButtonBox::Save))
                            saveButton = button;
                    tabTo(saveButton);
                    focusedKey(Qt::Key_Space);
                    check(!dialog->isVisible(), "Keyboard Save closes the editor");
                }
            } catch (...) {
                failure = std::current_exception();
                dialog->reject();
            }
        });
        timer.start(10);
        focusedKey(Qt::Key_K, Qt::ControlModifier);
        timer.stop();
        if (failure)
            std::rethrow_exception(failure);
        check(searched && edited && QTest::qWaitForWindowActive(&window) &&
                  QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
              "Keyboard shortcut workflow returns modeling focus");
        check(action(window, "tool.1")->shortcut() == QKeySequence(reset ? "R" : "Ctrl+Alt+R"),
              "Keyboard Save applies assignment or reset");
        if (!reset) {
            focusedKey(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
            check(window.viewport()->tool() == Viewport::Tool::Rectangle,
                  "Keyboard-configured shortcut activates Rectangle");
        }
    }
    check(encodeContainer(window.document()) == content, "Shortcut task preserves the model");
}
} // namespace
int main(int argc, char **argv) {
    QTemporaryDir files;
    if (!files.isValid())
        return 2;
    qputenv("XDG_CONFIG_HOME", files.path().toUtf8());
    qputenv("XDG_DATA_HOME", files.path().toUtf8());
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    sketchy::setDefaultViewportFormat(format);
    QApplication app(argc, argv);
    app.setOrganizationName("SketchyUp");
    app.setApplicationName("SketchyUp");
    try {
        QSettings settings;
        // Private UI-only configuration enables the composer; no prompt is submitted.
        settings.setValue("assistant/provider", "Ollama");
        settings.setValue("assistant/localModel", "unused-shortcut-fixture");
        const QByteArray unknown("untouched\0bytes", 15);
        settings.setValue("futureNamespace/data", unknown);
        settings.setValue("shortcuts/v1",
                          QByteArray("{\"apiVersion\":1,\"bindings\":{\"future.command\":\"future "
                                     "syntax\"},\"future\":42}"));
        settings.sync();
        {
            Window window;
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Window exposed");
            const auto content = encodeContainer(window.document());
            for (auto *entry : window.findChildren<QAction *>()) {
                const auto declared = entry->property("defaultShortcut");
                if (declared.isValid())
                    check(entry->shortcut() == declared.value<QKeySequence>(),
                          "Fresh preferences retain every default public shortcut");
            }
            check(action(window, "view.tray")->shortcut() == QKeySequence("Ctrl+Shift+T") &&
                      action(window, "textCreate")->shortcut() == QKeySequence("Ctrl+Alt+Shift+T"),
                  "Model-panel toggle and local text creation have separate defaults");
            keyboardShortcutTask(window);
            // R094.d (ADR 0166 §8): Add driver is Shift+D in the Model scope, a key no other
            // default uses; it works on the viewport and never consumes typing in text fields.
            auto *addDriver = action(window, "component.add_driver");
            check(addDriver->property("defaultShortcut").value<QKeySequence>() ==
                          QKeySequence("Shift+D") &&
                      addDriver->property("shortcutScope").toInt() == int(ShortcutScope::Model) &&
                      addDriver->shortcutContext() == Qt::WidgetShortcut,
                  "Add driver defaults to Shift+D on the viewport in the Model scope");
            for (auto *entry : window.findChildren<QAction *>()) {
                const auto declared = entry->property("defaultShortcut");
                if (entry != addDriver && declared.isValid())
                    if (!declared.value<QKeySequence>().isEmpty() &&
                        declared.value<QKeySequence>().matches(QKeySequence("Shift+D")) !=
                            QKeySequence::NoMatch)
                        throw std::runtime_error("Another default binds Shift+D: " +
                                                 entry->objectName().toStdString());
            }
            check(!addDriver->isEnabled(), "Add driver is off outside a definition context");
            {
                addDriver->setEnabled(true);
                QSignalSpy triggered(addDriver, &QAction::triggered);
                window.activateWindow();
                window.viewport()->setFocus();
                check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
                      "Viewport focused for Shift+D");
                QTest::keyClick(window.viewport(), Qt::Key_D, Qt::ShiftModifier);
                check(triggered.count() == 1 &&
                          window.findChild<QLabel *>("statusMessage")->text() ==
                              "Select a part inside the definition, then Shift+D",
                      "Shift+D on the viewport triggers Add driver");
                auto *box = window.findChild<QLineEdit *>("measurements");
                box->setFocus();
                box->clear();
                QTest::keyClick(box, 'D', Qt::ShiftModifier);
                check(triggered.count() == 1 && box->text() == "D",
                      "Shift+D types D in a text field");
                box->clear();
                window.viewport()->setFocus();
            }
            settings.sync();
            const auto before = settings.value("shortcuts/v1").toByteArray();
            editor(window, [&](QDialog &dialog) {
                check(dialog.findChild<QLabel *>("shortcutNotices")->text().isEmpty(),
                      "Fresh preferences have no inactive-default notice");
                choose(dialog, "view.commands", "Ctrl+Shift+M");
                click(dialog, "shortcutAssign");
                check(error(dialog).contains("panel"),
                      "Global custom shortcut cannot shadow a panel-local action");
                check(!dialog.findChild<QPushButton *>("shortcutReassign")->isEnabled(),
                      "Panel shortcut cannot be cleared by global reassignment");
                choose(dialog, "tool.1", "Ctrl+Shift+M");
                click(dialog, "shortcutAssign");
                check(error(dialog).isEmpty(),
                      "Viewport-only shortcut can share disjoint panel key");
                dialog.reject();
            });
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "tool.1", "Ctrl+Alt+R");
                click(dialog, "shortcutAssign");
                check(action(window, "tool.1")->shortcut() == QKeySequence("R"),
                      "Draft has no live effect");
                dialog.reject();
            });
            settings.sync();
            check(settings.value("shortcuts/v1").toByteArray() == before,
                  "Cancel preserves saved bytes");
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "tool.1", "Ctrl+Alt+R");
                save(dialog);
                if (!(dialog.isVisible() && error(dialog).contains("Assign")))
                    throw std::runtime_error(("Unassigned Save: " + error(dialog)).toStdString());
                click(dialog, "shortcutAssign");
                save(dialog);
                check(!dialog.isVisible(), "Assigned shortcut saves");
            });
            check(action(window, "tool.1")->shortcut() == QKeySequence("Ctrl+Alt+R"),
                  "Saved shortcut is live");
            check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
                  "Editor returns focus");
            QTest::keyClick(window.viewport(), Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
            check(window.viewport()->tool() == Viewport::Tool::Rectangle,
                  "Actual rebound key selects Rectangle");
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "tool.3", "Ctrl+Alt+R");
                click(dialog, "shortcutAssign");
                if (!error(dialog).contains("already assigned"))
                    throw std::runtime_error(
                        ("Conflict rejects ordinary assignment: " + error(dialog)).toStdString());
                click(dialog, "shortcutReassign");
                save(dialog);
            });
            check(action(window, "tool.1")->shortcut().isEmpty() &&
                      action(window, "tool.3")->shortcut() == QKeySequence("Ctrl+Alt+R"),
                  "Reassignment clears displaced action");
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "file.save", "J");
                click(dialog, "shortcutAssign");
                choose(dialog, "view.commands", "Ctrl+Alt+K");
                click(dialog, "shortcutAssign");
                save(dialog);
            });
            check(action(window, "file.save")->shortcutContext() == Qt::WidgetShortcut,
                  "Plain keys become viewport scoped");
            check(window.findChild<QPushButton *>("commandSearch")->text().contains("Ctrl+Alt+K"),
                  "Search button displays rebound key");
            auto *field = window.findChild<QLineEdit *>("measurements");
            check(field, "Measurements exists");
            field->setFocus();
            field->clear();
            QTest::keyClicks(field, "j");
            check(field->text() == "j", "Plain key preserves text typing");
            window.resize(900, 800);
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "view.assistant", "Ctrl+Alt+J");
                click(dialog, "shortcutAssign");
                save(dialog);
            });
            action(window, "view.assistant")->trigger();
            auto *sheet = window.findChild<QDialog *>("assistantSheet");
            auto *composer = window.findChild<QPlainTextEdit *>("assistantComposer");
            if (!(sheet && sheet->isVisible() && composer &&
                  QTest::qWaitFor([&] { return composer->hasFocus(); })))
                throw std::runtime_error(
                    QString("Floating composer: width=%1 sheet=%2 visible=%3 focus=%4")
                        .arg(window.width())
                        .arg(bool(sheet))
                        .arg(sheet && sheet->isVisible())
                        .arg(QApplication::focusWidget() ? QApplication::focusWidget()->objectName()
                                                         : "none")
                        .toStdString());
            QTest::keyClick(composer, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
            check(QTest::qWaitFor([&] { return !sheet->isVisible(); }),
                  "Rebound modifier shortcut closes floating assistant");
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "view.assistant", "Y");
                click(dialog, "shortcutAssign");
                save(dialog);
            });
            action(window, "view.assistant")->trigger();
            check(QTest::qWaitFor([&] { return composer->hasFocus(); }), "Composer focus restored");
            composer->clear();
            QTest::keyClicks(composer, "y");
            check(sheet->isVisible() && composer->toPlainText() == "y" &&
                      !sheet->findChild<QShortcut *>("assistantSheetToggle")->isEnabled(),
                  "Plain assistant binding cannot intercept composer typing");
            sheet->reject();
            check(encodeContainer(window.document()) == content && !window.document().canUndo(),
                  "Shortcut edits preserve model/history");
        }
        {
            Window window;
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Restart exposed");
            check(action(window, "tool.1")->shortcut().isEmpty() &&
                      action(window, "tool.3")->shortcut() == QKeySequence("Ctrl+Alt+R"),
                  "Choices and unbinding survive restart");
            editor(window, [&](QDialog &dialog) {
                click(dialog, "shortcutReset");
                save(dialog);
            });
            check(action(window, "file.save")->shortcut() == QKeySequence("Ctrl+S") &&
                      action(window, "file.save")->shortcutContext() == Qt::WindowShortcut,
                  "Reset restores global routing");
            settings.sync();
            const auto reset =
                QJsonDocument::fromJson(settings.value("shortcuts/v1").toByteArray()).object();
            check(reset["future"].toInt() == 42 &&
                      reset["bindings"].toObject()["future.command"] == "future syntax",
                  "Native reset preserves unknown entries");
            editor(window, [&](QDialog &dialog) {
                choose(dialog, "tool.1", "Ctrl+Alt+R");
                click(dialog, "shortcutAssign");
                QSettings external;
                external.setValue(
                    "shortcuts/v1",
                    QByteArray("{\"apiVersion\":1,\"bindings\":{},\"external\":true}"));
                external.sync();
                save(dialog);
                check(dialog.isVisible() && error(dialog).contains("changed elsewhere"),
                      "Stale editor rejects overwrite");
                dialog.reject();
            });
        }
        settings.sync();
        auto reserved =
            QJsonDocument::fromJson(settings.value("shortcuts/v1").toByteArray()).object();
        reserved["bindings"] = QJsonObject{{"view.commands", "Ctrl+Shift+M"}};
        const auto savedReserved = QJsonDocument(reserved).toJson();
        settings.setValue("shortcuts/v1", savedReserved);
        settings.sync();
        {
            Window window;
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Reserved binding restart exposed");
            check(action(window, "view.commands")->shortcut().isEmpty() &&
                      action(window, "outliner.move")->shortcut() == QKeySequence("Ctrl+Shift+M"),
                  "Saved collision disables only global binding and preserves panel action");
            settings.sync();
            check(settings.value("shortcuts/v1").toByteArray() == savedReserved,
                  "Reserved saved choice remains byte-exact until explicitly edited");
            editor(window, [&](QDialog &dialog) {
                check(dialog.findChild<QLabel *>("shortcutNotices")->text().contains("panel"),
                      "Inactive saved panel collision has a visible explanation");
                choose(dialog, "view.commands", "Ctrl+Alt+K");
                click(dialog, "shortcutAssign");
                save(dialog);
            });
            check(action(window, "view.commands")->shortcut() == QKeySequence("Ctrl+Alt+K"),
                  "Choosing a nonconflicting key restores global action");
        }
        settings.sync();
        check(settings.value("futureNamespace/data").toByteArray() == unknown,
              "Unrelated typed settings preserved");
        // ADR 0164 §4: workspace scopes in the editor and in live keys (R091.e).
        {
            Window window;
            window.show();
            check(QTest::qWaitForWindowExposed(&window), "Workspace-scope window exposed");
            window.activateWindow();
            check(QTest::qWaitForWindowActive(&window), "Workspace-scope window active");
            window.viewport()->setFocus();
            check(action(window, "sheet.tool.pan")->shortcut() == QKeySequence("H") &&
                      action(window, "tool.5")->shortcut() == QKeySequence("H") &&
                      action(window, "sheet.view.fit")->shortcut() == QKeySequence("Shift+Z") &&
                      action(window, "view.fit")->shortcut() == QKeySequence("Shift+Z") &&
                      action(window, "view.workspace.toggle")->shortcut() ==
                          QKeySequence("Ctrl+Alt+S"),
                  "Model and Sheets defaults share keys; the toggle is Ctrl+Alt+S");
            editor(window, [&](QDialog &dialog) {
                auto *list = dialog.findChild<QTreeWidget *>("shortcutActions");
                check(list && !list->isColumnHidden(2) &&
                          list->headerItem()->text(2) == "Workspace",
                      "The editor shows a Workspace column");
                auto workspaceOf = [&](const char *id) {
                    for (int i = 0; i < list->topLevelItemCount(); ++i)
                        if (list->topLevelItem(i)->data(0, Qt::UserRole).toString() == id)
                            return list->topLevelItem(i)->text(2);
                    return QString{};
                };
                check(workspaceOf("sheet.tool.pan") == "Sheets" &&
                          workspaceOf("tool.6") == "Model" && workspaceOf("view.commands") == "All",
                      "Workspace column names each command's scope");
                // R091.g: L is the sheet Line tool by default; Q (model Rotate) shows the same
                // rule for an assignment.
                choose(dialog, "sheet.tool.zoom", "Q");
                check(error(dialog).isEmpty(), "Q for a sheet tool shows no conflict");
                click(dialog, "shortcutAssign");
                check(error(dialog).isEmpty(),
                      "Q for a sheet tool does not conflict with model Rotate");
                choose(dialog, "sheet.tool.select", "Ctrl+S");
                check(error(dialog).contains("Save") && error(dialog).contains("(All)"),
                      ("A global key conflicts across workspaces: " + error(dialog).toStdString())
                          .c_str());
                click(dialog, "shortcutAssign");
                check(error(dialog).contains("already assigned"),
                      "Assigning a global key to a sheet tool is refused");
                choose(dialog, "sheet.tool.select", "Space");
                save(dialog);
                check(!dialog.isVisible(), "Scoped assignment saves");
            });
            check(action(window, "sheet.tool.zoom")->shortcut() == QKeySequence("Q") &&
                      action(window, "sheet.tool.line")->shortcut() == QKeySequence("L") &&
                      action(window, "tool.6")->shortcut() == QKeySequence("L"),
                  "Both L bindings are live, and the assigned Q");
            QTest::keyClick(window.viewport(), Qt::Key_L);
            check(window.viewport()->tool() == Viewport::Tool::Line, "L selects Line in Model");
            QTest::keyClick(window.viewport(), Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
            check(window.workspace() == Window::Workspace::Sheets, "Ctrl+Alt+S shows Sheets");
            auto *canvas = window.findChild<QWidget *>("sheetCanvas");
            check(QTest::qWaitFor([&] { return canvas->hasFocus(); }), "Canvas focused");
            // This model has no pages, so the page-item tools (L) wait for one; Q does not.
            QTest::keyClick(canvas, Qt::Key_Q);
            check(window.findChild<QLabel *>("activeToolLabel")->text() == "Zoom" &&
                      window.viewport()->tool() == Viewport::Tool::Line,
                  "Q selects the sheet Zoom tool in Sheets and leaves the model tool alone");
            QTest::keyClick(canvas, Qt::Key_H);
            check(window.findChild<QLabel *>("activeToolLabel")->text() == "Pan" &&
                      window.viewport()->tool() == Viewport::Tool::Line,
                  "H selects sheet Pan, not model Pan, in Sheets");
            QTest::keyClick(canvas, Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
            check(window.workspace() == Window::Workspace::Model &&
                      QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
                  "Ctrl+Alt+S returns to Model");
            editor(window, [&](QDialog &dialog) {
                click(dialog, "shortcutReset");
                save(dialog);
            });
        }
        std::cout << "Native shortcuts: draft/cancel/save, live keys, conflicts, typing, restart, "
                     "reset and stale-save rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
