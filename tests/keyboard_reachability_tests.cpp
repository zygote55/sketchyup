#include "app/collapsible_section.hpp"
#include "app/component_parameters_section.hpp"
#include "app/components_tab.hpp"
#include "app/file_operation.hpp"
#include "app/parameter_value_field.hpp"
#include "app/render_panel.hpp"
#include "app/sheet_evaluation.hpp"
#include "app/sheet_export_dialog.hpp"
#include "app/sheet_workspace.hpp"
#include "app/sheets_tray.hpp"
#include "app/surface_format.hpp"
#include "app/window.hpp"
#include "core/component_parameters.hpp"
#include "core/components.hpp"
#include "core/drawing_sheets.hpp"
#include "core/scenes.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <thread>
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
// UX_DESIGN §3.3: at 900 px the panel strip is one Tab stop and the Menu button another; Enter
// on a strip button opens the overlay sheet with focus inside, Esc returns to the button; and
// after the window is wide again the Tab cycle is the one it had before.
void narrowLayout(Fixture &f) {
    auto &window = f.window;
    auto *panels = window.findChild<QTabWidget *>("organizationTabs");
    const int tab = panels->currentIndex();
    const auto wide = f.cycle(false);
    window.resize(900, 900);
    check(QTest::qWaitFor([&] { return window.width() == 900; }), "Window narrowed to 900 px");
    QTest::qWait(50);
    auto *strip = window.findChild<QToolBar *>("panelStrip");
    auto *menu = window.findChild<QToolButton *>("menuButton");
    auto *overlay = window.findChild<QWidget *>("panelOverlay");
    check(strip && strip->isVisible() && menu && menu->isVisible() && overlay,
          "The strip and the Menu button show at 900 px");
    const auto narrow = f.cycle(false);
    int stripStops{}, menuStops{};
    for (auto *stop : narrow) {
        stripStops += strip->isAncestorOf(stop);
        menuStops += stop == menu;
    }
    std::string listed;
    for (auto *stop : narrow)
        listed += " " + identity(stop);
    if (stripStops != 1)
        for (auto *button : strip->findChildren<QToolButton *>())
            listed += "\n  strip " + identity(button) + " policy " +
                      std::to_string(int(button->focusPolicy())) + " visible " +
                      std::to_string(button->isVisible()) + " prev " +
                      identity(button->previousInFocusChain()) + " next " +
                      identity(button->nextInFocusChain());
    check(stripStops == 1 && menuStops == 1,
          "The narrow Tab cycle has one strip stop and the Menu button, found " +
              std::to_string(stripStops) + " and " + std::to_string(menuStops) + ":" + listed);
    // Tab reaches the strip's stop.
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Viewport focused");
    for (int i = 0; i < 40 && !strip->isAncestorOf(focused()); ++i)
        key(Qt::Key_Tab);
    check(strip->isAncestorOf(focused()), "Tab reaches the strip" + f.state());
    auto *stop = focused();
    key(Qt::Key_Return);
    check(QTest::qWaitFor([&] { return overlay->isVisible() && overlay->isAncestorOf(focused()); }),
          "Enter on the strip opens the sheet with focus inside" + f.state());
    // The sheet's controls keep the docked panel's order (its hidden tab row is no stop), so
    // Shift+Tab walks back inside it.
    auto *opened = focused();
    key(Qt::Key_Backtab, Qt::ShiftModifier);
    check(overlay->isAncestorOf(focused()) && focused() != opened,
          "Shift+Tab moves on inside the sheet from " + identity(opened) + f.state());
    key(Qt::Key_Escape);
    check(QTest::qWaitFor([&] { return !overlay->isVisible() && focused() == stop; }),
          "Esc closes the sheet and returns to the strip button" + f.state());
    window.resize(1280, 900);
    check(QTest::qWaitFor([&] { return window.width() == 1280; }), "Window wide again");
    QTest::qWait(50);
    // Info, opened from the strip, is the docked panel's tab now; the cycle is compared on
    // the tab that was open before.
    check(panels->tabText(panels->currentIndex()) == "Info", "The sheet's tab stays open");
    panels->setCurrentIndex(tab);
    QTest::qWait(30);
    const auto again = f.cycle(false);
    check(again == wide, "The wide Tab cycle is unchanged after the narrow round trip (" +
                             std::to_string(wide.size()) + " vs " + std::to_string(again.size()) +
                             " stops)");
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
// R091.h: a closed prompt or file-operation surface returns activation and keyboard focus to
// the Model workspace's viewport. Without a window manager (X11 under Xvfb) the window was
// left inactive with no focus widget after View › Field of view… and after an open or save
// that showed the progress surface.
void promptsReturnFocus(Fixture &f) {
    auto &window = f.window;
    auto *viewport = window.viewport();
    auto ready = [&] {
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active before the prompt");
        viewport->setFocus();
        check(QTest::qWaitFor([&] { return viewport->hasFocus(); }), "Viewport focus");
    };
    auto answer = [](bool accept) {
        auto *timer = new QTimer;
        QObject::connect(timer, &QTimer::timeout, [timer, accept] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || !dialog->isVisible() || dialog->objectName() == "fileOperationDialog")
                return;
            timer->stop();
            timer->deleteLater();
            accept ? dialog->accept() : dialog->reject();
        });
        timer->start(20);
    };
    auto returned = [&](const std::string &what) {
        check(QTest::qWaitFor([&] { return window.isActiveWindow() && viewport->hasFocus(); }),
              what + " returns activation and focus to the viewport" + f.state());
    };
    for (const bool accept : {true, false}) {
        ready();
        answer(accept);
        window.findChild<QAction *>("view.fov")->trigger();
        returned(accept ? "Accepting Field of view…" : "Cancelling Field of view…");
    }
    // The file-operation surface shows after 150 ms; this operation lasts longer.
    ready();
    bool shown = false;
    QTimer watch;
    QObject::connect(&watch, &QTimer::timeout, [&] {
        if (auto *surface = window.findChild<QDialog *>("fileOperationDialog");
            surface && surface->isVisible())
            shown = true;
    });
    watch.start(5);
    runFileOperation(&window, "Saving model…",
                     [] { std::this_thread::sleep_for(std::chrono::milliseconds(600)); });
    watch.stop();
    check(shown, "The file-operation surface was shown");
    returned("Closing the file-operation surface");
}
// R094.c (ADR 0166 §8): the Info tab's section headers, each parameter field, its reset
// button, Reset all and Edit definition… are in the Tab cycle and named.
void parameterSection(Fixture &f) {
    auto &window = f.window;
    auto &doc = window.document();
    const auto body = doc.addFace({{{40, 40, 0}, {41, 40, 0}, {41, 41, 0}, {40, 41, 0}}}, "Box");
    doc.extrude(body, doc.bodies().at(body)->surface.faces.begin()->first, 1);
    const auto created = createComponent(doc, body, "Parametric box");
    ComponentParameterSpec width;
    width.name = "width";
    width.label = "Width";
    width.expression = "1 m";
    width.min = .5;
    width.max = 2;
    defineComponentParameter(doc, created.definition, width);
    ComponentParameterSpec shelves;
    shelves.name = "shelves";
    shelves.label = "Shelves";
    shelves.type = ParameterType::Count;
    shelves.expression = "2";
    defineComponentParameter(doc, created.definition, shelves);
    overrideComponentParameters(doc, created.instance, {{"width", ParameterQuantity{1.5, {1, 0}}}});
    emit window.viewport()->changed();
    window.findChild<QAction *>(toolId(Viewport::Tool::Pan))->trigger();
    window.viewport()->setSelection(created.instance);
    auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
    tabs->setCurrentIndex(2);
    auto *section = window.findChild<ComponentParametersSection *>("componentParametersSection");
    check(QTest::qWaitFor([&] { return section->isVisible() && section->fields().size() == 2; }),
          "The parameters section shows for the placement");
    auto *info = window.findChild<QWidget *>("entityInfoSection");
    auto *widthRow = section->field("width");
    auto *shelvesRow = section->field("shelves");
    const std::vector<QWidget *> targets{static_cast<CollapsibleSection *>(info)->header(),
                                         section->header(),
                                         widthRow->control(),
                                         widthRow->resetButton(),
                                         shelvesRow->control(),
                                         section->resetAllButton(),
                                         section->editDefinitionButton()};
    std::set<QWidget *> reached;
    for (const bool backwards : {false, true})
        for (auto *stop : f.cycle(backwards))
            reached.insert(stop);
    for (auto *target : targets) {
        check(reached.contains(target), "Tab or Shift+Tab reaches " + identity(target));
        auto *accessible = QAccessible::queryAccessibleInterface(target);
        check(accessible && !accessible->text(QAccessible::Name).isEmpty(),
              "Accessible name for " + identity(target));
    }
    check(widthRow->control()->accessibleName() == "Width" &&
              widthRow->resetButton()->accessibleName().startsWith("Reset Width to the default, "),
          "Fields are named by their label and reset buttons say what they reset");
    // Tab goes field → its reset → next field.
    widthRow->control()->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return widthRow->control()->hasFocus(); }), "Width focused");
    key(Qt::Key_Tab);
    f.expectFocus(widthRow->resetButton(), "Tab from Width");
    key(Qt::Key_Tab);
    f.expectFocus(shelvesRow->control(), "Tab from the reset button");
    window.viewport()->setSelection(0);
    tabs->setCurrentIndex(0);
    window.document().markSaved();
}
// R094.h (ADR 0166 §8, §10): the Components tab (R094.f). View › Components shows the tab and
// focuses its Show filter; Tab from the Tray's tab bar enters the page and Shift+Tab returns. The
// section header, Show and the cards are Tab stops with names, and the cards are one stop (arrows
// move between them). Enter on a card opens the values form, whose fields, Use defaults and Cancel
// are Tab stops; Esc ends placement without losing keyboard focus.
void componentsTab(Fixture &f) {
    auto &window = f.window;
    auto *tabs = window.findChild<QTabWidget *>("organizationTabs");
    auto *tab = window.findChild<ComponentsTab *>("componentsTab");
    check(tab && tabs->indexOf(tab) == 12, "Components is the Tray's tab 12");
    window.findChild<QAction *>(toolId(Viewport::Tool::Pan))->trigger();
    window.findChild<QAction *>("view.components")->trigger();
    check(QTest::qWaitFor([&] { return tabs->currentWidget() == tab; }),
          "View › Components shows the tab");
    f.expectFocus(tab->filter(), "View › Components");
    tabs->tabBar()->setFocus(Qt::TabFocusReason);
    f.expectFocus(tabs->tabBar(), "Focusing the Tray's tab bar");
    f.intoPageAndBack(tabs, "Components");
    // The page's stops: Tab from the bar until focus leaves the page, then Shift+Tab back to it.
    auto pageStops = [&] {
        std::vector<QWidget *> stops;
        tabs->tabBar()->setFocus(Qt::TabFocusReason);
        f.expectFocus(tabs->tabBar(), "The page walk starts at the tab bar");
        key(Qt::Key_Tab);
        for (int i = 0; i < 80 && tab->isAncestorOf(focused()); ++i) {
            stops.push_back(focused());
            key(Qt::Key_Tab);
        }
        check(!tab->isAncestorOf(focused()), "Tab leaves the Components page" + f.state());
        key(Qt::Key_Backtab, Qt::ShiftModifier);
        for (int i = 0; i < 80 && !tabs->tabBar()->hasFocus(); ++i) {
            check(tab->isAncestorOf(focused()),
                  "Shift+Tab stays in the Components page until its tab bar" + f.state());
            stops.push_back(focused());
            key(Qt::Key_Backtab, Qt::ShiftModifier);
        }
        check(tabs->tabBar()->hasFocus(), "Shift+Tab returns to the tab bar" + f.state());
        return stops;
    };
    auto expectStops = [&](const std::vector<QWidget *> &stops,
                           const std::vector<QWidget *> &targets) {
        for (auto *target : targets) {
            check(std::find(stops.begin(), stops.end(), target) != stops.end(),
                  "Tab or Shift+Tab reaches " + identity(target));
            auto *accessible = QAccessible::queryAccessibleInterface(target);
            check(accessible && !accessible->text(QAccessible::Name).isEmpty(),
                  "Accessible name for " + identity(target));
        }
    };
    auto *cards = tab->cards();
    auto *header =
        static_cast<CollapsibleSection *>(tab->findChild<QWidget *>("componentsSection"))->header();
    auto stops = pageStops();
    expectStops(stops, {header, tab->filter(), cards});
    // One stop each way: the forward walk and the backward walk each pass the cards once.
    check(std::count(stops.begin(), stops.end(), cards) == 2,
          "The cards are one Tab stop, visited " +
              std::to_string(std::count(stops.begin(), stops.end(), cards)) +
              " times in two walks");
    check(cards->model()->rowCount() == 6, "The six assemblies are listed");
    // Arrows move between the cards; Enter starts placing and opens the values form.
    cards->setFocus(Qt::TabFocusReason);
    f.expectFocus(cards, "Focusing the cards");
    key(Qt::Key_Home);
    key(Qt::Key_Right);
    check(cards->currentIndex().row() == 1, "Right moves to the next card");
    key(Qt::Key_Home);
    key(Qt::Key_Return);
    check(QTest::qWaitFor([&] {
              return window.viewport()->assemblyPlacing() && tab->valuesSection()->isVisible();
          }),
          "Enter on a card opens the values form");
    std::vector<QWidget *> form;
    for (auto *field : tab->fields())
        form.push_back(field->control());
    form.push_back(tab->useDefaultsButton());
    form.push_back(tab->cancelButton());
    check(form.size() > 2, "The values form has fields");
    stops = pageStops();
    expectStops(stops, form);
    // Esc in the form ends placement; focus stays in the tab.
    tab->cancelButton()->setFocus(Qt::TabFocusReason);
    f.expectFocus(tab->cancelButton(), "Focusing Cancel");
    key(Qt::Key_Escape);
    check(QTest::qWaitFor([&] { return !window.viewport()->assemblyPlacing(); }) &&
              !tab->valuesSection()->isVisible(),
          "Esc ends placement and hides the values form");
    check(QTest::qWaitFor([] { return focused() != nullptr; }) && focused()->window() == &window,
          "Focus stays in the model window" + f.state());
    tabs->setCurrentIndex(0);
    window.document().markSaved();
}
// R091.e: the Sheets workspace (ADR 0164 §4). One Tab stop for the sheet rail, the F6 order
// search, sheet rail, canvas, page tabs, Tray (Pages), Assistant, Measurements, and the page
// tabs and Pages tree reached by Tab.
void sheetsWorkspace() {
    Window window;
    window.resize(1280, 900);
    window.show();
    check(QTest::qWaitForWindowExposed(&window), "Sheets window exposed");
    activate(window);
    auto &doc = window.document();
    SceneSnapshot plan;
    plan.camera = SceneCamera{};
    plan.camera->pitch = 90;
    plan.camera->orthographic = true;
    const auto scene = createScene(doc, "Plan", plan);
    const auto first = createDrawingSheet(doc, "Ground floor", {420, 297}, "A-101", 10);
    const auto second = createDrawingSheet(doc, "Elevations", {420, 297}, "A-201", 10);
    DrawingViewRecord view;
    view.name = "Plan";
    view.sheet = first;
    view.source = {DrawingViewSourceKind::Scene, scene};
    view.frame = {20, 20, 150, 100};
    view.scaleDenominator = 50;
    createDrawingView(doc, view);
    // R091.f: a second view whose scene is deleted later, for Choose scene….
    SceneSnapshot front;
    front.camera = SceneCamera{};
    front.camera->yaw = -90;
    front.camera->pitch = 0;
    front.camera->orthographic = true;
    const auto frontScene = createScene(doc, "Front", front);
    view.name = "Front";
    view.source = {DrawingViewSourceKind::Scene, frontScene};
    view.frame = {200, 20, 150, 100};
    const auto frontView = createDrawingView(doc, view);
    emit window.viewport()->changed();
    window.viewport()->setFocus();
    check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Model focus");
    key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    auto *canvas = window.findChild<QWidget *>("sheetCanvas");
    auto *rail = window.findChild<QToolBar *>("sheetRail");
    auto *tabs = window.findChild<QTabBar *>("sheetPageTabs");
    auto *pages = window.findChild<QTreeWidget *>("sheetPagesTree");
    check(canvas && rail && tabs && pages && canvas->isVisible(), "Sheets workspace shown");
    check(QTest::qWaitFor([&] { return canvas->hasFocus(); }), "Canvas focused");
    auto toolButton = [&](const char *id) {
        return qobject_cast<QToolButton *>(rail->widgetForAction(window.findChild<QAction *>(id)));
    };
    // F6 and Shift+F6 regions.
    const std::vector<QWidget *> order{tabs, pages,
                                       window.findChild<QPushButton *>("commandSearch"),
                                       toolButton("sheet.tool.select"), canvas};
    for (auto *expected : order) {
        key(Qt::Key_F6);
        check(QTest::qWaitFor([&] { return focused() == expected; }),
              "F6 in Sheets reaches " + identity(expected) + ", got " + identity(focused()));
    }
    for (auto it = order.rbegin() + 1; it != order.rend(); ++it) {
        key(Qt::Key_F6, Qt::ShiftModifier);
        check(QTest::qWaitFor([&] { return focused() == *it; }),
              "Shift+F6 in Sheets reaches " + identity(*it) + ", got " + identity(focused()));
    }
    // The rail is one Tab stop; arrows move inside it and Enter picks a tool.
    key(Qt::Key_F6, Qt::ShiftModifier); // From the page tabs back to the canvas,
    key(Qt::Key_F6, Qt::ShiftModifier); // then to the rail's stop.
    check(QTest::qWaitFor([&] { return focused() == toolButton("sheet.tool.select"); }),
          "Rail stop is the active sheet tool, got " + identity(focused()));
    key(Qt::Key_Down);
    check(focused() == toolButton("sheet.tool.view"), "Down moves to Model view in the sheet rail");
    // R091.g: the page-item tools follow, then Move past a separator.
    for (const auto *id : {"sheet.tool.line", "sheet.tool.rectangle", "sheet.tool.text",
                           "sheet.tool.label", "sheet.tool.dimension", "sheet.tool.move"}) {
        key(Qt::Key_Down);
        check(focused() == toolButton(id),
              std::string("Down moves to ") + id + " in the sheet rail");
    }
    key(Qt::Key_Down);
    check(focused() == toolButton("sheet.tool.pan"), "Down moves to Pan in the sheet rail");
    key(Qt::Key_Return);
    check(window.findChild<QAction *>("sheet.tool.pan")->isChecked(), "Enter picks Pan");
    // Tab cycle from the canvas back to it.
    canvas->setFocus(Qt::OtherFocusReason);
    check(QTest::qWaitFor([&] { return canvas->hasFocus(); }), "Canvas focus for the Tab cycle");
    int railStops = 0;
    bool tabsReached = false, pagesReached = false, back = false;
    QWidget *previous{};
    for (int i = 0; i < 400 && !back; ++i) {
        key(Qt::Key_Tab);
        auto *now = focused();
        check(now->window() == &window, "Tab stays in the window");
        check(!tabScrollButton(now), "Tab never stops on a tab scroll button");
        if (now == canvas) {
            back = true;
            break;
        }
        if (now != previous && rail->isAncestorOf(now))
            ++railStops;
        tabsReached |= now == tabs;
        pagesReached |= now == pages;
        previous = now;
    }
    check(back && railStops == 1 && tabsReached && pagesReached,
          "The Sheets Tab cycle has one sheet-rail stop and reaches the page tabs and Pages tree "
          "(rail stops " +
              std::to_string(railStops) + ")");
    // Page tabs switch with arrows; the Pages tree moves with arrows.
    tabs->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return tabs->hasFocus(); }), "Page tabs focus");
    key(Qt::Key_Right);
    check(tabs->currentIndex() == 1, "Right shows the next page");
    pages->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return pages->hasFocus(); }), "Pages tree focus");
    key(Qt::Key_Home);
    check(pages->currentItem() && pages->currentItem()->text(0) == "A-101",
          "Home moves to the first page in the Pages tree");
    key(Qt::Key_Down);
    check(pages->currentItem() && pages->currentItem() != pages->topLevelItem(0),
          "Down moves through the Pages tree");
    (void)second;
    // R091.f: every new control is reachable by Tab from the canvas.
    auto *sheetCanvas = window.sheetWorkspace()->canvas();
    auto tabReaches = [&](const std::vector<QWidget *> &targets, const std::string &context) {
        sheetCanvas->setFocus(Qt::OtherFocusReason);
        check(QTest::qWaitFor([&] { return sheetCanvas->hasFocus(); }), context + ": canvas focus");
        std::set<QWidget *> reached;
        std::string visited;
        for (int i = 0; i < 400; ++i) {
            key(Qt::Key_Tab);
            if (focused() == sheetCanvas)
                break;
            if (reached.insert(focused()).second)
                visited += " " + identity(focused());
        }
        for (auto *target : targets) {
            std::string state;
            if (target) {
                const auto at = target->mapTo(target->window(), QPoint());
                state = " (visible " + std::to_string(target->isVisible()) + ", enabled " +
                        std::to_string(target->isEnabled()) + ", at " + std::to_string(at.x()) +
                        "," + std::to_string(at.y()) + " size " + std::to_string(target->width()) +
                        "x" + std::to_string(target->height()) + ")";
            }
            check(target && reached.contains(target),
                  context + ": Tab reaches " + identity(target) + state + "; visited:" + visited);
        }
    };
    tabs->setCurrentIndex(0);
    sheetCanvas->setFocus(Qt::OtherFocusReason);
    check(QTest::qWaitFor([&] { return sheetCanvas->hasFocus(); }), "Canvas focus for R091.f");
    window.findChild<QAction *>("sheet.tool.select")->trigger();
    key(Qt::Key_Tab);
    check(sheetCanvas->selectedView() != 0 && sheetCanvas->hasFocus(),
          "With Select, Tab selects a view and the canvas keeps focus");
    // Entity info: title, scale, style, Edit view and Show scene in model (Pan lets Tab leave).
    window.findChild<QAction *>("sheet.tool.pan")->trigger();
    tabReaches({window.findChild<QWidget *>("sheetInfoTitle"),
                window.findChild<QWidget *>("sheetInfoScale"),
                window.findChild<QWidget *>("sheetInfoStyle"),
                window.findChild<QWidget *>("sheetInfoEditView"),
                window.findChild<QWidget *>("sheetInfoShowScene")},
               "Entity info");
    // The New model view panel.
    window.findChild<QAction *>("sheet.tool.view")->trigger();
    auto *panel = window.sheetsTray()->placePanel();
    tabReaches({panel->findChild<QWidget *>("sheetPlaceModel"), panel->scenes()->checkedButton(),
                panel->scaleChoice(), panel->placeButton(), panel->cancelButton()},
               "New model view");
    // Arrows move between the scene radio buttons.
    panel->scenes()->checkedButton()->setFocus(Qt::TabFocusReason);
    const auto firstScene = panel->scene();
    key(Qt::Key_Down);
    check(panel->scene() != firstScene, "Down chooses the next scene in the panel");
    // Edit-view: the banner's Save as new scene… and Done follow the canvas.
    window.findChild<QAction *>("sheet.tool.select")->trigger();
    sheetCanvas->setSelectedView(sheetCanvas->views().front());
    sheetCanvas->setFocus(Qt::OtherFocusReason);
    key(Qt::Key_Return);
    check(sheetCanvas->editView() != 0, "Enter opens edit-view");
    tabReaches({sheetCanvas->saveSceneButton(), sheetCanvas->doneButton()}, "Edit-view banner");
    sheetCanvas->setFocus(Qt::OtherFocusReason);
    key(Qt::Key_Escape);
    check(sheetCanvas->editView() == 0, "Esc leaves edit-view");
    // Choose scene… on a broken view.
    eraseScene(doc, frontScene);
    emit window.viewport()->changed();
    check(QTest::qWaitFor(
              [&] {
                  const auto status = window.sheetEvaluations()->status(frontView);
                  return status && status->state == DrawingViewState::Broken;
              },
              20000),
          "The Front view is broken");
    sheetCanvas->setSelectedView(frontView);
    window.findChild<QAction *>("sheet.tool.pan")->trigger();
    tabReaches({window.findChild<QWidget *>("sheetInfoChooseScene")}, "Choose scene…");
    // R091.g: page items. Insert › Title block; its Entity info fields and its Pages row are
    // reachable from the keyboard, and Enter on the row edits it in place.
    window.findChild<QAction *>("sheet.tool.select")->trigger();
    sheetCanvas->setFocus(Qt::OtherFocusReason);
    window.findChild<QAction *>("sheet.insert.titleBlock")->trigger();
    const auto block = sheetCanvas->selectedItem();
    check(block != 0, "Insert › Title block selects the new title block");
    window.findChild<QAction *>("sheet.tool.pan")->trigger();
    tabReaches({window.findChild<QWidget *>("sheetItemTitle_project"),
                window.findChild<QWidget *>("sheetItemTitle_drawingTitle"),
                window.findChild<QWidget *>("sheetItemSheetNumber"),
                window.findChild<QWidget *>("sheetItemTitle_date"),
                window.findChild<QWidget *>("sheetItemTitle_revision"),
                window.findChild<QWidget *>("sheetItemFont"),
                window.findChild<QWidget *>("sheetItemSize")},
               "Entity info · Title block");
    window.findChild<QAction *>("sheet.tool.select")->trigger();
    check(sheetCanvas->selectedItem() == block, "The title block stays selected");
    pages->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return pages->hasFocus(); }), "Pages tree focus for items");
    auto *row = pages->currentItem();
    check(row && row->data(0, Qt::UserRole + 1).toULongLong() == block &&
              row->text(1) == "Title block",
          "Pages shows the selected item's row as current");
    key(Qt::Key_Up);
    key(Qt::Key_Down);
    check(pages->currentItem() &&
              pages->currentItem()->data(0, Qt::UserRole + 1).toULongLong() == block &&
              sheetCanvas->selectedItem() == block,
          "Arrows reach the item's row, which selects it on the page");
    key(Qt::Key_Return);
    check(sheetCanvas->textEditing() && sheetCanvas->textEditField() == "project",
          "Enter on the row edits the title block in place");
    key(Qt::Key_Escape);
    check(!sheetCanvas->textEditing() && QTest::qWaitFor([&] { return sheetCanvas->hasFocus(); }),
          "Esc ends editing on the canvas");
    // R092.b: File › Export sheets… from the palette. Every control is reachable from the
    // keyboard: Tab reaches each one, the page choice is one Tab stop whose arrows choose, and
    // Esc returns to the canvas.
    {
        bool seen{};
        std::exception_ptr failure;
        QTimer watch;
        QObject::connect(&watch, &QTimer::timeout, &window, [&] {
            auto *dialog = dynamic_cast<SheetExportDialog *>(QApplication::activeModalWidget());
            if (seen || !dialog || !dialog->isVisible())
                return;
            seen = true;
            try {
                dialog->activateWindow();
                check(QTest::qWaitForWindowActive(dialog), "Export dialog active");
                check(QTest::qWaitFor([&] { return !dialog->checking() && dialog->preflight(); },
                                      20000),
                      "Export preflight settles");
                std::set<QString> reached;
                std::string visited;
                for (int i = 0; i < 30; ++i) {
                    check(focused() && dialog->isAncestorOf(focused()),
                          "Tab stays in the export dialog");
                    if (reached.insert(focused()->objectName()).second)
                        visited += " " + identity(focused());
                    key(Qt::Key_Tab);
                }
                for (const auto *name :
                     {"sheetExportAll", "sheetExportRangeText", "sheetExportChoose",
                      "sheetExportGoToView", "sheetExportAllowBroken", "sheetExportCancel"})
                    check(reached.contains(name), std::string("Tab reaches ") + name +
                                                      " in the export dialog; visited:" + visited);
                dialog->findChild<QWidget *>("sheetExportAll")->setFocus(Qt::TabFocusReason);
                for (const auto *name : {"sheetExportCurrent", "sheetExportRange"}) {
                    key(Qt::Key_Right);
                    auto *radio = qobject_cast<QRadioButton *>(focused());
                    check(radio && radio->objectName() == name && radio->isChecked(),
                          std::string("Right chooses ") + name + ", got " + identity(focused()));
                }
                key(Qt::Key_Escape, {}, true);
            } catch (...) {
                failure = std::current_exception();
                dialog->reject();
            }
        });
        watch.start(20);
        sheetCanvas->setFocus(Qt::OtherFocusReason);
        check(QTest::qWaitFor([&] { return sheetCanvas->hasFocus(); }), "Canvas for Export");
        paletteChoose(window, "Export sheets…");
        watch.stop();
        if (failure)
            std::rethrow_exception(failure);
        check(seen, "The palette opened File › Export sheets…");
        check(QTest::qWaitFor([&] { return window.isActiveWindow() && sheetCanvas->hasFocus(); }),
              "Esc in the export dialog returns to the canvas, got " + identity(focused()));
    }
    key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Model &&
              QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }),
          "Ctrl+Alt+S returns to Model and its viewport");
    window.document().markSaved();
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
        narrowLayout(f);
        std::cout << "Narrow layout: one strip stop, Menu button, Enter into the sheet, Esc back, "
                     "Tab cycle kept after the round trip passed\n";
        renderReopen(f, files.path());
        std::cout << "Render with Blender reopens on Render (D-026); Model/Render tabs passed\n";
        promptsReturnFocus(f);
        std::cout << "Field of view… and the file-operation surface return focus to the "
                     "viewport passed\n";
        parameterSection(f);
        std::cout << "Info tab sections and placement parameters in the Tab cycle passed\n";
        componentsTab(f);
        std::cout << "Components tab: header, Show, one cards stop, values form and Esc passed\n";
        window.document().markSaved();
        window.hide();
        sheetsWorkspace();
        std::cout << "Sheets workspace: one sheet-rail stop, F6 regions, page tabs and Pages tree "
                     "passed\n";
    } catch (const std::exception &error) {
        window.document().markSaved();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
