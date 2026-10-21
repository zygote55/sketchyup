#include "app/keyboard_navigation.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <set>
#include <vector>
namespace sketchy {
namespace {
class ToolbarNavigation : public QObject {
  public:
    ToolbarNavigation(QToolBar *toolbar, QActionGroup *active)
        : QObject(toolbar), toolbar_(toolbar), active_(active) {
        toolbar->setFocusPolicy(Qt::NoFocus);
        toolbar->installEventFilter(this);
        connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *now) {
            if (inside(now)) {
                if (auto *button = qobject_cast<QToolButton *>(now); button && member(button))
                    rove(button);
            } else if (inside(old))
                rove(home());
        });
        if (active)
            connect(active, &QActionGroup::triggered, this, [this] { refresh(); });
        refresh();
        // Reparenting the window's contents rebuilds the focus chain; order again afterwards.
        QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object == toolbar_) {
            switch (event->type()) {
            case QEvent::FocusIn:
                // F6 and other focus requests on the rail itself land on its Tab stop.
                if (auto *target = home())
                    target->setFocus(static_cast<QFocusEvent *>(event)->reason());
                break;
            case QEvent::ParentChange:
            // A toolbar built hidden (the narrow layout's panel strip) shows its buttons when
            // it is laid out after showing; only then is one of them the Tab stop.
            case QEvent::Show:
            case QEvent::LayoutRequest:
            case QEvent::ActionAdded:
            case QEvent::ActionChanged:
            case QEvent::ActionRemoved:
                // Buttons are created and shown after this filter runs.
                QMetaObject::invokeMethod(this, [this] { refresh(); }, Qt::QueuedConnection);
                break;
            default:
                break;
            }
            return false;
        }
        auto *button = qobject_cast<QToolButton *>(object);
        if (!button || event->type() != QEvent::KeyPress)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() & ~Qt::KeypadModifier)
            return false;
        // A rail button that opens a menu (the tool group button) opens it on Down, Enter and
        // Space; the other arrows still move along the rail.
        if (button->property("sketchyupRailMenu").toBool() && button->menu() &&
            (key->key() == Qt::Key_Down || key->key() == Qt::Key_Return ||
             key->key() == Qt::Key_Enter || key->key() == Qt::Key_Space)) {
            if (!key->isAutoRepeat())
                button->showMenu();
            return true;
        }
        const auto buttons = usable();
        if (buttons.empty())
            return false;
        const auto at = std::find(buttons.begin(), buttons.end(), button);
        const int index = at == buttons.end() ? 0 : int(at - buttons.begin());
        const int count = int(buttons.size());
        QToolButton *target{};
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Left:
            target = buttons[(index + count - 1) % count];
            break;
        case Qt::Key_Down:
        case Qt::Key_Right:
            target = buttons[(index + 1) % count];
            break;
        case Qt::Key_Home:
            target = buttons.front();
            break;
        case Qt::Key_End:
            target = buttons.back();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            // Space is handled by the button itself; Enter clicks it the same way.
            if (!key->isAutoRepeat())
                button->click();
            return true;
        default:
            return false;
        }
        target->setFocus(Qt::TabFocusReason);
        return true;
    }

  private:
    QToolBar *toolbar_;
    QPointer<QActionGroup> active_;
    std::vector<QToolButton *> buttons() const {
        std::vector<QToolButton *> result;
        for (auto *action : toolbar_->actions()) {
            if (action->isSeparator())
                continue;
            if (auto *button = qobject_cast<QToolButton *>(toolbar_->widgetForAction(action)))
                result.push_back(button);
        }
        // Qt's overflow button, when tools do not fit, is the last stop; Enter or Space on it
        // opens Qt's menu of the remaining tools.
        if (auto *more = extension())
            result.push_back(more);
        return result;
    }
    QToolButton *extension() const {
        return toolbar_->findChild<QToolButton *>("qt_toolbar_ext_button",
                                                  Qt::FindDirectChildrenOnly);
    }
    std::vector<QToolButton *> usable() const {
        std::vector<QToolButton *> result;
        for (auto *button : buttons())
            if (button->isVisibleTo(toolbar_) && button->isEnabled())
                result.push_back(button);
        return result;
    }
    bool member(QToolButton *button) const {
        const auto all = buttons();
        return std::find(all.begin(), all.end(), button) != all.end();
    }
    bool inside(QWidget *widget) const {
        return widget && (widget == toolbar_ || toolbar_->isAncestorOf(widget));
    }
    // The active tool's button, or the first usable one.
    QToolButton *home() const {
        const auto candidates = usable();
        if (active_)
            if (auto *checked = active_->checkedAction())
                if (auto *button = qobject_cast<QToolButton *>(toolbar_->widgetForAction(checked));
                    button &&
                    std::find(candidates.begin(), candidates.end(), button) != candidates.end())
                    return button;
        // R096.f1: a slot that stands for tools off the rail (the Terrain slot) while one is active.
        for (auto *button : candidates)
            if (auto *action = button->defaultAction();
                action && action->property("sketchyupRailSlot").toBool() && action->isChecked())
                return button;
        return candidates.empty() ? nullptr : candidates.front();
    }
    // Exactly one button takes Tab focus; mouse clicks never take focus, as before.
    void rove(QToolButton *stop) {
        for (auto *button : buttons()) {
            if (!button->property("sketchyupRailFilter").toBool()) {
                button->installEventFilter(this);
                button->setProperty("sketchyupRailFilter", true);
            }
            button->setFocusPolicy(button == stop ? Qt::TabFocus : Qt::NoFocus);
            // The full tool name, also where the rail shows shorter icon text ("Solids").
            if (auto *action = button->defaultAction()) {
                auto name = action->property("accessibleName").toString();
                if (name.isEmpty())
                    name = action->text();
                name.remove('&');
                button->setAccessibleName(name);
            } else if (button->accessibleName().isEmpty())
                button->setAccessibleName("More tools");
        }
    }
    void refresh() {
        auto *focus = QApplication::focusWidget();
        auto *stop = inside(focus) ? qobject_cast<QToolButton *>(focus) : nullptr;
        if (!stop || !member(stop))
            stop = home();
        // Buttons created before the window's contents were assembled sit far from the rail in
        // the focus chain; keep them together just before it, so the rail's single Tab stop
        // sits where the rail is. Qt only reorders focusable widgets, hence the temporary policy.
        QWidget *previous = toolbar_->previousInFocusChain();
        while (previous != toolbar_ && toolbar_->isAncestorOf(previous))
            previous = previous->previousInFocusChain();
        const auto all = buttons();
        for (auto *button : all)
            button->setFocusPolicy(Qt::TabFocus);
        for (auto *button : all) {
            if (previous->nextInFocusChain() != button)
                QWidget::setTabOrder(previous, button);
            previous = button;
        }
        rove(stop);
    }
};
// Qt's link traversal in a QLabel keeps Shift+Tab on the label (R084.v); see the header.
class BacktabOutOfLinks : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::KeyPress)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        const bool backtab =
            key->key() == Qt::Key_Backtab ||
            (key->key() == Qt::Key_Tab && key->modifiers().testFlag(Qt::ShiftModifier));
        if (!backtab || (key->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
            return false;
        // The window moves focus to the previous stop without the label's link handling.
        QKeyEvent copy(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QCoreApplication::sendEvent(static_cast<QWidget *>(object)->window(), &copy);
        return true;
    }
};
class AutoHiddenBarStop : public QObject {
  public:
    using QObject::QObject;

  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
            static_cast<QWidget *>(object)->setFocusPolicy(
                event->type() == QEvent::Show ? Qt::TabFocus : Qt::NoFocus);
        return false;
    }
};
} // namespace
void backtabOutOfLinks(QLabel *label) { label->installEventFilter(new BacktabOutOfLinks(label)); }
void installToolbarNavigation(QToolBar *toolbar, QActionGroup *active) {
    new ToolbarNavigation(toolbar, active);
}
void keyboardTabBars(QWidget *root) {
    for (auto *bar : root->findChildren<QTabBar *>())
        for (auto *button : bar->findChildren<QToolButton *>(Qt::FindDirectChildrenOnly))
            button->setFocusPolicy(Qt::NoFocus);
    // A tab widget is a Tab stop only through its bar (its focus proxy). While an auto-hidden
    // bar is hidden, Tab would stop on the tab widget, fail to focus the hidden bar and stay
    // where it was; the bar itself is the stop once it is shown.
    for (auto *tabs : root->findChildren<QTabWidget *>())
        if (tabs->tabBarAutoHide()) {
            auto *bar = tabs->tabBar();
            tabs->setFocusPolicy(Qt::NoFocus); // Qt forwards this to the focus proxy (the bar).
            // A hidden bar still stops Tab in Qt 6.11, so it is a stop only while shown.
            bar->setFocusPolicy(bar->isVisibleTo(tabs) ? Qt::TabFocus : Qt::NoFocus);
            bar->installEventFilter(new AutoHiddenBarStop(bar));
        }
}
void keepPagesAfterTabBar(QTabWidget *tabs) {
    // Pages added after construction (render results, the Assistant) join the focus chain far
    // from the bar. Move such a page's stops, in their own order, right after the bar when it
    // becomes current. Only Tab stops matter for traversal, and Qt only reorders those; widgets
    // that pass focus to a proxy are reached through it. Pages present now already follow the bar;
    // reordering them would break the entry order of compound widgets inside them (e.g. a panel's
    // focus proxy).
    std::set<QWidget *> initial;
    for (int i = 0; i < tabs->count(); ++i)
        initial.insert(tabs->widget(i));
    const auto order = [tabs, initial] {
        auto *page = tabs->currentWidget();
        if (!page || initial.contains(page))
            return;
        std::vector<QWidget *> stops;
        for (auto *widget = page;;) {
            if ((widget == page || page->isAncestorOf(widget)) && !widget->focusProxy() &&
                (widget->focusPolicy() & Qt::TabFocus))
                stops.push_back(widget);
            widget = widget->nextInFocusChain();
            if (widget == page)
                break;
        }
        QWidget *previous = tabs->tabBar();
        for (auto *stop : stops) {
            if (previous->nextInFocusChain() != stop)
                QWidget::setTabOrder(previous, stop);
            previous = stop;
        }
    };
    QObject::connect(tabs, &QTabWidget::currentChanged, tabs, order);
    order();
}
} // namespace sketchy
