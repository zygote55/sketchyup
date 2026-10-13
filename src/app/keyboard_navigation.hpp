#pragma once
class QActionGroup;
class QLabel;
class QTabWidget;
class QToolBar;
class QWidget;
namespace sketchy {
// R084.v: the toolbar pattern (one Tab stop, roving focus). The rail is a single stop in the
// Tab order; Tab and focus requests on the toolbar land on the button of `active`'s checked
// action, or the first usable button. Up/Down and Left/Right move between enabled, visible
// buttons and wrap; Home/End go to the ends; Space, Enter and Return click the focused button.
// Each button's accessible name is its action's full text.
void installToolbarNavigation(QToolBar *toolbar, QActionGroup *active);
// Tab bars' scroll arrows are mouse-only; keyboard users switch tabs with the arrow keys.
// Tab widgets whose bar hides itself are not Tab stops of their own.
void keyboardTabBars(QWidget *root);
// Keeps the current page directly after the tab bar in the Tab order, so Tab from the bar
// enters the page and Shift+Tab from the page's first control returns to the bar, also for
// pages added after construction.
void keepPagesAfterTabBar(QTabWidget *tabs);
// A label with keyboard-accessible links keeps Shift+Tab on itself (Qt 6.11), trapping a
// reverse Tab cycle. Shift+Tab then leaves the label for the previous stop; Tab still walks
// its links.
void backtabOutOfLinks(QLabel *label);
} // namespace sketchy
