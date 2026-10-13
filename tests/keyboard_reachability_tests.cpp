#include "app/render_panel.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <iostream>
#include <set>
// R084.v: keyboard reachability of the tool rail, the main window's Tab cycle, the side tabs
// and the reopened Render with Blender sheet (D-026). Real key events to the focused widget only.
using namespace sketchy;
namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
QWidget *focused() { return QApplication::focusWidget(); }
std::string identity(const QWidget *widget) {
    if (!widget)
        return "none";
    return std::string(widget->metaObject()->className()) + "/" +
           widget->objectName().toStdString() +
           (widget->accessibleName().isEmpty()
                ? std::string{}
                : " \"" + widget->accessibleName().toStdString() + "\"");
}
// `closes` marks a key that hides the active sheet; the window manager then decides which
// window is active, so focus is re-established by the caller.
void key(Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool closes = false) {
    check(QTest::qWaitFor([] { return focused() != nullptr; }),
          "Keyboard workflow always has a focus owner");
    if (qEnvironmentVariableIsSet("R084V_DEBUG"))
        std::cerr << "key "
                  << QKeySequence(QKeyCombination(modifiers, code)).toString().toStdString()
                  << " on " << identity(focused()) << '\n';
    QTest::keyClick(focused(), code, modifiers);
    QCoreApplication::processEvents();
    if (closes)
        return;
    check(QTest::qWaitFor([] { return focused() != nullptr; }),
          "A key press never leaves the window without a focus owner (after " +
              QKeySequence(QKeyCombination(modifiers, code)).toString().toStdString() + ")");
}
QString toolId(Viewport::Tool tool) { return "tool." + QString::number(int(tool)); }
// A QTabBar's own scroll arrows.
bool tabScrollButton(const QWidget *widget) {
    return qobject_cast<const QToolButton *>(widget) && widget->parentWidget() &&
           qobject_cast<const QTabBar *>(widget->parentWidget());
}
// Runs `action` (which opens the command palette) and chooses `label` using only key events.
void paletteChoose(Window &window, const QString &label) {
    bool handled{};
    std::exception_ptr failure;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("commandPalette");
        if (handled || !dialog || !dialog->isVisible())
            return;
        handled = true;
        try {
            auto *query = dialog->findChild<QLineEdit *>("paletteQuery");
            auto *list = dialog->findChild<QListWidget *>("paletteResults");
            check(query && list && QTest::qWaitFor([&] { return query->hasFocus(); }),
                  "Command palette focuses its query field");
            // QTest types ASCII only; the list match below still uses the full label.
            QTest::keyClicks(query, QString(label).remove(QChar(0x2026)));
            for (int i = 0; i < 40; ++i) {
                auto *item = list->currentItem();
                if (item && item->text().startsWith(label + "    "))
                    break;
                QTest::keyClick(query, Qt::Key_Down);
            }
            check(list->currentItem() && list->currentItem()->text().startsWith(label + "    "),
                  "Command palette lists " + label.toStdString());
            QTest::keyClick(query, Qt::Key_Return);
            check(!dialog->isVisible(), "Enter runs the chosen palette command");
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    timer.start(10);
    key(Qt::Key_K, Qt::ControlModifier);
    timer.stop();
    if (failure)
        std::rethrow_exception(failure);
    check(handled, "Ctrl+K opened the command palette");
}
// After a sheet hides itself, Weston may leave no window active; synthetic input cannot
// re-activate it, so map the window again (as the R084.u dialog audit does).
void activate(QWidget &window) {
    window.activateWindow();
    if (QTest::qWaitForWindowActive(&window, 2000))
        return;
    for (int attempt = 0; attempt < 3 && !window.isActiveWindow(); ++attempt) {
        window.hide();
        window.show();
        (void)QTest::qWaitForWindowExposed(&window, 5000);
        window.activateWindow();
        (void)QTest::qWaitForWindowActive(&window, 2000);
        QTest::qWait(300);
    }
    check(window.isActiveWindow(), "Model window active again");
}
struct Fixture {
    Window &window;
    QToolBar *rail;
    std::vector<QToolButton *> railButtons() const {
        std::vector<QToolButton *> buttons;
        for (auto *action : rail->actions())
            if (auto *button = qobject_cast<QToolButton *>(rail->widgetForAction(action));
                button && !action->isSeparator() && button->isVisibleTo(rail) &&
                button->isEnabled())
                buttons.push_back(button);
        // Qt's overflow button is the last stop when the tools do not all fit.
        if (auto *more = rail->findChild<QToolButton *>("qt_toolbar_ext_button");
            more && more->isVisibleTo(rail))
            buttons.push_back(more);
        return buttons;
    }
    QToolButton *railButton(Viewport::Tool tool) const {
        return qobject_cast<QToolButton *>(
            rail->widgetForAction(window.findChild<QAction *>(toolId(tool))));
    }
    bool inRail() const { return focused() && rail->isAncestorOf(focused()); }
    std::string state() const { return " (focus " + identity(focused()) + ")"; }
    void expectFocus(QWidget *widget, const std::string &via) const {
        check(QTest::qWaitFor([&] { return widget->hasFocus(); }, 2000),
              via + " focuses " + identity(widget) + state());
    }
    void expectTool(Viewport::Tool tool, const std::string &via) const {
        check(window.viewport()->tool() == tool &&
                  window.findChild<QAction *>(toolId(tool))->isChecked(),
              via + " makes " + window.findChild<QAction *>(toolId(tool))->text().toStdString() +
                  " the active tool");
    }
    // Walks the whole Tab (or Shift+Tab) cycle from the viewport back to it. The viewport traps
    // Tab only for the Select tool's entity cycling, so the caller activates another tool.
    std::vector<QWidget *> cycle(bool backwards) const {
        window.viewport()->setFocus();
        check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
              "Tab cycle starts in the viewport");
        std::vector<QWidget *> stops;
        for (int i = 0; i < 400; ++i) {
            if (backwards)
                key(Qt::Key_Backtab, Qt::ShiftModifier);
            else
                key(Qt::Key_Tab);
            auto *now = focused();
            check(now->window() == &window, "Tab stays in the model window" + state());
            if (now == window.viewport())
                return stops;
            stops.push_back(now);
        }
        throw std::runtime_error(std::string(backwards ? "Shift+Tab" : "Tab") +
                                 " cycle returns to the viewport within 400 presses");
    }
    // Both cycles have no tab-bar scroll button and one rail stop; together they reach `bars`.
    // (A Model-panel bar is entered by Shift+Tab from its page; Tab from the canvas goes
    // straight into the panel's focus proxy, as R084.s records.)
    void checkCycle(const std::vector<QTabBar *> &bars) const {
        std::set<QWidget *> reached;
        for (const bool backwards : {false, true}) {
            const auto stops = cycle(backwards);
            const char *name = backwards ? "Shift+Tab cycle" : "Tab cycle";
            int railStops{};
            std::set<QWidget *> seen;
            QWidget *previous{};
            for (auto *stop : stops) {
                check(!tabScrollButton(stop),
                      std::string(name) + " skips tab-bar scroll buttons, found " + identity(stop));
                // A label's links are visited inside the one widget, one press each.
                if (stop == previous)
                    continue;
                check(seen.insert(stop).second,
                      std::string(name) + " visits " + identity(stop) + " once");
                railStops += rail->isAncestorOf(stop);
                previous = stop;
            }
            check(railStops == 1, std::string(name) + " has exactly one tool-rail stop, found " +
                                      std::to_string(railStops));
            reached.insert(seen.begin(), seen.end());
            std::cout << name << ": " << stops.size() + 1 << " stops\n";
        }
        for (auto *bar : bars)
            check(reached.contains(bar), "Tab or Shift+Tab cycle reaches tab bar " + identity(bar) +
                                             " of " + identity(bar->parentWidget()));
    }
    // Tab from a tab bar lands directly in the current page; Shift+Tab returns to the bar.
    void intoPageAndBack(QTabWidget *tabs, const std::string &title) const {
        auto *bar = tabs->tabBar();
        check(bar->hasFocus(), "Tab bar has focus before entering " + title + state());
        check(tabs->tabText(tabs->currentIndex()).startsWith(QString::fromStdString(title)),
              "Arrow keys show the " + title + " tab");
        key(Qt::Key_Tab);
        auto *page = tabs->currentWidget();
        check(page->isVisible() && (focused() == page || page->isAncestorOf(focused())),
              "Tab from the tab bar moves into the " + title + " page" + state());
        // Shift+Tab walks back through the page's earlier controls, then to the bar.
        for (int i = 0; i < 40 && !bar->hasFocus(); ++i) {
            key(Qt::Key_Backtab, Qt::ShiftModifier);
            check(bar->hasFocus() || page->isAncestorOf(focused()) || focused() == page,
                  "Shift+Tab stays in the " + title + " page until its tab bar" + state());
        }
        check(bar->hasFocus(),
              "Shift+Tab from the " + title + " page returns to its tab bar" + state());
    }
    // Reaches `bar` from the current focus with Tab or Shift+Tab, never passing a scroll button.
    void tabToBar(QTabBar *bar, bool backwards) const {
        for (int i = 0; i < 400 && !bar->hasFocus(); ++i) {
            if (backwards)
                key(Qt::Key_Backtab, Qt::ShiftModifier);
            else
                key(Qt::Key_Tab);
            check(!tabScrollButton(focused()), "Tab never stops on a tab scroll button" + state());
        }
        check(bar->hasFocus(), "Tab reaches " + identity(bar) + state());
    }
};
QString fakeBlender(const QString &directory) {
    const auto path = QDir(directory).filePath("blender-fake");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly), "Create fake Blender launcher");
    file.write(("#!/bin/sh\nexec '" + QCoreApplication::applicationDirPath() +
                "/blender_job_tests' --fake success \"$@\"\n")
                   .toUtf8());
    file.close();
    check(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner),
          "Fake Blender launcher is executable");
    return path;
}
void toolRail(Fixture &f) {
    auto &window = f.window;
    auto buttons = f.railButtons();
    check(buttons.size() > 10, "Tool rail lists its tools");
    int tabStops{};
    for (auto *button : buttons) {
        auto *accessible = QAccessible::queryAccessibleInterface(button);
        auto name = button->defaultAction() ? button->defaultAction()->text() : "More tools";
        name.remove('&');
        check(accessible && accessible->text(QAccessible::Name) == name,
              "Tool button's accessible name is its tool name: " + name.toStdString() + ", found " +
                  (accessible ? accessible->text(QAccessible::Name).toStdString() : "none"));
        tabStops += bool(button->focusPolicy() & Qt::TabFocus);
    }
    check(!(f.rail->focusPolicy() & Qt::TabFocus) && tabStops == 1 &&
              (f.railButton(Viewport::Tool::Select)->focusPolicy() & Qt::TabFocus),
          "The rail has one Tab stop: the active tool");
    auto *search = window.findChild<QWidget *>("commandSearch");
    auto *select = f.railButton(Viewport::Tool::Select);
    const int last = int(buttons.size()) - 1;
    // Tab enters on the active tool.
    search->setFocus();
    check(QTest::qWaitFor([&] { return search->hasFocus(); }), "Command search focused");
    key(Qt::Key_Tab);
    f.expectFocus(select, "Tab from command search into the rail");
    // Arrows, wrapping, Home and End move focus without changing the tool.
    key(Qt::Key_Down);
    f.expectFocus(buttons[1], "Down");
    key(Qt::Key_Up);
    f.expectFocus(buttons[0], "Up");
    key(Qt::Key_Up);
    f.expectFocus(buttons[last], "Up at the first tool wraps");
    key(Qt::Key_Down);
    f.expectFocus(buttons[0], "Down at the last tool wraps");
    key(Qt::Key_Right);
    f.expectFocus(buttons[1], "Right");
    key(Qt::Key_Left);
    f.expectFocus(buttons[0], "Left");
    key(Qt::Key_Left);
    f.expectFocus(buttons[last], "Left at the first tool wraps");
    key(Qt::Key_Right);
    f.expectFocus(buttons[0], "Right at the last tool wraps");
    key(Qt::Key_End);
    f.expectFocus(buttons[last], "End");
    key(Qt::Key_Home);
    f.expectFocus(buttons[0], "Home");
    f.expectTool(Viewport::Tool::Select, "Arrow keys alone");
    // Disabled and hidden buttons are skipped.
    buttons[1]->defaultAction()->setEnabled(false);
    key(Qt::Key_Down);
    f.expectFocus(buttons[2], "Down skips a disabled tool");
    key(Qt::Key_Up);
    f.expectFocus(buttons[0], "Up skips a disabled tool");
    buttons[1]->defaultAction()->setEnabled(true);
    buttons[2]->defaultAction()->setVisible(false);
    key(Qt::Key_Down);
    f.expectFocus(buttons[1], "Down reaches a re-enabled tool");
    key(Qt::Key_Down);
    f.expectFocus(buttons[3], "Down skips a hidden tool");
    buttons[2]->defaultAction()->setVisible(true);
    // Tab and Shift+Tab leave the rail; re-entry lands on the active tool again.
    key(Qt::Key_Backtab, Qt::ShiftModifier);
    check(!f.inRail(), "Shift+Tab leaves the rail" + f.state());
    key(Qt::Key_Tab);
    f.expectFocus(select, "Tab back into the rail returns to the active tool");
    key(Qt::Key_Down);
    key(Qt::Key_Tab);
    check(!f.inRail(), "Tab leaves the rail from any tool" + f.state());
    // Enter and Space activate the focused tool as a click does (focus moves to the canvas).
    auto *paint = f.railButton(Viewport::Tool::Paint);
    auto *rectangle = f.railButton(Viewport::Tool::Rectangle);
    // The canvas keeps Tab for entity cycling with Select, so return by region (Shift+F6).
    check(window.viewport()->hasFocus(), "Tab from the rail reaches the canvas" + f.state());
    key(Qt::Key_F6, Qt::ShiftModifier);
    f.expectFocus(select, "Shift+F6 back into the rail lands on the active tool");
    for (int i = 0; i < 3; ++i)
        key(Qt::Key_Down);
    f.expectFocus(rectangle, "Down reaches Rectangle");
    key(Qt::Key_Return);
    f.expectTool(Viewport::Tool::Rectangle, "Enter on the Rectangle button");
    f.expectFocus(window.viewport(), "Activating a tool by keyboard");
    key(Qt::Key_F6, Qt::ShiftModifier);
    f.expectFocus(rectangle, "Shift+F6 from the canvas returns to the active tool");
    key(Qt::Key_Up);
    key(Qt::Key_Up);
    f.expectFocus(paint, "Up from Rectangle");
    key(Qt::Key_Space);
    f.expectTool(Viewport::Tool::Paint, "Space on the Paint button");
    f.expectFocus(window.viewport(), "Activating a tool by keyboard");
    key(Qt::Key_F6, Qt::ShiftModifier);
    f.expectFocus(paint, "Shift+F6 lands on the new active tool");
    key(Qt::Key_Return);
    f.expectTool(Viewport::Tool::Paint, "Enter on the active tool");
    // Tools that do not fit are behind Qt's overflow button, the rail's last stop. Enter opens
    // its menu of the remaining tools. Qt's Wayland popups need a compositor input serial that
    // synthetic keys cannot supply, so the menu is opened and driven by keys on X11 only.
    auto *more = f.rail->findChild<QToolButton *>("qt_toolbar_ext_button");
    if (!more || !more->isVisible())
        return;
    auto *pan = window.findChild<QAction *>(toolId(Viewport::Tool::Pan));
    check(!f.railButton(Viewport::Tool::Pan)->isVisible(), "Pan is in the rail's overflow");
    key(Qt::Key_F6, Qt::ShiftModifier);
    key(Qt::Key_End);
    f.expectFocus(more, "End reaches the overflow button");
    if (QGuiApplication::platformName() != "xcb")
        return;
    bool listed{}, chosen{};
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu || listed)
            return;
        listed = menu->actions().contains(pan);
        if (!listed) {
            menu->close();
            return;
        }
        for (int i = 0; i < 40 && menu->activeAction() != pan; ++i)
            QTest::keyClick(menu, Qt::Key_Down);
        chosen = menu->activeAction() == pan;
        QTest::keyClick(menu, chosen ? Qt::Key_Return : Qt::Key_Escape);
    });
    timer.start(20);
    key(Qt::Key_Return);
    check(QTest::qWaitFor([&] { return listed && !QApplication::activePopupWidget(); }),
          "Enter on More tools opens a menu that lists Pan");
    timer.stop();
    check(chosen, "Arrow keys reach Pan in the overflow menu");
    f.expectTool(Viewport::Tool::Pan, "Enter on Pan in the overflow menu");
    activate(window);
    // The active tool is hidden in the overflow, so the rail's stop is its first tool.
    key(Qt::Key_F6, Qt::ShiftModifier);
    f.expectFocus(select, "Shift+F6 lands on the first tool when the active one overflows");
    key(Qt::Key_Down);
    key(Qt::Key_Space);
    f.expectTool(Viewport::Tool::Paint, "Space on the Paint button");
}
void sideTabs(Fixture &f) {
    auto &window = f.window;
    auto *side = window.findChild<QTabWidget *>("assistantSideTabs");
    check(side, "Side tabs exist");
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Viewport focused");
    key(Qt::Key_J, Qt::ControlModifier);
    auto *assistant = window.assistantPanel();
    check(QTest::qWaitFor([&] {
              return side->isVisible() && side->count() == 2 &&
                     side->currentWidget() == assistant && assistant->isAncestorOf(focused());
          }),
          "Ctrl+J shows the Assistant side tab and focuses it" + f.state());
    f.tabToBar(side->tabBar(), true);
    key(Qt::Key_Left);
    f.intoPageAndBack(side, "Model");
    key(Qt::Key_Right);
    f.intoPageAndBack(side, "Assistant");
    key(Qt::Key_Left);
    f.intoPageAndBack(side, "Model");
    key(Qt::Key_Right);
    check(side->currentWidget() == assistant, "Right returns to the Assistant tab");
    // Forward Tab from the canvas also reaches the side tab bar.
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Viewport focused");
    f.tabToBar(side->tabBar(), false);
    f.checkCycle({side->tabBar()});
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Viewport focused");
    key(Qt::Key_J, Qt::ControlModifier);
    check(QTest::qWaitFor([&] { return !assistant->isVisible() && window.viewport()->hasFocus(); }),
          "Ctrl+J again hides the Assistant and returns to the canvas" + f.state());
}
// D-026, then the Model/Render tabs that the finished render adds.
void renderReopen(Fixture &f, const QString &files) {
    auto &window = f.window;
    paletteChoose(window, "Render…");
    auto *setup = window.findChild<QDialog *>("renderSetup");
    check(setup && QTest::qWaitForWindowExposed(setup), "Render with Blender opens");
    auto *render = setup->findChild<QPushButton *>("startRender");
    auto *jobs = setup->findChild<QPushButton *>("showRenderJobs");
    setup->findChild<QSpinBox *>("renderWidth")->setValue(64);
    setup->findChild<QSpinBox *>("renderHeight")->setValue(64);
    setup->findChild<QSpinBox *>("renderSamples")->setValue(1);
    setup->findChild<QLineEdit *>("blenderPath")->setText(fakeBlender(files));
    setup->findChild<QComboBox *>("renderBackend")->setCurrentText("CPU");
    auto *probe = setup->findChild<QPushButton *>("probeBlender");
    check(QTest::qWaitFor([&] { return probe->isEnabled(); }, 20000),
          "The automatic check of the absent Blender ends");
    probe->click();
    check(QTest::qWaitFor([&] { return render->isEnabled(); }, 20000),
          "Fake Blender check enables Render");
    setup->activateWindow();
    check(QTest::qWaitForWindowActive(setup), "Render with Blender is active");
    // What a job start does: Render is disabled while it has focus and Qt moves focus on.
    render->setFocus();
    check(QTest::qWaitFor([&] { return render->hasFocus(); }),
          "Render focused (active window " + identity(QApplication::activeWindow()) + ")" +
              f.state());
    render->setEnabled(false);
    check(QTest::qWaitFor([&] { return jobs->hasFocus(); }),
          "Disabling Render moves focus to Jobs… as during a job" + f.state());
    key(Qt::Key_Escape, Qt::NoModifier, true);
    check(QTest::qWaitFor([&] { return !setup->isVisible(); }), "Escape closes the sheet");
    render->setEnabled(true);
    activate(window);
    window.viewport()->setFocus();
    paletteChoose(window, "Render…");
    check(QTest::qWaitFor([&] { return setup->isVisible(); }), "Render with Blender reopens");
    check(setup->focusWidget() == render,
          "D-026: reopened Render with Blender focuses Render, not " +
              identity(setup->focusWidget()));
    check(QTest::qWaitForWindowActive(setup) && QTest::qWaitFor([&] { return render->hasFocus(); }),
          "Render has keyboard focus after reopening" + f.state());
    key(Qt::Key_Return, Qt::NoModifier, true);
    auto *jobsDialog = window.findChild<QDialog *>("renderJobs");
    check(!(jobsDialog && jobsDialog->isVisible()), "Enter does not open Jobs");
    auto *tabs = window.findChild<QTabWidget *>("modelTabs");
    check(QTest::qWaitFor(
              [&] {
                  return tabs->count() == 2 &&
                         tabs->currentWidget()->objectName() == "renderResult" &&
                         !window.renderPanel()->active();
              },
              30000),
          "Enter on the reopened sheet renders (tabs " + std::to_string(tabs->count()) +
              ", status " + window.renderPanel()->status().toStdString() + ", active " +
              std::to_string(window.renderPanel()->active()) + ")");
    check(!setup->isVisible(), "Render closes the sheet");
    activate(window);
    // Model and Render tabs by keyboard, starting on the result page's first button.
    auto *save = tabs->currentWidget()->findChild<QPushButton *>("saveRender");
    save->setFocus();
    check(QTest::qWaitFor([&] { return save->hasFocus(); }), "Result page focused");
    f.tabToBar(tabs->tabBar(), true);
    f.intoPageAndBack(tabs, "Render");
    key(Qt::Key_Left);
    f.intoPageAndBack(tabs, "Model");
    key(Qt::Key_Right);
    f.intoPageAndBack(tabs, "Render");
    key(Qt::Key_Left);
    check(tabs->currentIndex() == 0, "Left returns to the Model tab");
    f.checkCycle({tabs->tabBar(), window.findChild<QTabWidget *>("organizationTabs")->tabBar()});
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
    // Never probe an installed Blender: opening the sheet checks the saved path first.
    QSettings("SketchyUp", "SketchyUp")
        .setValue("render/blenderPath", QDir(files.path()).filePath("absent-blender"));
    Window window;
    try {
        window.resize(1280, 900);
        window.show();
        check(QTest::qWaitForWindowExposed(&window), "Window exposed");
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
        window.demo(); // A model to render.
        window.document().markSaved();
        auto *rail = window.findChild<QToolBar *>("toolRail");
        check(rail, "Tool rail exists");
        Fixture f{window, rail};
        if (qEnvironmentVariableIsSet("R084V_DEBUG"))
            QObject::connect(&app, &QApplication::focusChanged, [](QWidget *old, QWidget *now) {
                std::cerr << "  focus " << identity(old) << " -> " << identity(now) << '\n';
            });
        toolRail(f);
        std::cout << "Tool rail: one Tab stop; arrows, wrap, Home/End, Enter/Space passed\n";
        // Tab cycle with only the Model side tab (its bar is hidden) and no Render tab.
        f.checkCycle({window.findChild<QTabWidget *>("organizationTabs")->tabBar()});
        sideTabs(f);
        std::cout << "Side tabs: Ctrl+J, arrows, Tab into Model/Assistant pages and back passed\n";
        renderReopen(f, files.path());
        std::cout << "Render with Blender reopens on Render (D-026); Model/Render tabs passed\n";
        window.document().markSaved();
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
