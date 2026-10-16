// R094.a: the right sidebar is user-resizable. Synthetic mouse drags on the real splitter
// handles, the View menu keyboard actions, persistence across window re-creation, the bounds
// (minimum, maximum, viewport minimum on window shrink), the tabbed and column layouts, both
// workspaces and the interface text-size rule.
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <memory>
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    std::unique_ptr<Window> window;
    QSplitter *splitter{};
    QTabWidget *side{};
    QWidget *workspace{};
    explicit Fixture(int width) {
        window = std::make_unique<Window>();
        window->setFixedSize(width, 900);
        window->show();
        check(QTest::qWaitForWindowExposed(window.get()), "Window exposed");
        splitter = window->findChild<QSplitter *>("sidebarSplitter");
        side = window->findChild<QTabWidget *>("assistantSideTabs");
        workspace = window->findChild<QStackedWidget *>("workspaceStack");
        check(splitter && side && workspace && splitter->indexOf(workspace) == 0 &&
                  splitter->indexOf(side) == 1,
              "The workspace and side panel share the sidebar splitter");
        settle();
    }
    ~Fixture() {
        if (window)
            window->document().markSaved();
    }
    void settle() {
        QTest::qWait(30);
        QCoreApplication::processEvents();
    }
    void resize(int width) {
        window->setFixedSize(width, 900);
        check(QTest::qWaitFor([&] { return window->width() == width; }), "Window resized");
        settle();
    }
    QSplitterHandle *handle(int index) const { return splitter->handle(index); }
    void drag(int index, int dx) {
        auto *grip = handle(index);
        check(grip && grip->isVisible(), "Handle " + std::to_string(index) + " is visible");
        const auto start = grip->rect().center();
        QTest::mousePress(grip, Qt::LeftButton, {}, start);
        // Move in steps, as a pointer does. The handle moves with the drag, so each position
        // is fixed on screen and mapped into the handle's current coordinates.
        const auto origin = grip->mapToGlobal(start);
        for (int step = 1; step <= 4; ++step)
            QTest::mouseMove(grip, grip->mapFromGlobal(origin + QPoint(dx * step / 4, 0)));
        QTest::mouseRelease(grip, Qt::LeftButton, {}, grip->mapFromGlobal(origin + QPoint(dx, 0)));
        settle();
    }
    int width(QWidget *column) const { return column->width(); }
    // The sidebar never overlaps the workspace, and the workspace keeps its minimum.
    void checkGeometry(const std::string &where) const {
        check(workspace->width() >= 400,
              where + ": workspace keeps 400 px (" + std::to_string(workspace->width()) + ")");
        check(workspace->geometry().right() < side->geometry().left(),
              where + ": sidebar does not overlap the workspace");
        check(side->geometry().right() < splitter->width(), where + ": sidebar fits the window");
    }
    void key(Qt::Key code) {
        window->activateWindow();
        check(QTest::qWaitForWindowActive(window.get()), "Window active");
        window->viewport()->setFocus();
        check(QTest::qWaitFor([] { return QApplication::focusWidget() != nullptr; }),
              "A widget has focus");
        QTest::keyClick(QApplication::focusWidget(), code, Qt::ControlModifier | Qt::AltModifier);
        settle();
    }
    QAction *action(const char *id) const {
        auto *found = window->findChild<QAction *>(id);
        check(found != nullptr, std::string("Action ") + id + " exists");
        return found;
    }
};
int stored(const char *key) {
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.sync();
    return preferences.value(key, -1).toInt();
}
QString accessibleName(QWidget *widget) {
    auto *accessible = QAccessible::queryAccessibleInterface(widget);
    return accessible ? accessible->text(QAccessible::Name) : QString();
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
    double dpr = 1;
    try {
        {
            // Corrupt and out-of-range stored widths fall back to the defaults.
            QSettings preferences("SketchyUp", "SketchyUp");
            preferences.setValue("sidebarTrayWidth", "wide");
            preferences.setValue("sidebarTabbedWidth", 5000);
            preferences.setValue("sidebarAssistantWidth", 12);
            preferences.sync();
        }
        {
            Fixture f(1200);
            dpr = f.window->devicePixelRatioF();
            check(f.width(f.side) == 248,
                  "Default side panel width is 248 px, got " + std::to_string(f.width(f.side)));
            check(accessibleName(f.handle(1)) == "Resize sidebar",
                  "The sidebar handle is named for assistive technology");
            f.checkGeometry("Default");
            // Drag the sidebar's left edge.
            f.drag(1, -100);
            check(std::abs(f.width(f.side) - 348) <= 1,
                  "Dragging left widens the sidebar, got " + std::to_string(f.width(f.side)));
            check(stored("sidebarTrayWidth") == f.width(f.side), "The width persists");
            f.drag(1, 60);
            check(std::abs(f.width(f.side) - 288) <= 1, "Dragging right narrows the sidebar");
            // Bounds: 240 px minimum, and 720 px or the window less the workspace minimum.
            f.drag(1, 2000);
            check(f.width(f.side) == 240,
                  "The sidebar stops at 240 px, got " + std::to_string(f.width(f.side)));
            f.drag(1, -2000);
            const int maximum =
                std::min(720, f.splitter->width() - 400 - f.splitter->handleWidth());
            check(f.width(f.side) == maximum, "The sidebar stops at the maximum " +
                                                  std::to_string(maximum) + ", got " +
                                                  std::to_string(f.width(f.side)));
            f.checkGeometry("Maximum");
            // Keyboard: Narrow and Widen step 40 px.
            f.key(Qt::Key_BracketLeft);
            check(f.width(f.side) == maximum - 40, "Ctrl+Alt+[ narrows the sidebar by 40 px");
            f.key(Qt::Key_BracketRight);
            check(f.width(f.side) == maximum, "Ctrl+Alt+] widens the sidebar by 40 px");
            f.key(Qt::Key_BracketRight);
            check(f.width(f.side) == maximum, "Widen stops at the maximum");
            // Window shrink clamps; growing restores the preference.
            f.resize(1600);
            f.drag(1, f.width(f.side) - 600);
            check(f.width(f.side) == 600, "A 600 px sidebar fits a 1600 px window");
            f.resize(1000);
            f.checkGeometry("Shrunk to 1000 px");
            check(f.width(f.side) == f.splitter->width() - 400 - f.splitter->handleWidth(),
                  "The sidebar clamps when the window shrinks");
            f.resize(820);
            f.checkGeometry("Shrunk to 820 px");
            f.resize(1600);
            check(f.width(f.side) == 600, "Growing the window restores the chosen width");
            // Reset: double-click the handle, then the View action.
            QTest::mouseDClick(f.handle(1), Qt::LeftButton, {}, f.handle(1)->rect().center());
            check(QTest::qWaitFor([&] { return f.width(f.side) == 248; }),
                  "Double-clicking the handle resets the width, got " +
                      std::to_string(f.width(f.side)));
            check(stored("sidebarTrayWidth") == -1, "Reset forgets the stored width");
            f.action("view.sidebar.narrower")->trigger();
            f.settle();
            check(f.width(f.side) == 240, "Narrow sidebar from the View menu (clamped at 240)");
            f.action("view.sidebar.reset")->trigger();
            f.settle();
            check(f.width(f.side) == 248, "View > Reset sidebar width restores the default");
            // Both workspaces share the width.
            f.drag(1, -52);
            check(f.width(f.side) == 300, "Sidebar at 300 px in Model");
            f.window->setWorkspace(Window::Workspace::Sheets);
            f.settle();
            check(f.width(f.side) == 300, "Sheets shares the sidebar width");
            f.checkGeometry("Sheets");
            f.drag(1, -20);
            check(f.width(f.side) == 320, "Resizing in Sheets");
            f.window->setWorkspace(Window::Workspace::Model);
            f.settle();
            check(f.width(f.side) == 320, "Model keeps the width set in Sheets");
            // Columns layout: Tray and Assistant columns with a handle between them.
            f.action("view.assistant")->trigger();
            auto *assistant = f.window->assistantPanel();
            check(QTest::qWaitFor([&] { return f.splitter->indexOf(assistant) == 2; }),
                  "The Assistant is a separate column at 1600 px");
            f.settle();
            check(f.width(f.side) == 320 && f.width(assistant) == 340,
                  "The Tray keeps its width and the Assistant column defaults to 340 px");
            check(accessibleName(f.handle(1)) == "Resize sidebar" &&
                      accessibleName(f.handle(2)) == "Resize between Tray and Assistant",
                  "Both handles are named");
            f.drag(2, -60);
            check(f.width(f.side) == 260 && f.width(assistant) == 400,
                  "The middle handle trades width between Tray and Assistant, got " +
                      std::to_string(f.width(f.side)) + "/" + std::to_string(f.width(assistant)));
            check(stored("sidebarAssistantWidth") == 400 && stored("sidebarTrayWidth") == 260,
                  "Both column widths persist");
            f.checkGeometry("Columns");
            // Tabbed layout remembers its own width.
            f.resize(1300);
            check(f.side->indexOf(assistant) >= 0, "Tray and Assistant share the side panel");
            check(f.width(f.side) == 340,
                  "The tabbed layout defaults to 340 px, got " + std::to_string(f.width(f.side)));
            f.drag(1, -40);
            check(f.width(f.side) == 380, "The tabbed layout resizes");
            check(stored("sidebarTabbedWidth") == 380, "The tabbed width persists");
            f.resize(1600);
            check(f.splitter->indexOf(assistant) == 2 && f.width(f.side) == 260 &&
                      f.width(assistant) == 400,
                  "The column layout restores its own widths");
            // Shrinking with both columns keeps the workspace minimum.
            f.drag(1, -2000);
            f.checkGeometry("Columns at maximum");
            check(f.workspace->width() == 400, "The Tray grows until the workspace minimum");
            f.action("view.sidebar.reset")->trigger();
            f.settle();
            check(f.width(f.side) == 248 && f.width(assistant) == 340,
                  "Reset restores both column defaults");
            f.drag(1, -52);
            f.drag(2, -40);
            check(f.width(f.side) == 260 && f.width(assistant) == 380, "Columns resized again");
            f.action("view.assistant")->trigger();
            f.settle();
        }
        {
            // A new window with the same settings restores the widths.
            Fixture f(1600);
            check(f.width(f.side) == 260, "The Tray width is restored on the next launch, got " +
                                              std::to_string(f.width(f.side)));
            f.action("view.assistant")->trigger();
            auto *assistant = f.window->assistantPanel();
            check(QTest::qWaitFor([&] { return f.splitter->indexOf(assistant) == 2; }),
                  "Assistant column after re-creation");
            f.settle();
            check(f.width(assistant) == 380, "The Assistant width is restored");
            f.action("view.assistant")->trigger();
            f.settle();
        }
        {
            // A stored width larger than the window allows is clamped on launch.
            QSettings("SketchyUp", "SketchyUp").setValue("sidebarTrayWidth", 700);
            Fixture f(900);
            f.checkGeometry("Launch at 900 px");
            check(f.width(f.side) == f.splitter->width() - 400 - f.splitter->handleWidth(),
                  "A stored width clamps to the current bounds");
            f.resize(1600);
            check(f.width(f.side) == 700, "The stored width applies when it fits");
        }
        {
            // Interface text size keeps the width in proportion (stored at 100% text).
            QSettings preferences("SketchyUp", "SketchyUp");
            preferences.setValue("sidebarTrayWidth", 300);
            preferences.setValue("interfaceTextPercent", 150);
            preferences.sync();
            Fixture f(1600);
            check(f.width(f.side) == 450,
                  "300 px at 150% text is 450 px, got " + std::to_string(f.width(f.side)));
            f.drag(1, 2000);
            check(f.width(f.side) == 360, "The minimum scales with text (240 at 150% is 360)");
            f.key(Qt::Key_BracketRight);
            check(f.width(f.side) == 420, "The keyboard step scales with text (60 px at 150%)");
            check(stored("sidebarTrayWidth") == 280, "Widths are stored at 100% text");
            QSettings("SketchyUp", "SketchyUp").setValue("interfaceTextPercent", 100);
        }
        std::cout << "Native sidebar resize: drag, bounds, window shrink, keyboard, double-click "
                     "and menu reset, persistence, columns/tabbed, Sheets, text size passed; DPR="
                  << dpr << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
