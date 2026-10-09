#include "app/window.hpp"
#include "io/document_io.hpp"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QKeySequenceEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
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
    auto *list = dialog.findChild<QListWidget *>("shortcutActions");
    check(list, "Command list exists");
    int row = -1;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == id)
            row = i;
    check(row >= 0, "Requested command appears in editor");
    list->setCurrentRow(row);
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
    QSurfaceFormat::setDefaultFormat(format);
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
            const auto before = settings.value("shortcuts/v1").toByteArray();
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
        check(settings.value("futureNamespace/data").toByteArray() == unknown,
              "Unrelated typed settings preserved");
        std::cout << "Native shortcuts: draft/cancel/save, live keys, conflicts, typing, restart, "
                     "reset and stale-save rejection passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
