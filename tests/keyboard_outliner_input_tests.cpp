#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/groups.hpp"
#include <QApplication>
#include <QDialog>
#include <QLineEdit>
#include <QSettings>
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
void key(Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    check(QTest::qWaitFor([] { return QApplication::focusWidget() != nullptr; }),
          "Keyboard workflow has a focus owner");
    QTest::keyClick(QApplication::focusWidget(), code, modifiers);
    QCoreApplication::processEvents();
}
void outliner(QTreeWidget *tree) {
    for (int i = 0; !tree->hasFocus() && i < 12; ++i)
        key(Qt::Key_F6);
    check(tree->hasFocus(), "F6 reaches the Outliner without direct focus assignment");
}
void choose(QTreeWidget *tree, Id id) {
    outliner(tree);
    key(Qt::Key_Home);
    auto current = [&] {
        return tree->currentItem() ? tree->currentItem()->data(0, Qt::UserRole).toULongLong() : 0;
    };
    for (int i = 0; current() != id && i < 16; ++i)
        key(Qt::Key_Down);
    check(current() == id, "Arrow navigation reaches the intended entity");
}
void rename(Window &window, const QString &name) {
    bool handled{};
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("organizationDialog");
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            auto *edit = dialog->findChild<QLineEdit *>("organizationName");
            check(edit && QTest::qWaitFor([&] { return edit->hasFocus(); }),
                  "Rename dialog gives its name field keyboard focus");
            key(Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(edit, name);
            key(Qt::Key_Return);
            check(!dialog->isVisible(), "Enter accepts the typed rename");
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    timer.start(10);
    key(Qt::Key_F2);
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled && QTest::qWaitForWindowActive(&window),
          "Rename returns keyboard activation to the parent");
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
    QSettings().setValue("recoverySeconds", 0);
    Window window;
    try {
        auto &doc = window.document();
        auto *view = window.viewport();
        // A small hierarchy is fixture setup; all subsequent editing uses actual keys.
        const auto child = doc.addFace({{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}});
        const auto neighbor = doc.addFace({{{3, 0, 0}, {4, 0, 0}, {4, 1, 0}, {3, 1, 0}}});
        const auto group = createGroup(doc, {child}, "Frame");
        const auto originalChildName = doc.bodies().at(child)->name;
        const auto untouched = doc.bodies().at(neighbor);
        QMetaObject::invokeMethod(view, "changed");
        window.resize(1280, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Keyboard Outliner window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Keyboard Outliner window active");
        view->setFocus();
        check(QTest::qWaitFor([&] { return view->hasFocus(); }), "Initial viewport focus");
        auto *tree = window.findChild<QTreeWidget *>("outlinerTree");
        check(tree && tree->isVisible(), "Outliner available");
        choose(tree, group);
        check(view->selectedBody() == group, "Keyboard row selection updates viewport selection");
        rename(window, "Keyboard frame");
        check(doc.bodies().at(group)->name == "Keyboard frame", "Keyboard renames selected group");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().at(group)->name == "Frame", "Keyboard undo restores group name");
        key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(doc.bodies().at(group)->name == "Keyboard frame", "Keyboard redo restores rename");
        choose(tree, group);
        key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        check(doc.bodies().at(group)->locked, "Outliner lock shortcut works");
        key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
        check(!doc.bodies().at(group)->locked, "Outliner unlock shortcut works");
        key(Qt::Key_Space);
        check(doc.bodies().at(group)->hidden, "Outliner visibility shortcut hides group");
        key(Qt::Key_Space);
        check(!doc.bodies().at(group)->hidden, "Outliner visibility shortcut restores group");
        key(Qt::Key_Return);
        check(view->selectionState().context() == group, "Enter opens the selected group context");
        choose(tree, child);
        rename(window, "Nested keyboard panel");
        check(doc.bodies().at(child)->name == "Nested keyboard panel" &&
                  doc.bodies().at(child)->parent == group,
              "Keyboard renames the intended child without changing ownership");
        key(Qt::Key_Z, Qt::ControlModifier);
        check(doc.bodies().at(child)->name == originalChildName, "Undo restores nested name");
        key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        check(doc.bodies().at(child)->name == "Nested keyboard panel", "Redo restores nested name");
        outliner(tree);
        key(Qt::Key_Escape);
        check(view->selectionState().context() == 0, "Escape exits nested context");
        check(doc.bodies().at(neighbor) == untouched && doc.bodies().size() == 3,
              "Keyboard organization preserves neighboring geometry");
        doc.markSaved();
        std::cout
            << "Keyboard-only Outliner: F6, arrows, rename, lock, visibility, nested context, "
               "undo/redo and untouched neighbor passed\n";
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
