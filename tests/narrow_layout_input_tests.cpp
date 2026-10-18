// UX_DESIGN §3.3: the narrow layout. Below 1100 px the side panel collapses to an icon strip
// that opens the Model panel tabs, the Assistant and More panels in an overlay sheet anchored
// to the right edge, and the menu bar becomes one Menu button. The real window at 1600, 1200,
// 1000, 900 and 780 logical pixels; the strip's accessible names and tooltips; opening and
// closing the sheet by click, shortcut, Esc and a workspace click, with focus moving in and
// back; resizing the sheet (the R094.a tabbed width); the Menu button's menus and the
// shortcuts; state across breakpoints; and the strip, its icons and the sheet in both themes.
// Below 800 px the Model tool rail shows one group of tools behind a group button (§3.3).
#include "app/surface_format.hpp"
#include "app/theme.hpp"
#include "app/window.hpp"
#include <QAccessible>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <set>
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
           widget->objectName().toStdString();
}
double luminance(const QColor &color) {
    auto linear = [](double v) {
        return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
    };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) +
           .0722 * linear(color.blueF());
}
double contrast(const QColor &a, const QColor &b) {
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}
bool near(const QColor &a, const QColor &b, int tolerance = 2) {
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}
int stored(const char *key) {
    QSettings preferences("SketchyUp", "SketchyUp");
    preferences.sync();
    return preferences.value(key, -1).toInt();
}
QString accessibleText(QWidget *widget, QAccessible::Text text) {
    auto *accessible = QAccessible::queryAccessibleInterface(widget);
    return accessible ? accessible->text(text) : QString();
}
struct Fixture {
    Window &window;
    QSplitter *splitter{};
    QTabWidget *side{}, *organization{};
    QToolBar *strip{};
    QWidget *overlay{}, *handle{}, *tray{};
    QToolButton *menuButton{};
    QLineEdit *measurements{};
    explicit Fixture(Window &w) : window(w) {
        splitter = window.findChild<QSplitter *>("sidebarSplitter");
        side = window.findChild<QTabWidget *>("assistantSideTabs");
        organization = window.findChild<QTabWidget *>("organizationTabs");
        strip = window.findChild<QToolBar *>("panelStrip");
        overlay = window.findChild<QWidget *>("panelOverlay");
        handle = window.findChild<QWidget *>("overlayHandle");
        tray = window.findChild<QWidget *>("tray");
        menuButton = window.findChild<QToolButton *>("menuButton");
        measurements = window.findChild<QLineEdit *>("measurements");
        check(splitter && side && organization && strip && overlay && handle && tray &&
                  menuButton && measurements,
              "The narrow layout's widgets exist");
    }
    void settle() {
        QTest::qWait(40);
        QCoreApplication::processEvents();
    }
    void resize(int width) {
        window.setFixedSize(width, 800);
        check(QTest::qWaitFor([&] { return window.width() == width; }),
              "Window resized to " + std::to_string(width));
        settle();
    }
    void activate() {
        window.activateWindow();
        check(QTest::qWaitForWindowActive(&window), "Window active");
    }
    void focusViewport() {
        activate();
        window.viewport()->setFocus(Qt::OtherFocusReason);
        check(QTest::qWaitFor([&] { return window.viewport()->hasFocus(); }), "Viewport focused");
    }
    void key(Qt::Key code, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        check(QTest::qWaitFor([] { return focused() != nullptr; }), "A widget has focus");
        QTest::keyClick(focused(), code, modifiers);
        settle();
    }
    QAction *action(const QString &id) const {
        auto *found = window.findChild<QAction *>(id);
        check(found != nullptr, "Action " + id.toStdString() + " exists");
        return found;
    }
    QToolButton *button(const QString &id) const {
        auto *entry = action("panelStrip." + id);
        auto *found = qobject_cast<QToolButton *>(strip->widgetForAction(entry));
        check(found != nullptr, "Strip button " + id.toStdString() + " exists");
        return found;
    }
    void click(QWidget *widget) {
        QTest::mouseClick(widget, Qt::LeftButton, {}, widget->rect().center());
        settle();
    }
    bool open() const { return overlay->isVisible(); }
    bool focusInside() const {
        auto *now = focused();
        return now && overlay->isAncestorOf(now);
    }
    std::string state() const {
        return " [width " + std::to_string(window.width()) + ", overlay " +
               (overlay->isVisible() ? "open" : "closed") + ", focus " + identity(focused()) +
               ", side tab " +
               (side->currentWidget() ? side->currentWidget()->objectName().toStdString() : "") +
               "]";
    }
    // The checked strip buttons.
    std::set<QString> checked() const {
        std::set<QString> result;
        for (auto *entry : strip->actions())
            if (entry->isChecked())
                result.insert(entry->objectName().section('.', 1));
        return result;
    }
    void expectOpen(const QString &id, const std::string &where) {
        check(QTest::qWaitFor([&] { return open() && focusInside(); }),
              where + ": the sheet opens with focus inside" + state());
        check(checked() == std::set<QString>{id},
              where + ": only " + id.toStdString() + " is checked");
        // Anchored to the workspace's right edge, beside the strip, over the workspace.
        const auto area = splitter->geometry();
        check(overlay->geometry().right() == area.right() &&
                  overlay->geometry().top() == area.top() &&
                  overlay->geometry().height() == area.height() &&
                  overlay->geometry().left() > area.left(),
              where + ": the sheet is anchored to the workspace's right edge");
        check(splitter->indexOf(side) < 0 && overlay->isAncestorOf(side),
              where + ": the side panel is in the sheet");
    }
    void expectClosed(QWidget *returned, const std::string &where) {
        check(QTest::qWaitFor([&] { return !open(); }), where + ": the sheet closes" + state());
        check(checked().empty(), where + ": no strip button is checked");
        if (returned)
            check(QTest::qWaitFor([&] { return focused() == returned; }),
                  where + ": focus returns to " + identity(returned) + state());
    }
};
void layoutAt(Fixture &f, int width) {
    f.resize(width);
    auto &window = f.window;
    const bool narrow = width < 1100;
    const auto percent = 100;
    (void)percent;
    check(f.strip->isVisible() == narrow,
          "Strip shows only below 1100 px at " + std::to_string(width));
    check(f.menuButton->isVisible() == narrow && window.menuBar()->isVisible() == !narrow,
          "Menu button replaces the menu bar only below 1100 px at " + std::to_string(width));
    check(!f.open(), "The sheet starts closed at " + std::to_string(width));
    auto *rail = window.findChild<QToolBar *>("toolRail");
    check(rail->isVisible() && rail->width() == 78,
          "The Model tool rail is unchanged (78 px, text) at " + std::to_string(width));
    check(f.measurements->isVisible() && window.rect().contains(f.measurements->mapTo(
                                             &window, f.measurements->rect().bottomRight())),
          "Measurements stays on screen at " + std::to_string(width));
    auto *status = window.findChild<QLabel *>("statusMessage");
    check(status && status->isVisible() && status->width() > 0,
          "The status instruction stays (truncating) at " + std::to_string(width));
    auto *chip = window.findChild<QPushButton *>("renderStatusChip");
    check(chip && chip->parentWidget()->objectName() == "footer",
          "The job chip stays in the footer at " + std::to_string(width));
    if (narrow) {
        check(f.strip->width() == 46,
              "The strip is 46 px wide, got " + std::to_string(f.strip->width()));
        const auto right = f.strip->mapTo(&window, QPoint(f.strip->width(), 0)).x();
        check(right == window.width(), "The strip is at the window's right edge");
        for (auto *entry : f.strip->actions()) {
            auto *button = f.strip->widgetForAction(entry);
            check(button && button->isVisible() && button->size() == QSize(36, 36),
                  "Strip buttons are 36 px squares (" + entry->text().toStdString() + ")");
        }
        check(f.splitter->indexOf(f.side) < 0, "The side panel left the splitter");
        check(f.splitter->count() == 1 && f.splitter->widget(0)->width() == f.splitter->width(),
              "The workspace takes the splitter's whole width at " + std::to_string(width));
        check(window.viewport()->width() >= 300, "The viewport stays usable");
    } else {
        check(f.splitter->indexOf(f.side) == 1 && f.side->isVisible(),
              "The side panel is a splitter column at " + std::to_string(width));
    }
    const char *expected = width < 800    ? "compact"
                           : width < 1100 ? "standard"
                           : width < 1500 ? "expanded"
                                          : "wide";
    check(window.property("layoutClass").toString() == expected,
          "Layout class at " + std::to_string(width));
}
void accessibility(Fixture &f) {
    const std::vector<std::pair<QString, QString>> names{
        {"info", "Info"},       {"outliner", "Outliner"}, {"materials", "Materials"},
        {"tags", "Tags"},       {"history", "History"},   {"assistant", "Assistant"},
        {"tray", "More panels"}};
    const std::map<QString, QString> tips{{"assistant", "Assistant (Ctrl+J)"},
                                          {"tray", "More panels (Ctrl+Shift+T)"}};
    int order = 0;
    QWidget *previous{};
    for (const auto &[id, name] : names) {
        auto *button = f.button(id);
        check(accessibleText(button, QAccessible::Name) == name,
              "Strip button name is " + name.toStdString() + ", got " +
                  accessibleText(button, QAccessible::Name).toStdString());
        const auto tip = tips.contains(id) ? tips.at(id) : name;
        check(button->toolTip() == tip && accessibleText(button, QAccessible::Description) == tip,
              "Strip button tooltip is " + tip.toStdString() + ", got " +
                  button->toolTip().toStdString());
        // Top to bottom in the mockup's order.
        if (previous)
            check(button->y() > previous->y(), "Strip order " + std::to_string(order));
        previous = button;
        ++order;
    }
    check(accessibleText(f.strip, QAccessible::Name) == "Panels", "The strip is named Panels");
    check(accessibleText(f.menuButton, QAccessible::Name) == "Menu" &&
              f.menuButton->toolTip() == "Menu",
          "The menu button is named Menu");
    // A rebinding shows in the tooltip, and a reset removes it again.
    f.action("view.outliner")->setShortcut(QKeySequence("Ctrl+Alt+O"));
    const auto rebound = QKeySequence("Ctrl+Alt+O").toString(QKeySequence::NativeText);
    check(QTest::qWaitFor(
              [&] { return f.button("outliner")->toolTip() == "Outliner (" + rebound + ")"; }) &&
              accessibleText(f.button("outliner"), QAccessible::Description) ==
                  "Outliner (" + rebound + ")",
          "The tooltip names a rebound shortcut");
    f.action("view.outliner")->setShortcut({});
    check(QTest::qWaitFor([&] { return f.button("outliner")->toolTip() == "Outliner"; }),
          "The tooltip follows a cleared binding");
    // Exactly one handle is named "Resize sidebar": the sheet's edge, only while it shows.
    auto named = [&] {
        int count = 0;
        for (auto *widget : f.window.findChildren<QWidget *>())
            count += accessibleText(widget, QAccessible::Name) == "Resize sidebar";
        return count;
    };
    check(named() == 0 && accessibleText(f.handle, QAccessible::Name).isEmpty(),
          "No sidebar handle is named while the sheet is closed");
    f.focusViewport();
    f.click(f.button("info"));
    auto *handle = QAccessible::queryAccessibleInterface(f.handle);
    check(f.open() && handle && handle->role() == QAccessible::Splitter &&
              handle->text(QAccessible::Name) == "Resize sidebar" && named() == 1,
          "The open sheet's edge is the one named R094.a splitter handle");
    f.key(Qt::Key_Escape);
    check(!f.open() && named() == 0, "Closing the sheet unnames its edge");
    f.resize(1200);
    check(named() == 1 &&
              accessibleText(f.splitter->handle(1), QAccessible::Name) == "Resize sidebar",
          "Docked, the splitter's edge is the one named handle");
    f.resize(900);
}
void openAndClose(Fixture &f) {
    auto &window = f.window;
    auto *viewport = window.viewport();
    // Click an icon: the sheet opens on its tab, focus moves in; clicking it again closes.
    f.focusViewport();
    f.click(f.button("info"));
    f.expectOpen("info", "Info click");
    check(f.side->currentWidget() == f.tray &&
              f.organization->tabText(f.organization->currentIndex()) == "Info",
          "Info opens the Model panel on its Info tab");
    // A strip panel's sheet has its name as the heading and no tab row (the strip is the row).
    auto *heading = window.findChild<QLabel *>("overlayHeading");
    QLabel *trayHeading{};
    for (auto *label : window.findChildren<QLabel *>("section"))
        if (label->text() == "MODEL")
            trayHeading = label;
    check(heading && heading->isVisible() && heading->text() == "INFO" &&
              heading->accessibleName() == "Info" && !trayHeading->isVisible() &&
              !f.organization->tabBar()->isVisible(),
          "The Info sheet is headed INFO, without the Tray's heading and tab row");
    check(f.overlay->width() == 340 + 6,
          "The sheet is the default tabbed width (340 px) plus the handle, got " +
              std::to_string(f.overlay->width()));
    f.click(f.button("info"));
    f.expectClosed(viewport, "Info click again");
    // Each of the five tab buttons opens its tab.
    for (const auto &[id, tab] :
         std::vector<std::pair<QString, QString>>{{"outliner", "Outliner"},
                                                  {"materials", "Materials"},
                                                  {"tags", "Tags"},
                                                  {"history", "History"}}) {
        f.click(f.button(id));
        f.expectOpen(id, tab.toStdString() + " click");
        check(f.organization->tabText(f.organization->currentIndex()) == tab,
              tab.toStdString() + " opens its tab");
    }
    // Another icon while open switches the sheet; Esc closes and returns focus.
    f.key(Qt::Key_Escape);
    f.expectClosed(viewport, "Esc");
    // Esc from the strip itself also closes.
    f.click(f.button("tags"));
    f.expectOpen("tags", "Tags click");
    f.button("tags")->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return f.button("tags")->hasFocus(); }), "Strip button focused");
    f.key(Qt::Key_Escape);
    f.expectClosed(nullptr, "Esc on the strip");
    // Esc in the Outliner at the model's top level closes the sheet.
    f.focusViewport();
    f.click(f.button("outliner"));
    f.expectOpen("outliner", "Outliner click");
    auto *tree = window.findChild<QTreeWidget *>("outlinerTree");
    if (!tree)
        for (auto *candidate : f.organization->currentWidget()->findChildren<QTreeWidget *>())
            if (candidate->isVisible())
                tree = candidate;
    check(tree, "The Outliner tree exists");
    tree->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return tree->hasFocus(); }), "Outliner focused");
    f.key(Qt::Key_Escape);
    f.expectClosed(viewport, "Esc in the Outliner");
    // A click in the viewport closes and does not reach the model.
    f.click(f.button("materials"));
    f.expectOpen("materials", "Materials click");
    const auto selection = viewport->selectionState().entities();
    const auto revision = window.document().revision();
    QTest::mouseClick(viewport, Qt::LeftButton, {}, QPoint(20, viewport->height() / 2));
    f.settle();
    f.expectClosed(viewport, "Viewport click");
    check(viewport->selectionState().entities() == selection &&
              window.document().revision() == revision,
          "The dismissing click does not select or edit");
    // Shortcuts: Ctrl+Shift+T (Model panel) is More panels and toggles; the View menu's panel
    // actions open their tab.
    f.focusViewport();
    f.key(Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    f.expectOpen("tray", "Ctrl+Shift+T");
    check(f.action("view.tray")->isChecked(), "Model panel is checked while the sheet shows it");
    f.key(Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    f.expectClosed(viewport, "Ctrl+Shift+T again");
    check(!f.action("view.tray")->isChecked(), "Model panel is unchecked when the sheet closes");
    f.action("view.history")->trigger();
    f.expectOpen("history", "View › History");
    f.click(f.button("tray"));
    f.expectOpen("tray", "More panels click");
    check(f.side->currentWidget() == f.tray && f.organization->isVisible() &&
              f.organization->tabBar()->isVisible() && heading->text() == "PANELS",
          "More panels shows the whole Model panel, headed PANELS, with its tab row");
    // Ctrl+Tab and Ctrl+Shift+Tab switch the Tray's tabs in the More panels sheet (the Tray has
    // no Ctrl+PgDown binding).
    {
        auto *bar = f.organization->tabBar();
        bar->setFocus(Qt::TabFocusReason);
        check(QTest::qWaitFor([&] { return bar->hasFocus(); }), "Tray tab bar focused");
        const int before = f.organization->currentIndex();
        f.key(Qt::Key_Tab, Qt::ControlModifier);
        check(f.organization->currentIndex() == (before + 1) % f.organization->count(),
              "Ctrl+Tab moves to the next Tray tab");
        f.key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
        check(f.organization->currentIndex() == before, "Ctrl+Shift+Tab moves back a tab");
        check(f.open() && f.checked() == std::set<QString>{"tray"},
              "Switching tabs keeps the More panels sheet");
    }
    f.click(f.button("tray"));
    f.expectClosed(viewport, "More panels click again");
    // Ctrl+J: the Assistant in the sheet, its composer focused; Esc closes; Ctrl+J toggles.
    auto *assistant = window.assistantPanel();
    auto *composer = window.findChild<QPlainTextEdit *>("assistantComposer");
    f.focusViewport();
    f.key(Qt::Key_J, Qt::ControlModifier);
    f.expectOpen("assistant", "Ctrl+J");
    // The Assistant's own focus target (the composer once a provider is set up).
    check(f.side->currentWidget() == assistant && composer &&
              QTest::qWaitFor([&] { return assistant->isAncestorOf(focused()); }),
          "The Assistant has focus" + f.state());
    check(heading->text() == "ASSISTANT", "The Assistant's sheet is headed ASSISTANT");
    check(!window.findChild<QDialog *>("assistantSheet"),
          "No separate Assistant dialog at 800-1100 px");
    f.key(Qt::Key_Escape);
    f.expectClosed(viewport, "Esc in the Assistant");
    f.key(Qt::Key_J, Qt::ControlModifier);
    f.expectOpen("assistant", "Ctrl+J again");
    f.key(Qt::Key_J, Qt::ControlModifier);
    f.expectClosed(viewport, "Ctrl+J on the open Assistant");
    check(f.side->indexOf(assistant) < 0, "Closing with Ctrl+J hides the Assistant");
    f.click(f.button("assistant"));
    f.expectOpen("assistant", "Assistant click");
    f.click(f.button("assistant"));
    f.expectClosed(viewport, "Assistant click again");
    // The sheet never takes Measurements typing: closed, then open with focus moved out.
    f.measurements->setFocus(Qt::OtherFocusReason);
    check(QTest::qWaitFor([&] { return f.measurements->hasFocus(); }), "Measurements focused");
    f.measurements->clear();
    QTest::keyClicks(f.measurements, "2,3");
    check(f.measurements->text() == "2,3" && !f.open(),
          "Typing reaches Measurements while the sheet is closed");
    f.click(f.button("info"));
    f.expectOpen("info", "Info click from Measurements");
    f.measurements->setFocus(Qt::OtherFocusReason);
    check(QTest::qWaitFor([&] { return f.measurements->hasFocus(); }), "Measurements focused");
    QTest::keyClicks(f.measurements, "0");
    check(f.measurements->text() == "2,30", "Typing reaches Measurements while the sheet shows");
    f.measurements->clear();
    f.click(f.button("info"));
    f.expectClosed(nullptr, "Info closes again");
    f.focusViewport();
    // The strip is a single Tab stop with arrow keys (R084.v); Enter opens, Esc returns to it.
    f.strip->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return f.strip->isAncestorOf(focused()); }),
          "F6-style focus on the strip lands on a button");
    f.button("info")->setFocus(Qt::TabFocusReason);
    f.key(Qt::Key_Down);
    check(focused() == f.button("outliner"), "Down moves to Outliner");
    f.key(Qt::Key_Return);
    f.expectOpen("outliner", "Enter on Outliner");
    f.key(Qt::Key_Escape);
    f.expectClosed(f.button("outliner"), "Esc returns to the strip button");
    // F6 visits the strip as a region.
    f.focusViewport();
    bool reached = false;
    for (int i = 0; i < 8 && !reached; ++i) {
        f.key(Qt::Key_F6);
        reached = f.strip->isAncestorOf(focused());
    }
    check(reached, "F6 reaches the strip");
}
void resizing(Fixture &f) {
    auto &window = f.window;
    f.focusViewport();
    f.click(f.button("info"));
    f.expectOpen("info", "Info before resizing");
    const auto area = f.splitter->geometry();
    auto drag = [&](int dx) {
        const auto start = f.handle->rect().center();
        const auto origin = f.handle->mapToGlobal(start);
        QTest::mousePress(f.handle, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 4; ++step)
            QTest::mouseMove(f.handle, f.handle->mapFromGlobal(origin + QPoint(dx * step / 4, 0)));
        QTest::mouseRelease(f.handle, Qt::LeftButton, {},
                            f.handle->mapFromGlobal(origin + QPoint(dx, 0)));
        f.settle();
    };
    drag(-60);
    check(f.side->width() == 400 && stored("sidebarTabbedWidth") == 400,
          "Dragging the sheet's edge widens it and stores the tabbed width, got " +
              std::to_string(f.side->width()) + " / " +
              std::to_string(stored("sidebarTabbedWidth")));
    check(f.overlay->geometry().right() == area.right(), "The sheet stays anchored on the right");
    drag(30);
    check(f.side->width() == 370 && stored("sidebarTabbedWidth") == 370, "Dragging right narrows");
    drag(2000);
    check(f.side->width() == 240, "The sheet stops at 240 px");
    drag(-4000);
    check(f.side->width() == 720 && stored("sidebarTabbedWidth") == 720,
          "The sheet stops at 720 px, got " + std::to_string(f.side->width()));
    // A narrower window clamps the sheet to it without rewriting the setting.
    f.resize(780);
    check(f.open() && f.side->width() == f.splitter->width() - f.handle->width() &&
              f.overlay->geometry().left() == f.splitter->geometry().left(),
          "The sheet is clamped to the window at 780 px, got " + std::to_string(f.side->width()));
    check(stored("sidebarTabbedWidth") == 720, "Clamping keeps the stored width");
    f.resize(900);
    check(f.open() && f.side->width() == 720, "A wider window restores the stored width");
    // Keyboard: Widen and Narrow step the open sheet.
    drag(f.side->width() - 300);
    check(f.side->width() == 300, "Sheet at 300 px");
    f.key(Qt::Key_BracketRight, Qt::ControlModifier | Qt::AltModifier);
    check(f.side->width() == 340 && stored("sidebarTabbedWidth") == 340,
          "Ctrl+Alt+] widens the open sheet by 40 px");
    f.key(Qt::Key_BracketLeft, Qt::ControlModifier | Qt::AltModifier);
    check(f.side->width() == 300, "Ctrl+Alt+[ narrows it again");
    // Double-click resets.
    QTest::mouseDClick(f.handle, Qt::LeftButton, {}, f.handle->rect().center());
    check(QTest::qWaitFor([&] { return f.side->width() == 340; }),
          "Double-clicking the edge resets the sheet to 340 px");
    check(stored("sidebarTabbedWidth") == -1, "Reset forgets the stored width");
    // The setting is the tabbed layout's: the 1100-1500 px panel with the Assistant uses it.
    drag(-80);
    check(stored("sidebarTabbedWidth") == 420, "Sheet at 420 px");
    f.key(Qt::Key_Escape);
    f.expectClosed(window.viewport(), "Esc after resizing");
    f.key(Qt::Key_J, Qt::ControlModifier);
    f.expectOpen("assistant", "Ctrl+J before widening");
    check(f.side->width() == 420, "The Assistant uses the same sheet width");
    f.resize(1300);
    check(f.splitter->indexOf(f.side) == 1 && f.side->width() == 420 &&
              f.side->currentWidget() == window.assistantPanel(),
          "At 1300 px the tabbed panel has the sheet's width and the Assistant tab, got " +
              std::to_string(f.side->width()));
    f.focusViewport();
    f.key(Qt::Key_J, Qt::ControlModifier);
    f.settle();
    f.resize(900);
    QSettings("SketchyUp", "SketchyUp").remove("sidebarTabbedWidth");
    window.findChild<QAction *>("view.sidebar.reset")->trigger();
    f.settle();
}
void menus(Fixture &f) {
    auto &window = f.window;
    f.focusViewport();
    // The Menu button lists every visible menu of the (hidden) menu bar, in order.
    QStringList expected;
    for (auto *entry : window.menuBar()->actions())
        if (entry->menu() && entry->isVisible())
            expected << entry->text();
    check(expected.size() >= 6, "The menu bar has its menus");
    // Qt's Wayland popups need a compositor input serial that synthetic input cannot supply
    // (as for the tool rail's overflow menu), so the menus are opened for real on X11 only;
    // on Wayland the Menu button's menu is filled as it is before showing.
    const bool popups = QGuiApplication::platformName() == "xcb";
    auto *narrowMenu = f.menuButton->menu();
    check(narrowMenu && narrowMenu->objectName() == "narrowMenu", "The Menu button has a menu");
    auto listedMenus = [&](QMenu *popup) {
        QStringList result;
        for (auto *entry : popup->actions())
            if (entry->menu() && entry->isVisible())
                result << entry->text();
        return result;
    };
    auto openMenu = [&]() {
        QStringList listed;
        if (!popups) {
            emit narrowMenu->aboutToShow();
            return listedMenus(narrowMenu);
        }
        bool shown{};
        QTimer::singleShot(150, &window, [&] {
            auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            shown = popup == narrowMenu;
            if (popup) {
                listed = listedMenus(popup);
                popup->close();
            }
        });
        f.click(f.menuButton);
        check(QTest::qWaitFor([&] { return shown; }), "The Menu button opens its menu");
        QTest::qWait(100);
        return listed;
    };
    const auto listed = openMenu();
    check(listed == expected,
          "The Menu button lists every menu: " + listed.join(", ").toStdString() + " vs " +
              expected.join(", ").toStdString());
    // Sheets adds Page, Insert and Arrange.
    window.setWorkspace(Window::Workspace::Sheets);
    f.settle();
    const auto sheets = openMenu();
    check(sheets.contains("&Page") && sheets.contains("&Insert") && sheets.contains("&Arrange"),
          "In Sheets the menu lists Page, Insert and Arrange");
    window.setWorkspace(Window::Workspace::Model);
    f.settle();
    // The hidden menu bar's mnemonics open the same menus from the button.
    for (const auto &[title, key] : std::vector<std::pair<QString, QString>>{
             {"File", "Alt+F"}, {"Edit", "Alt+E"}, {"View", "Alt+V"}, {"Page", "Alt+P"}}) {
        auto *shortcut = f.menuButton->findChild<QShortcut *>("menuMnemonic." + title);
        check(shortcut && shortcut->key() == QKeySequence(key) &&
                  shortcut->isEnabled() == (title != "Page"),
              "The " + title.toStdString() + " mnemonic is " + key.toStdString() +
                  " and live only while its menu is");
    }
    f.focusViewport();
    if (popups) {
        QString opened;
        QTimer::singleShot(150, &window, [&] {
            if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                opened = popup->title();
                popup->close();
            }
        });
        QTest::keyClick(focused(), Qt::Key_F, Qt::AltModifier);
        check(QTest::qWaitFor([&] { return !opened.isEmpty(); }) && opened == "&File",
              "Alt+F opens the File menu, got " + opened.toStdString());
        QTest::qWait(100);
        f.activate();
    }
    // Every action shortcut keeps its host: the same actions are reachable as at 1200 px.
    auto hosts = [&] {
        std::set<QString> result;
        for (auto *entry : window.findChildren<QAction *>()) {
            if (entry->shortcut().isEmpty() || entry->objectName().isEmpty())
                continue;
            for (auto *object : entry->associatedObjects())
                if (auto *widget = qobject_cast<QWidget *>(object);
                    widget && !qobject_cast<QMenu *>(widget) && !qobject_cast<QMenuBar *>(widget) &&
                    widget->window() == &window &&
                    (widget == &window || widget->isVisibleTo(&window)))
                    result.insert(entry->objectName());
        }
        return result;
    };
    const auto narrow = hosts();
    f.resize(1200);
    const auto wide = hosts();
    check(narrow == wide && narrow.size() > 20,
          "Shortcut hosts are the same in the narrow layout (" + std::to_string(narrow.size()) +
              " vs " + std::to_string(wide.size()) + ")");
    f.resize(900);
    // Shortcuts fire: F6, Ctrl+Alt+S (Sheets and back), Ctrl+J and Ctrl+Shift+T above.
    f.focusViewport();
    f.key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Sheets, "Ctrl+Alt+S switches to Sheets");
    f.key(Qt::Key_S, Qt::ControlModifier | Qt::AltModifier);
    check(window.workspace() == Window::Workspace::Model, "Ctrl+Alt+S switches back");
    // The palette (Ctrl+K) still opens.
    bool palette{};
    QTimer::singleShot(200, &window, [&] {
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            dialog && dialog->objectName() == "commandPalette") {
            palette = true;
            dialog->reject();
        }
    });
    f.key(Qt::Key_K, Qt::ControlModifier);
    check(palette, "Ctrl+K opens the command palette");
    f.activate();
}
void breakpoints(Fixture &f) {
    auto &window = f.window;
    auto *viewport = window.viewport();
    f.resize(1200);
    // State: a selection, an unsaved edit, the Materials tab and a docked width.
    const auto body = window.document().bodies().begin()->first;
    viewport->setSelection(body);
    f.settle();
    const auto selection = viewport->selectionState().entities();
    check(!selection.empty(), "A selection to keep");
    window.findChild<QAction *>("view.materials")->trigger();
    f.settle();
    check(window.document().dirty(), "The demo document is dirty");
    const auto revision = window.document().revision();
    const int docked = f.side->width();
    for (int width : {1000, 780, 1600, 900, 1200}) {
        f.resize(width);
        check(viewport->selectionState().entities() == selection,
              "Selection survives " + std::to_string(width));
        check(window.document().dirty() && window.document().revision() == revision,
              "Dirty state survives " + std::to_string(width));
        check(f.organization->tabText(f.organization->currentIndex()) == "Materials" &&
                  f.side->currentWidget() == f.tray,
              "The open tab survives " + std::to_string(width));
    }
    check(f.side->width() == docked, "Crossing back above 1100 px restores the docked width");
    // Working in the docked panel as the window narrows: it stays open as the sheet, with
    // focus where it was; widening docks it again on the same tab.
    auto *tabBar = f.organization->tabBar();
    tabBar->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return tabBar->hasFocus(); }), "Panel tab bar focused");
    f.resize(900);
    f.expectOpen("tray", "Narrowing with focus in the panel (the whole panel, as More panels)");
    check(tabBar->hasFocus(), "Focus stays in the panel" + f.state());
    f.resize(1200);
    check(!f.open() && f.splitter->indexOf(f.side) == 1 && tabBar->hasFocus() &&
              f.organization->tabText(f.organization->currentIndex()) == "Materials",
          "Widening docks the panel on the same tab with focus kept" + f.state());
    // The open sheet's tab is the docked panel's tab after widening.
    f.resize(900);
    f.focusViewport();
    f.click(f.button("tags"));
    f.expectOpen("tags", "Tags before widening");
    f.resize(1200);
    check(!f.open() && f.organization->tabText(f.organization->currentIndex()) == "Tags",
          "The sheet's tab is open in the docked panel");
    // The Assistant's tab, docked at 1300 px, is the sheet's Assistant at 900 px and the
    // column at 1600 px.
    f.resize(1300);
    f.focusViewport();
    f.key(Qt::Key_J, Qt::ControlModifier);
    auto *assistant = window.assistantPanel();
    check(f.side->currentWidget() == assistant, "Assistant tab at 1300 px");
    // Ctrl+J left focus in the Assistant, so narrowing keeps it open as the sheet.
    f.resize(900);
    check(f.side->indexOf(assistant) >= 0, "The Assistant stays a tab in the narrow layout");
    f.expectOpen("assistant", "The Assistant after narrowing");
    f.resize(1600);
    check(f.splitter->indexOf(assistant) == 2 && assistant->isVisible() && f.side->isVisible(),
          "At 1600 px the Assistant is its column again");
    f.focusViewport();
    f.key(Qt::Key_J, Qt::ControlModifier);
    f.resize(900);
    check(window.document().dirty() && viewport->selectionState().entities() == selection,
          "Selection and dirty state after the Assistant round trip");
}
// The strip, its icons and the sheet in one theme: 36 px buttons on the panel surface, a muted
// glyph (accent when its panel is open) of at least 3:1, the sheet's surface and edge.
void pixels(Fixture &f, bool dark, const QString &directory) {
    auto &window = f.window;
    const auto colors = themeColors(dark);
    window.findChild<QAction *>(dark ? "view.theme.2" : "view.theme.1")->trigger();
    f.settle();
    const std::string theme = dark ? "dark" : "light";
    f.focusViewport();
    // Icons rendered large keep their exact colours: muted at rest, accent when checked.
    for (auto *entry : f.strip->actions()) {
        for (const bool on : {false, true}) {
            const auto image =
                entry->icon()
                    .pixmap(QSize(96, 96), 1, QIcon::Normal, on ? QIcon::On : QIcon::Off)
                    .toImage();
            int exact = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    exact += near(image.pixelColor(x, y), on ? colors.accent : colors.muted, 1) &&
                             image.pixelColor(x, y).alpha() == 255;
            check(exact > 40, theme + ": " + entry->text().toStdString() + " icon is drawn in " +
                                  (on ? "accent" : "muted") + " (" + std::to_string(exact) +
                                  " px)");
        }
    }
    // In place: each button's background and its strongest icon pixel.
    auto inspect = [&](QWidget *button, const QColor &background, const QColor &ink,
                       const std::string &what) {
        const auto image = button->grab().toImage();
        const auto scale = image.devicePixelRatio();
        const QColor fill = image.pixelColor(image.width() / 2, int(3 * scale));
        check(near(fill, background, 1), theme + ": " + what + " background is " +
                                             background.name().toStdString() + ", got " +
                                             fill.name().toStdString());
        double best = 0;
        bool exact = false;
        const int inset = int(6 * scale);
        for (int y = inset; y < image.height() - inset; ++y)
            for (int x = inset; x < image.width() - inset; ++x) {
                const auto pixel = image.pixelColor(x, y);
                best = std::max(best, contrast(pixel, fill));
                exact |= near(pixel, ink, 2);
            }
        check(exact,
              theme + ": " + what + " glyph shows its full colour " + ink.name().toStdString());
        check(best >= 3,
              theme + ": " + what + " glyph contrast " + std::to_string(best) + " is at least 3:1");
        return image;
    };
    for (auto *entry : f.strip->actions())
        inspect(f.strip->widgetForAction(entry), colors.surface, colors.muted,
                entry->text().toStdString());
    inspect(f.menuButton, colors.surface, colors.ink, "Menu button");
    f.click(f.button("info"));
    f.expectOpen("info", theme + " Info");
    inspect(f.button("info"), colors.selected, colors.accent, "checked Info");
    check(contrast(colors.accent, colors.selected) >= 3 &&
              contrast(colors.muted, colors.surface) >= 3,
          theme + ": icon colours meet 3:1");
    // The sheet: the panel surface behind its text, and the R094.a edge.
    const auto sheet = f.overlay->grab().toImage();
    const auto scale = sheet.devicePixelRatio();
    const auto handleWidth = int(f.handle->width() * scale);
    check(near(sheet.pixelColor(0, 0), colors.hover, 1),
          theme + ": the sheet's edge is the handle's fill");
    int grip = 0;
    for (int y = 0; y < sheet.height(); ++y)
        for (int x = 0; x < handleWidth; ++x)
            grip += near(sheet.pixelColor(x, y), colors.muted, 1);
    check(grip > 0 && contrast(colors.muted, colors.hover) >= 3,
          theme + ": the edge's grip is drawn at 3:1");
    const auto heading = window.findChild<QLabel *>("overlayHeading");
    check(heading && heading->isVisible() && heading->text() == "INFO",
          theme + ": the sheet is headed with its panel's name");
    const auto background = sheet.pixelColor(int((f.overlay->width() - 4) * scale),
                                             int((f.overlay->height() - 4) * scale));
    check(near(heading->palette().color(heading->backgroundRole()), colors.surface, 1) ||
              near(background, colors.surface, 1) || near(background, colors.input, 1),
          theme + ": the sheet is painted on the panel surface");
    check(contrast(colors.ink, colors.surface) >= 4.5 &&
              contrast(colors.muted, colors.surface) >= 4.5,
          theme + ": text in the sheet meets 4.5:1");
    // Screenshots with keyboard focus in the sheet and the pointer over its empty lower part.
    auto shoot = [&](const QString &name) {
        if (directory.isEmpty())
            return;
        check(f.focusInside(), theme + ": focus is in the sheet for the screenshot" + f.state());
        QTest::mouseMove(f.overlay, QPoint(f.overlay->width() / 2, f.overlay->height() - 8));
        QTest::qWait(50);
        check(window.grab().save(
                  directory +
                  QString("/narrow-900-%1-%2.png").arg(QString::fromStdString(theme), name)),
              "Save the " + name.toStdString() + " screenshot");
    };
    shoot("overlay");
    if (!directory.isEmpty())
        check(
            f.overlay->grab().save(
                directory + QString("/narrow-900-%1-sheet.png").arg(QString::fromStdString(theme))),
            "Save the sheet screenshot");
    // The strip's focus ring is the accent (3:1 on the surface).
    f.button("history")->setFocus(Qt::TabFocusReason);
    QTest::qWait(30);
    {
        const auto image = f.button("history")->grab().toImage();
        const auto edge = image.pixelColor(image.width() / 2, 0);
        check(near(edge, colors.accent, 2) && contrast(edge, colors.surface) >= 3,
              theme + ": a focused strip button has the accent ring, got " +
                  edge.name().toStdString());
    }
    // More panels: the whole panel with its tab row, headed PANELS.
    f.focusViewport();
    f.click(f.button("tray"));
    f.expectOpen("tray", theme + " More panels");
    check(heading->text() == "PANELS" && f.organization->tabBar()->isVisible(),
          theme + ": the More panels sheet has its tab row");
    shoot("more");
    f.key(Qt::Key_Escape);
    f.expectClosed(nullptr, theme + " Esc");
    if (!directory.isEmpty()) {
        f.focusViewport();
        check(
            window.grab().save(
                directory + QString("/narrow-900-%1-strip.png").arg(QString::fromStdString(theme))),
            "Save the strip screenshot");
        auto image = f.strip->grab().toImage();
        image.scaled(image.size() * 3, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .save(directory +
                  QString("/narrow-900-%1-strip-zoom.png").arg(QString::fromStdString(theme)));
    }
}

// ---- The tool rail's groups (UX_DESIGN §3.3, below 800 px). ----
const std::vector<std::pair<QString, std::vector<QString>>> railGroups{
    {"Select", {"Select", "Paint"}},
    {"Draw", {"Line", "Rectangle", "Circle"}},
    {"Modify",
     {"Push/pull", "Offset", "Follow Me", "Intersect", "Solid tools", "Face orientation", "Move",
      "Rotate", "Scale", "Create new face when pushing/pulling"}},
    {"Measure", {"Tape measure"}},
    {"Camera", {"Orbit", "Pan"}}};
struct RailFixture {
    Fixture &f;
    QToolBar *rail{};
    QToolButton *group{};
    std::vector<QAction *> tools; // Every tool and option on the wide rail, in rail order.
    explicit RailFixture(Fixture &fixture) : f(fixture) {
        rail = f.window.findChild<QToolBar *>("toolRail");
        group = f.window.findChild<QToolButton *>("railGroupButton");
        check(rail && group, "The tool rail has its group button");
        widen();
        for (auto *entry : rail->actions())
            if (!entry->isSeparator() && rail->widgetForAction(entry) != group)
                tools.push_back(entry);
    }
    // The wide rail at a height that holds every tool; at 800 px high Qt's overflow menu takes
    // the last few, as before.
    void widen(int width = 900) {
        f.window.setFixedSize(width, 1000);
        check(QTest::qWaitFor([&] { return f.window.size() == QSize(width, 1000); }),
              "Window resized to " + std::to_string(width) + "x1000");
        f.settle();
    }
    // A tool leaves the rail (not just the view) while another group shows.
    bool onRail(QAction *entry) const { return rail->actions().contains(entry); }
    QAction *tool(const QString &text) const {
        for (auto *entry : tools)
            if (entry->text() == text)
                return entry;
        check(false, "The rail has " + text.toStdString());
        return nullptr;
    }
    QWidget *widget(const QString &text) const { return rail->widgetForAction(tool(text)); }
    // The tools (not the group button or separators) that show on the rail, by name.
    std::set<QString> visible() const {
        std::set<QString> result;
        for (auto *entry : tools) {
            auto *shown = onRail(entry) ? rail->widgetForAction(entry) : nullptr;
            if (shown && entry->isVisible() && shown->isVisibleTo(rail))
                result.insert(entry->text());
        }
        return result;
    }
    int separators() const {
        int count = 0;
        for (auto *entry : rail->actions())
            if (entry->isSeparator() && entry->isVisible() && rail->widgetForAction(entry) &&
                rail->widgetForAction(entry)->isVisibleTo(rail))
                ++count;
        return count;
    }
    std::set<QString> all() const {
        std::set<QString> result;
        for (const auto &[name, tools] : railGroups)
            result.insert(tools.begin(), tools.end());
        return result;
    }
    std::set<QString> members(const QString &name) const {
        for (const auto &[group, tools] : railGroups)
            if (group == name)
                return {tools.begin(), tools.end()};
        return {};
    }
    // The checked tool (the new-face toggle is an option, not a tool).
    QString active() const {
        for (auto *entry : tools)
            if (entry->actionGroup() && entry->isChecked())
                return entry->text();
        return {};
    }
    // The rail's Tab stops: the rail's buttons that take Tab focus.
    std::vector<QToolButton *> stops() const {
        std::vector<QToolButton *> result;
        for (auto *button : rail->findChildren<QToolButton *>(Qt::FindDirectChildrenOnly))
            if (button->focusPolicy() & Qt::TabFocus)
                result.push_back(button);
        return result;
    }
    void expectGroup(const QString &name, const std::string &where) {
        f.settle();
        check(group->isVisible() && group->text() == name, where + ": the group button shows " +
                                                               name.toStdString() + ", got " +
                                                               group->text().toStdString());
        {
            const auto now = visible();
            QStringList shown(now.begin(), now.end());
            check(now == members(name),
                  where + ": only the " + name.toStdString() +
                      " tools show; shown: " + shown.join(", ").toStdString());
        }
        check(separators() == 0, where + ": no separators show");
        check(group->toolTip() == "Tool group: " + name &&
                  accessibleText(group, QAccessible::Name) == "Tool group" &&
                  accessibleText(group, QAccessible::Description) == name,
              where + ": the group button's name, description and tooltip follow the group");
        for (auto *entry : group->menu()->actions())
            check(entry->isChecked() == (entry->text() == name),
                  where + ": the menu checks " + name.toStdString() + " only");
        const auto stops = this->stops();
        check(stops.size() == 1 && stops.front()->isVisibleTo(rail),
              where + ": the rail has one visible Tab stop, got " + std::to_string(stops.size()));
    }
    void expectWide(const std::string &where) {
        f.settle();
        check(!group->isVisible() && !group->isVisibleTo(rail),
              where + ": no group button on the wide rail");
        auto join = [](const std::set<QString> &names) {
            QStringList list(names.begin(), names.end());
            return list.join(", ").toStdString();
        };
        const auto now = visible();
        check(now == all(), where + ": every tool shows on the wide rail; shown: " + join(now));
        check(separators() == 1, where + ": one separator (before Camera) shows");
    }
};
void railGroupBehaviour(Fixture &f) {
    auto &window = f.window;
    RailFixture r(f);
    r.widen();
    r.expectWide("900 px");
    check(r.rail->width() == 78, "The wide rail is 78 px");
    // The breakpoint: 800 px is the wide rail, 799 px the grouped one.
    r.widen(800);
    r.expectWide("800 px");
    r.widen(799);
    r.expectGroup("Select", "799 px");
    // Below 800 px: the active tool's group.
    f.resize(760);
    check(r.active() == "Select", "Select is the starting tool");
    r.expectGroup("Select", "760 px");
    check(r.rail->widgetForAction(r.rail->actions().front()) == r.group,
          "The group button is the rail's first item");
    check(r.group->width() <= r.rail->width() &&
              r.group->mapTo(r.rail, QPoint(r.group->width(), 0)).x() <= r.rail->width(),
          "The group button is no wider than the rail");
    check(!r.group->icon().isNull() && r.group->iconSize().width() >= 8,
          "The group button has its chevron");
    check(r.group->width() >= r.widget("Select")->width() &&
              r.group->width() + 10 >= r.rail->width(),
          "The group button fills the rail's width: " + std::to_string(r.group->width()) +
              " against " + std::to_string(r.widget("Select")->width()));
    // Every group's name and chevron fit the rail without clipping.
    auto *menu = r.group->menu();
    check(menu && menu->actions().size() == 5, "The group menu lists five groups");
    QStringList names;
    for (auto *entry : menu->actions())
        names << entry->text();
    check(names == QStringList({"Select", "Draw", "Modify", "Measure", "Camera"}),
          "The group menu lists the five groups in order");
    // Menu item names reach the accessibility bridge as the group names.
    if (auto *menuAccessible = QAccessible::queryAccessibleInterface(menu)) {
        QStringList listed;
        for (int i = 0; i < menuAccessible->childCount(); ++i)
            listed << menuAccessible->child(i)->text(QAccessible::Name);
        check(listed == names,
              "The menu items are named for the groups, got " + listed.join('|').toStdString());
    }
    for (const auto &[name, tools] : railGroups) {
        // Choosing a group shows its tools and leaves the active tool alone.
        for (auto *entry : menu->actions())
            if (entry->text() == name)
                entry->trigger();
        r.expectGroup(name, "menu " + name.toStdString());
        check(r.active() == "Select", "Choosing " + name.toStdString() + " keeps the active tool");
        const auto want =
            r.group->fontMetrics().horizontalAdvance(name) + r.group->iconSize().width();
        check(want + 3 + 2 <= r.group->width(),
              "\"" + name.toStdString() + "\" and the chevron fit the group button (" +
                  std::to_string(want + 5) + " of " + std::to_string(r.group->width()) + ")");
    }
    // A tool picked by shortcut switches to its group; the shortcut works with the group hidden.
    f.focusViewport();
    f.key(Qt::Key_L);
    check(r.active() == "Line", "L chooses the Line tool");
    r.expectGroup("Draw", "L");
    f.key(Qt::Key_B);
    check(r.active() == "Paint", "B chooses the Paint tool");
    r.expectGroup("Select", "B (Paint)");
    f.key(Qt::Key_O);
    check(r.active() == "Orbit", "O chooses the Orbit tool");
    r.expectGroup("Camera", "O (Orbit)");
    f.key(Qt::Key_M);
    check(r.active() == "Move", "M chooses the Move tool");
    r.expectGroup("Modify", "M");
    f.key(Qt::Key_T);
    r.expectGroup("Measure", "T");
    // A tool that is not on the rail (Zoom) leaves the shown group alone.
    f.key(Qt::Key_Z);
    check(r.active().isEmpty(), "Zoom is not on the rail");
    r.expectGroup("Measure", "Z (Zoom, off the rail)");
    // Menu and command search run the tool's action.
    r.tool("Rotate")->trigger();
    r.expectGroup("Modify", "the Rotate action");
    r.tool("Circle")->trigger();
    r.expectGroup("Draw", "the Circle action");
    check(r.active() == "Circle", "Circle is active");
    // The option toggle is not a tool: switching it does not move the rail.
    r.widen();
    r.expectWide("widened");
    check(r.active() == "Circle", "Widening keeps the active tool");
    r.tool("Create new face when pushing/pulling")->setChecked(true);
    r.tool("Create new face when pushing/pulling")->setChecked(false);
    f.resize(760);
    r.expectGroup("Draw", "narrowed again");
    // Narrowing starts on the active tool's group, not the last group chosen from the menu.
    for (auto *entry : menu->actions())
        if (entry->text() == "Camera")
            entry->trigger();
    r.expectGroup("Camera", "Camera chosen");
    f.resize(900);
    f.resize(760);
    r.expectGroup("Draw", "narrowed after choosing Camera");
    // Widening keeps the active tool and the keyboard focus.
    f.focusViewport();
    f.resize(900);
    check(window.viewport()->hasFocus(), "Widening keeps focus in the viewport");
    f.resize(760);
    r.group->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return r.group->hasFocus(); }), "The group button takes focus");
    r.widen();
    check(r.active() == "Circle" && focused() == r.widget("Circle") && focused()->isVisible(),
          "Widening from the group button moves focus to the active tool, got " +
              identity(focused()));
    r.expectWide("widened from the group button");
    // Narrowing with focus on a tool that hides moves focus to the new group's active tool.
    r.widget("Orbit")->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return r.widget("Orbit")->hasFocus(); }), "Orbit takes focus");
    // (Narrowing at the same height: a shorter window would first push Orbit into the rail's
    // overflow menu, as it always has, and Qt would move focus before this code runs.)
    r.widen(760);
    r.expectGroup("Draw", "narrowed with focus in the rail");
    check(focused() == r.widget("Circle") && r.active() == "Circle",
          "Narrowing keeps focus in the rail on the active tool, got " + identity(focused()));
    // The shortcut and the group button work after the rail has been through a breakpoint.
    f.focusViewport();
    f.key(Qt::Key_O);
    r.expectGroup("Camera", "O after the breakpoint");
    f.key(Qt::Key_L);
    r.expectGroup("Draw", "L after the breakpoint");
    check(r.active() == "Line", "Line is the active tool");
}
// Tab into the rail, arrow to the group button, open its menu from the keyboard.
void railGroupKeyboard(Fixture &f) {
    auto &window = f.window;
    RailFixture r(f);
    f.resize(760);
    f.focusViewport();
    f.key(Qt::Key_L);
    r.expectGroup("Draw", "keyboard");
    auto *search = window.findChild<QPushButton *>("commandSearch");
    check(search != nullptr, "Command search exists");
    search->setFocus(Qt::TabFocusReason);
    check(QTest::qWaitFor([&] { return search->hasFocus(); }), "Command search takes focus");
    f.key(Qt::Key_Tab);
    check(focused() == r.widget("Line"),
          "Tab from command search enters the rail on the active tool, got " + identity(focused()));
    // Down on the rail's tools moves along; Up from the first tool reaches the group button.
    f.key(Qt::Key_Down);
    check(focused() == r.widget("Rectangle"), "Down moves to the next tool");
    f.key(Qt::Key_Up);
    f.key(Qt::Key_Up);
    check(focused() == r.group,
          "Up reaches the group button, the rail's first item, got " + identity(focused()));
    check(r.stops().size() == 1 && r.stops().front() == r.group,
          "The group button is the rail's one Tab stop while it has focus");
    f.key(Qt::Key_Right);
    check(focused() == r.widget("Line"), "Right leaves the group button for the first tool");
    f.key(Qt::Key_Left);
    check(focused() == r.group, "Left returns to the group button");
    f.key(Qt::Key_End);
    check(focused() == r.widget("Circle"), "End reaches the last tool of the shown group");
    f.key(Qt::Key_Home);
    check(focused() == r.group, "Home reaches the group button");
    f.key(Qt::Key_Tab);
    check(focused() != nullptr && !r.rail->isAncestorOf(focused()),
          "Tab leaves the rail from the group button, got " + identity(focused()));
    const auto after = identity(focused());
    f.key(Qt::Key_Backtab, Qt::ShiftModifier);
    // Leaving the rail puts its one Tab stop back on the active tool, as it always has.
    check(focused() == r.widget("Line"),
          "Shift+Tab returns to the rail's stop (the active tool) from " + after + ", got " +
              identity(focused()));
    f.key(Qt::Key_Up);
    check(focused() == r.group, "Up from the active tool reaches the group button again");
    // Enter, Space and Down open the menu; Esc closes it and focus returns to the button. Qt's
    // Wayland popups need an input serial that synthetic input cannot supply, so there the
    // menu is only filled, as the Menu button's is.
    const bool popups = QGuiApplication::platformName() == "xcb";
    auto *menu = r.group->menu();
    for (const auto key : {Qt::Key_Return, Qt::Key_Space, Qt::Key_Down, Qt::Key_Enter}) {
        r.group->setFocus(Qt::TabFocusReason);
        check(QTest::qWaitFor([&] { return r.group->hasFocus(); }), "Group button focused");
        if (!popups) {
            emit menu->aboutToShow();
            for (auto *entry : menu->actions())
                check(!entry->text().isEmpty() && entry->isCheckable(),
                      "The group menu is filled before showing");
            continue;
        }
        bool opened = false, closed = false;
        QTimer::singleShot(200, &window, [&] {
            auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            opened = popup == menu && menu->isVisible();
            if (popup)
                QTest::keyClick(popup, Qt::Key_Escape);
        });
        QTest::keyClick(r.group, key);
        r.f.settle();
        closed = !menu->isVisible() && QApplication::activePopupWidget() == nullptr;
        check(opened,
              "The group menu opens from the keyboard (key " + std::to_string(int(key)) + ")");
        check(closed, "Esc closes the group menu");
        check(focused() == r.group && r.active() == "Line",
              "Esc returns focus to the group button, got " + identity(focused()));
        r.expectGroup("Draw", "after Esc");
    }
    if (popups) {
        // Choosing a group from the open menu with the keyboard: Down, Enter.
        QTimer::singleShot(200, &window, [&] {
            auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!popup)
                return;
            QTest::keyClick(popup, Qt::Key_Down);
            QTest::keyClick(popup, Qt::Key_Down);
            QTest::keyClick(popup, Qt::Key_Down);
            QTest::keyClick(popup, Qt::Key_Return);
        });
        QTest::keyClick(r.group, Qt::Key_Return);
        r.f.settle();
        check(r.active() == "Line" && r.group->text() != "Draw",
              "Choosing a group from the keyboard changes the shown group only, got " +
                  r.group->text().toStdString());
    }
    f.resize(900);
    f.focusViewport();
}
// The group button in one theme: the rail's tokens, its ink, chevron, focus ring and hover.
void railGroupPixels(Fixture &f, bool dark, const QString &directory) {
    auto &window = f.window;
    RailFixture r(f);
    const auto colors = themeColors(dark);
    const std::string theme = dark ? "dark" : "light";
    window.findChild<QAction *>(dark ? "view.theme.2" : "view.theme.1")->trigger();
    window.setFixedSize(760, 700);
    check(QTest::qWaitFor([&] { return window.size() == QSize(760, 700); }), "760x700");
    f.settle();
    f.focusViewport();
    f.key(Qt::Key_L);
    r.expectGroup("Draw", theme + " pixels");
    // The chevron is drawn in the rail's ink at 1.5 px.
    const auto icon = r.group->icon().pixmap(QSize(96, 96)).toImage();
    int exact = 0;
    for (int y = 0; y < icon.height(); ++y)
        for (int x = 0; x < icon.width(); ++x)
            exact +=
                near(icon.pixelColor(x, y), colors.ink, 1) && icon.pixelColor(x, y).alpha() == 255;
    check(exact > 20,
          theme + ": the chevron is drawn in the ink colour (" + std::to_string(exact) + " px)");
    check(contrast(colors.ink, colors.surface) >= 4.5, theme + ": the group name meets 4.5:1");
    // The button on the rail: same surface as the tool buttons, with text in the ink colour.
    auto strongest = [&](QWidget *button) {
        const auto image = button->grab().toImage();
        const auto scale = image.devicePixelRatio();
        const QColor fill = image.pixelColor(image.width() / 2, int(4 * scale));
        double best = 0;
        bool ink = false;
        for (int y = int(6 * scale); y < image.height() - int(6 * scale); ++y)
            for (int x = int(6 * scale); x < image.width() - int(6 * scale); ++x) {
                best = std::max(best, contrast(image.pixelColor(x, y), fill));
                ink |= near(image.pixelColor(x, y), colors.ink, 6);
            }
        return std::tuple{fill, best, ink};
    };
    const auto [groupFill, groupBest, groupInk] = strongest(r.group);
    const auto [toolFill, toolBest, toolInk] = strongest(r.widget("Rectangle"));
    check(near(groupFill, toolFill, 1) && near(groupFill, colors.surface, 1),
          theme + ": the group button rests on the rail's surface, got " +
              groupFill.name().toStdString());
    check(groupInk && groupBest >= 4.5,
          theme + ": the group name's contrast is " + std::to_string(groupBest));
    // Focus: the accent ring as on the other rail buttons.
    r.group->setFocus(Qt::TabFocusReason);
    QTest::qWait(30);
    {
        const auto image = r.group->grab().toImage();
        const auto edge = image.pixelColor(image.width() / 2, 0);
        check(near(edge, colors.accent, 2) && contrast(edge, colors.surface) >= 3,
              theme + ": the focused group button has the accent ring, got " +
                  edge.name().toStdString());
    }
    // Hover: the rail's hover fill.
    f.focusViewport();
    QTest::mouseMove(r.group, r.group->rect().center());
    QTest::qWait(60);
    if (r.group->underMouse()) {
        const auto image = r.group->grab().toImage();
        const auto fill = image.pixelColor(image.width() / 2, int(4 * image.devicePixelRatio()));
        check(near(fill, colors.hover, 1),
              theme + ": hover fills with the rail's hover tone, got " + fill.name().toStdString());
    }
    QTest::mouseMove(&window, QPoint(window.width() / 2, window.height() / 2));
    QTest::qWait(30);
    if (!directory.isEmpty()) {
        QTest::mouseMove(&window, QPoint(window.width() / 2, window.height() - 40));
        QTest::qWait(60);
        check(window.grab().save(
                  directory +
                  QString("/rail-760x700-draw-%1.png").arg(QString::fromStdString(theme))),
              "Save the rail screenshot");
        auto image = r.rail->grab().toImage();
        image.scaled(image.size() * 3, Qt::IgnoreAspectRatio, Qt::FastTransformation)
            .save(directory +
                  QString("/rail-760x700-draw-%1-zoom.png").arg(QString::fromStdString(theme)));
    }
    window.setFixedSize(900, 800);
    check(QTest::qWaitFor([&] { return window.width() == 900; }), "Back to 900 px");
    f.settle();
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
    QSettings("SketchyUp", "SketchyUp").setValue("recoverySeconds", 0);
    const auto directory = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (!directory.isEmpty())
        QDir().mkpath(directory);
    AssistantPanel::HostServices services;
    services.outcomeRoot = files.filePath("isolated-outcomes");
    services.credentialExecutable = "/bin/false";
    double dpr = 1;
    try {
        auto window = std::make_unique<Window>(nullptr, services);
        window->setFixedSize(1600, 800);
        window->show();
        check(QTest::qWaitForWindowExposed(window.get(), 20000), "Window exposed");
        window->demo();
        dpr = window->devicePixelRatioF();
        Fixture f(*window);
        f.activate();
        for (int width : {1600, 1200, 1000, 900, 780})
            layoutAt(f, width);
        std::cout << "Layout at 1600, 1200, 1000, 900 and 780 px passed\n";
        f.resize(900);
        accessibility(f);
        std::cout << "Strip names, tooltips and the sheet's edge over the accessibility bridge "
                     "passed\n";
        openAndClose(f);
        std::cout << "Sheet open and close by click, shortcut, Esc and a workspace click, focus "
                     "in and back, Measurements typing passed\n";
        f.resize(780);
        f.focusViewport();
        f.click(f.button("materials"));
        f.expectOpen("materials", "Materials at 780 px");
        f.key(Qt::Key_Escape);
        f.expectClosed(window->viewport(), "Esc at 780 px");
        f.resize(900);
        resizing(f);
        std::cout << "Sheet resize, keyboard steps, reset and the shared tabbed width passed\n";
        menus(f);
        std::cout << "Menu button menus, mnemonics and shortcuts passed\n";
        breakpoints(f);
        std::cout << "Breakpoints keep the open tab, widths, selection and dirty state passed\n";
        f.resize(900);
        pixels(f, false, directory);
        pixels(f, true, directory);
        window->findChild<QAction *>("view.theme.1")->trigger();
        std::cout << "Strip, icons and sheet pixels in both themes passed\n";
        railGroupBehaviour(f);
        std::cout << "Tool rail groups: one group below 800 px, the group menu, tool shortcuts, "
                     "breakpoints keeping the tool and focus passed\n";
        railGroupKeyboard(f);
        std::cout << "Tool rail group button: Tab, arrows, Enter, Space, Down and Esc passed\n";
        railGroupPixels(f, false, directory);
        railGroupPixels(f, true, directory);
        window->findChild<QAction *>("view.theme.1")->trigger();
        std::cout << "Tool rail group button pixels in both themes passed\n";
        window->document().markSaved();
        window.reset();
        std::cout << "Native narrow layout: 1600/1200/1000/900/780, strip, overlay sheet, Menu "
                     "button, breakpoints and themes passed; DPR="
                  << dpr << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
