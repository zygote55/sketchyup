// UX_DESIGN §3.3: the narrow layout. Below 1100 px the side panel collapses to an icon strip at
// the window's right edge; a strip button (or its panel's View action and shortcut) opens the
// Model panel or the Assistant in an overlay sheet over the workspace, anchored to the right,
// and the menu bar becomes one Menu button.
#include "app/interface_preferences.hpp"
#include "app/keyboard_navigation.hpp"
#include "app/line_icons.hpp"
#include "app/sheets_tray.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenuBar>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace sketchy {
namespace {
// The overlay sheet: the side panel and its resize handle, over the workspace. An Esc that no
// control inside used closes it. The callback runs only while `guard` lives.
class PanelOverlay final : public QWidget {
  public:
    PanelOverlay(QWidget *parent, QObject *guard, std::function<void()> escape)
        : QWidget(parent), guard_(guard), escape_(std::move(escape)) {
        setAttribute(Qt::WA_StyledBackground);
    }

  protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier && guard_) {
            event->accept();
            escape_();
            return;
        }
        QWidget::keyPressEvent(event);
    }

  private:
    QPointer<QObject> guard_;
    std::function<void()> escape_;
};
// Installed on the application only while the sheet is open.
class OverlayFilter final : public QObject {
  public:
    OverlayFilter(QObject *parent, std::function<bool(QObject *, QEvent *)> filter)
        : QObject(parent), filter_(std::move(filter)) {}

  protected:
    bool eventFilter(QObject *object, QEvent *event) override { return filter_(object, event); }

  private:
    std::function<bool(QObject *, QEvent *)> filter_;
};
} // namespace
void Window::setupNarrow(QWidget *header) {
    narrowContext_ = new QObject(this);
    narrowContext_->setObjectName("narrowLayout");
    // The strip: Info, Outliner, Materials, Tags and History open their Model panel tab, then
    // the Assistant, then More panels for the whole Model panel (all thirteen tabs). Each
    // button runs its View action, so menus, the palette and shortcuts open the same sheet.
    auto *strip = new QToolBar;
    strip->setObjectName("panelStrip");
    strip->setAccessibleName("Panels");
    strip->setOrientation(Qt::Vertical);
    strip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    strip->setMovable(false);
    panelStrip_ = strip;
    auto *group = new QActionGroup(strip);
    group->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    for (const auto &[key, name, id] :
         std::initializer_list<std::tuple<PanelKey, const char *, const char *>>{
             {PanelKey::Info, "Info", "view.info"},
             {PanelKey::Outliner, "Outliner", "view.outliner"},
             {PanelKey::Materials, "Materials", "view.materials"},
             {PanelKey::Tags, "Tags", "view.tags"},
             {PanelKey::History, "History", "view.history"},
             {PanelKey::Assistant, "Assistant", "view.assistant"},
             {PanelKey::More, "More panels", "view.tray"}}) {
        auto *entry = new QAction(name, strip);
        entry->setObjectName(QString("panelStrip.") + QString(id).section('.', 1));
        entry->setProperty("panelAction", id);
        entry->setCheckable(true);
        group->addAction(entry);
        strip->addAction(entry);
        stripActions_.emplace_back(key, entry);
        connect(entry, &QAction::triggered, narrowContext_,
                [this, key = key] { stripClicked(key); });
        // Tooltips name the current binding, also after the user rebinds it.
        connect(findChild<QAction *>(id), &QAction::changed, narrowContext_,
                [this] { syncPanelStrip(); });
    }
    installToolbarNavigation(strip, group);
    content_->addWidget(strip);
    strip->hide();
    for (auto *regions : {&modelRegions_, &sheetRegions_}) {
        auto *panel = regions == &modelRegions_ ? static_cast<QWidget *>(organization_)
                                                : static_cast<QWidget *>(sheetsTray_);
        regions->insert(std::find(regions->begin(), regions->end(), panel), strip);
    }
    focusRegions_ = workspace_ == Workspace::Sheets ? sheetRegions_ : modelRegions_;

    auto *overlay = new PanelOverlay(centralWidget(), narrowContext_, [this] { closeOverlay(); });
    overlay->setObjectName("panelOverlay");
    auto *overlayLayout = new QHBoxLayout(overlay);
    overlayLayout->setContentsMargins(0, 0, 0, 0);
    overlayLayout->setSpacing(0);
    panelOverlay_ = overlay;
    overlayHandle_ = createOverlayHandle(overlay);
    overlayLayout->addWidget(overlayHandle_);
    // The panel column: the heading over the side panel (added when the layout narrows).
    auto *column = new QVBoxLayout;
    column->setSpacing(0);
    column->setContentsMargins(0, 0, 0, 0);
    auto *headingRow = new QWidget;
    auto *headingLayout = new QVBoxLayout(headingRow);
    headingLayout->setContentsMargins(headingLayout->contentsMargins().left(), 0,
                                      headingLayout->contentsMargins().right(), 0);
    overlayHeading_ = new QLabel;
    overlayHeading_->setObjectName("overlayHeading");
    overlayHeading_->setTextFormat(Qt::PlainText);
    headingLayout->addWidget(overlayHeading_);
    column->addWidget(headingRow);
    overlayLayout->addLayout(column, 1);
    overlay->hide();
    overlayDismiss_ = new OverlayFilter(narrowContext_, [this](QObject *object, QEvent *event) {
        return overlayFilter(object, event);
    });
    connect(organization_->findChild<QTabWidget *>("organizationTabs"), &QTabWidget::currentChanged,
            narrowContext_, [this] {
                // Choosing another tab in the sheet moves the strip's mark with it.
                if (overlayOpen_ && overlayKey_ != PanelKey::More &&
                    overlayKey_ != PanelKey::Assistant) {
                    overlayKey_ = organizationKey();
                    syncPanelStrip();
                }
            });
    connect(sideTabs_, &QTabWidget::currentChanged, narrowContext_, [this] {
        if (!overlayOpen_)
            return;
        if (sideTabs_->currentWidget() == assistant_)
            overlayKey_ = PanelKey::Assistant;
        else if (overlayKey_ == PanelKey::Assistant)
            overlayKey_ = organizationKey();
        syncPanelStrip();
    });

    // One Menu button with the same menus, in the menu bar's order and visibility.
    auto *menuButton = new QToolButton;
    menuButton->setObjectName("menuButton");
    menuButton->setAccessibleName("Menu");
    menuButton->setToolTip("Menu");
    menuButton->setPopupMode(QToolButton::InstantPopup);
    menuButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    auto *menu = new QMenu(menuButton);
    menu->setObjectName("narrowMenu");
    menu->setTitle("Menu");
    connect(menu, &QMenu::aboutToShow, narrowContext_, [this, menu] {
        menu->clear();
        for (auto *entry : menuBar()->actions())
            if (entry->menu())
                menu->addAction(entry);
    });
    menuButton->setMenu(menu);
    if (auto *layout = qobject_cast<QHBoxLayout *>(header->layout()))
        layout->insertWidget(0, menuButton);
    menuButton_ = menuButton;
    menuButton->hide();
    // The hidden menu bar's mnemonics (Alt+F and so on) open the same menu from the button.
    // The shortcuts belong to the button, so they are live only while it shows.
    for (auto *entry : menuBar()->actions()) {
        auto *submenu = entry->menu();
        const auto key = QKeySequence::mnemonic(entry->text());
        if (!submenu || key.isEmpty())
            continue;
        auto *shortcut = new QShortcut(key, menuButton);
        shortcut->setObjectName("menuMnemonic." + submenu->title().remove('&'));
        shortcut->setContext(Qt::WindowShortcut);
        shortcut->setEnabled(entry->isVisible());
        connect(entry, &QAction::changed, shortcut,
                [entry, shortcut] { shortcut->setEnabled(entry->isVisible()); });
        connect(shortcut, &QShortcut::activated, narrowContext_, [this, submenu] {
            submenu->popup(menuButton_->mapToGlobal(QPoint(0, menuButton_->height())));
        });
    }
}
void Window::detachNarrow() {
    if (overlayDismiss_)
        qApp->removeEventFilter(overlayDismiss_);
    // Deleting the context disconnects every narrow-layout callback and the dismiss filter;
    // the overlay and its handle check it before reaching the window.
    delete narrowContext_;
    narrowContext_ = nullptr;
    overlayDismiss_ = nullptr;
}
void Window::layoutNarrow() {
    if (!panelOverlay_ || !sidebarSplitter_)
        return;
    const bool narrow = narrowLayout();
    const bool docked = sidebarSplitter_->indexOf(sideTabs_) >= 0;
    QPointer<QWidget> focus = focusWidget();
    const bool focusInPanel = focus && sideTabs_->isAncestorOf(focus);
    if (narrow && docked) {
        // Only the side panel moves, never the live GL viewport. Its tab bar hides: the strip
        // chooses the page.
        static_cast<QVBoxLayout *>(panelOverlay_->layout()->itemAt(1)->layout())
            ->addWidget(sideTabs_, 1);
        sideTabs_->setTabBarAutoHide(false);
        sideTabs_->tabBar()->hide();
        sideTabs_->show();
        // Working in the panel as the window narrows: it stays open, as the sheet, whole (More
        // panels, with its tab row) so the focused control stays shown.
        if (focusInPanel) {
            openOverlay(sideTabs_->currentWidget() == assistant_ ? PanelKey::Assistant
                                                                 : PanelKey::More);
            if (focus)
                focus->setFocus(Qt::OtherFocusReason);
        }
    } else if (!narrow && !docked) {
        // Back to the docked panel, on the tab that was open, at the remembered widths.
        closeOverlay(false);
        sidebarSplitter_->insertWidget(1, sideTabs_);
        sideTabs_->setTabBarAutoHide(true);
        if (focusInPanel && focus)
            focus->setFocus(Qt::OtherFocusReason);
    }
    if (!narrow)
        sideTabs_->show();
    panelStrip_->setVisible(narrow);
    menuButton_->setVisible(narrow);
    menuBar()->setVisible(!narrow);
    syncPanelStrip();
}
Window::PanelKey Window::organizationKey() const {
    if (workspace_ != Workspace::Model)
        return PanelKey::More;
    auto *tabs = organization_->findChild<QTabWidget *>("organizationTabs");
    const auto name = tabs ? tabs->tabText(tabs->currentIndex()) : QString{};
    for (const auto &[key, entry] : stripActions_)
        if (key != PanelKey::Assistant && key != PanelKey::More && entry->text() == name)
            return key;
    return PanelKey::More;
}
void Window::openOverlay(PanelKey key, QWidget *returnTo) {
    if (!panelOverlay_ || !narrowLayout())
        return;
    if (sidebarSplitter_->indexOf(sideTabs_) >= 0)
        layoutNarrow();
    overlayKey_ = key;
    if (!overlayOpen_) {
        auto *focus = returnTo ? returnTo : focusWidget();
        overlayReturn_ = focus && !panelOverlay_->isAncestorOf(focus) ? focus : nullptr;
        overlayOpen_ = true;
        overlayDismissPending_ = false;
        sideTabs_->show();
        placeOverlay();
        panelOverlay_->show();
        panelOverlay_->raise();
        qApp->installEventFilter(overlayDismiss_);
    }
    syncPanelStrip();
    focusOverlay();
}
void Window::syncOverlayChrome() {
    if (!panelOverlay_ || !overlayHeading_)
        return;
    const bool sheet = sidebarSplitter_->indexOf(sideTabs_) < 0;
    auto *tabs = organization_->findChild<QTabWidget *>("organizationTabs");
    const bool row = !sheet || overlayKey_ == PanelKey::More || overlayKey_ == PanelKey::None;
    if (tabs && tabs->tabBar()->isHidden() == row) {
        // A hidden bar must not be a Tab stop (Qt forwards the policy to the bar, its proxy);
        // Ctrl+Tab still switches tabs. The policy it had comes back with the row.
        if (!row)
            tabs->setProperty("rowFocusPolicy", int(tabs->focusPolicy()));
        tabs->tabBar()->setVisible(row);
        tabs->setFocusPolicy(row ? Qt::FocusPolicy(tabs->property("rowFocusPolicy").toInt())
                                 : Qt::NoFocus);
    }
    if (!modelTrayWidgets_.empty())
        modelTrayWidgets_.front()->setVisible(!sheet && workspace_ == Workspace::Model);
    QString name = "Panels";
    for (const auto &[key, entry] : stripActions_)
        if (key == overlayKey_ && key != PanelKey::More)
            name = entry->text();
    // The Tray heading's style: upper case, as "MODEL".
    overlayHeading_->setText(name.toUpper());
    // The sheet's edge is the sidebar's resize handle only while the sheet shows.
    overlayHandle_->setAccessibleName(overlayOpen_ ? "Resize sidebar" : QString());
    overlayHeading_->setAccessibleName(name);
}
void Window::focusOverlay() {
    auto *page = sideTabs_->currentWidget();
    if (!page)
        return;
    if (page == assistant_)
        assistant_->focusComposer();
    else if (workspace_ == Workspace::Model)
        organization_->setFocus(Qt::ShortcutFocusReason);
    else
        sheetsTray_->setFocus(Qt::ShortcutFocusReason);
    if (auto *now = focusWidget(); now && panelOverlay_->isAncestorOf(now))
        return;
    // Otherwise the open page's first Tab stop.
    for (auto *widget = page->nextInFocusChain(); widget != page && page->isAncestorOf(widget);
         widget = widget->nextInFocusChain())
        if (widget->isVisibleTo(page) && widget->isEnabled() &&
            (widget->focusPolicy() & Qt::TabFocus)) {
            widget->setFocus(Qt::TabFocusReason);
            return;
        }
}
void Window::closeOverlay(bool restoreFocus) {
    if (!overlayOpen_)
        return;
    overlayOpen_ = overlayDismissPending_ = false;
    qApp->removeEventFilter(overlayDismiss_);
    auto *focus = focusWidget();
    const bool inside =
        !focus || panelOverlay_->isAncestorOf(focus) || panelStrip_->isAncestorOf(focus);
    const QPointer<QWidget> back = overlayReturn_;
    overlayReturn_ = nullptr;
    panelOverlay_->hide();
    syncPanelStrip();
    if (!restoreFocus || !inside)
        return;
    // Focus returns to where it was before the sheet opened.
    if (back && back->isVisible() && back->isEnabled())
        back->setFocus(Qt::OtherFocusReason);
    else
        focusWorkspace();
}
void Window::stripClicked(PanelKey key) {
    // The open panel's button closes the sheet. The Assistant and More panels close through
    // their own toggles (Ctrl+J, Ctrl+Shift+T), so the Assistant also leaves the side panel.
    if (overlayOpen_ && overlayKey_ == key && key != PanelKey::Assistant && key != PanelKey::More) {
        closeOverlay();
        return;
    }
    for (const auto &[entryKey, entry] : stripActions_)
        if (entryKey == key)
            if (auto *source = findChild<QAction *>(entry->property("panelAction").toString()))
                source->trigger();
    syncPanelStrip();
}
void Window::syncPanelStrip() {
    if (!panelStrip_)
        return;
    for (const auto &[key, entry] : stripActions_) {
        entry->setChecked(overlayOpen_ && overlayKey_ == key);
        const auto *source = findChild<QAction *>(entry->property("panelAction").toString());
        const auto shortcut =
            source ? source->shortcut().toString(QKeySequence::NativeText) : QString{};
        entry->setToolTip(shortcut.isEmpty() ? entry->text()
                                             : entry->text() + " (" + shortcut + ")");
    }
    if (narrowLayout())
        if (auto *panel = findChild<QAction *>("view.tray"))
            panel->setChecked(overlayOpen_ && overlayKey_ != PanelKey::Assistant);
    syncOverlayChrome();
}
void Window::themeNarrow(const ThemeColors &colors) {
    if (!panelStrip_)
        return;
    const auto percent = interfaceTextPercent();
    const int button = interfaceExtent(36, percent);
    const QSize icon(interfaceExtent(24, percent), interfaceExtent(24, percent));
    panelStrip_->setFixedWidth(interfaceExtent(46, percent));
    panelStrip_->setIconSize(icon);
    // Muted 1.5 px glyphs, 18 units in a 24-unit box; the open panel's is accent.
    for (const auto &[key, entry] : stripActions_) {
        const auto path = key == PanelKey::Info        ? icons::infoIcon()
                          : key == PanelKey::Outliner  ? icons::outlinerIcon()
                          : key == PanelKey::Materials ? icons::materialsIcon()
                          : key == PanelKey::Tags      ? icons::tagsIcon()
                          : key == PanelKey::History   ? icons::historyIcon()
                          : key == PanelKey::Assistant ? icons::assistantIcon()
                                                       : icons::morePanelsIcon();
        entry->setIcon(strokeIcon(path, colors.muted, colors.muted, colors.accent, 3));
        if (auto *widget = panelStrip_->widgetForAction(entry))
            widget->setFixedSize(button, button);
    }
    auto *menu = static_cast<QToolButton *>(menuButton_);
    menu->setIconSize(icon);
    menu->setIcon(strokeIcon(icons::menuIcon(), colors.ink, colors.muted, {}, 3));
    menu->setFixedSize(button, button);
    placeOverlay();
}
bool Window::overlayFilter(QObject *object, QEvent *event) {
    if (!overlayOpen_ || !object->isWidgetType())
        return false;
    auto *widget = static_cast<QWidget *>(object);
    if (widget->window() != this)
        return false;
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
        // A click in the workspace closes the sheet; it does not also select or draw, so the
        // press, any drag and the release are all the sheet's. It closes on the release.
        if (widget == workspaceStack_ || workspaceStack_->isAncestorOf(widget)) {
            overlayDismissPending_ = true;
            event->accept();
            return true;
        }
        return false;
    case QEvent::MouseMove:
        return overlayDismissPending_;
    case QEvent::MouseButtonRelease:
        if (!overlayDismissPending_)
            return false;
        overlayDismissPending_ = false;
        event->accept();
        closeOverlay();
        return true;
    case QEvent::ShortcutOverride: {
        // Esc in the Outliner leaves an open context; at the model's top level it closes the
        // sheet instead (the tree passes the key on to the sheet).
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier &&
            widget == outliner_ && !viewport_->selectionState().context()) {
            event->accept();
            return true;
        }
        return false;
    }
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier &&
            (widget == panelStrip_ || panelStrip_->isAncestorOf(widget))) {
            closeOverlay();
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}
} // namespace sketchy
