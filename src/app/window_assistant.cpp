#include "app/interface_preferences.hpp"
#include "app/window.hpp"
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
namespace sketchy {
void Window::toggleAssistant() {
    if (narrowLayout()) {
        // UX_DESIGN §3.3: the Assistant opens in the overlay sheet; Ctrl+J (or its strip
        // button) on the open Assistant closes it again.
        if (overlayOpen_ && overlayKey_ == PanelKey::Assistant) {
            closeOverlay();
            assistantShown_ = false;
            layoutAssistant();
            return;
        }
        assistantShown_ = true;
        layoutAssistant();
        sideTabs_->setCurrentWidget(assistant_);
        openOverlay(PanelKey::Assistant);
        assistant_->refresh();
        assistant_->focusComposer();
        return;
    }
    assistantShown_ = !assistantShown_;
    layoutAssistant();
    if (assistantShown_) {
        if (sideTabs_->indexOf(assistant_) >= 0)
            sideTabs_->setCurrentWidget(assistant_);
        assistant_->refresh();
        assistant_->focusComposer();
    } else {
        activateWindow();
        focusWorkspace();
    }
}
void Window::layoutAssistant() {
    if (!assistant_ || !sideTabs_)
        return;
    // R094.a: at >= 1500 px the Assistant is a splitter column beside the side panel; below,
    // a tab of the side panel (docked at 1100-1500 px, in the overlay sheet below 1100 px). It
    // moves only when its place changes, so its resize handle survives window resizes and the
    // open tab survives crossing a breakpoint. Only the assistant is reparented, never the
    // live GL viewport.
    const bool column = assistantShown_ && width() >= 1500;
    const auto tab = sideTabs_->indexOf(assistant_);
    const bool inColumn = sidebarSplitter_->indexOf(assistant_) >= 0;
    if (column) {
        if (!inColumn) {
            if (tab >= 0)
                sideTabs_->removeTab(tab);
            sidebarSplitter_->addWidget(assistant_);
        }
        assistant_->show();
    } else if (assistantShown_) {
        if (tab < 0) {
            assistant_->setMinimumWidth(240);
            assistant_->setMaximumWidth(QWIDGETSIZE_MAX);
            sideTabs_->addTab(assistant_, "Assistant");
            // Joining the side panel (newly shown, or from the column) opens its tab.
            sideTabs_->setCurrentWidget(assistant_);
        }
    } else {
        if (tab >= 0)
            sideTabs_->removeTab(tab);
        if (tab >= 0 || inColumn) {
            assistant_->setParent(centralWidget());
            assistant_->setMinimumWidth(240);
            assistant_->setMaximumWidth(QWIDGETSIZE_MAX);
        }
        assistant_->hide();
    }
    layoutNarrow();
    layoutSidebar();
}
void Window::assistantFence(bool uncertain) {
    if (assistantFenced_ == uncertain)
        return;
    assistantFenced_ = uncertain;
    if (uncertain) {
        assistantActionStates_.clear();
        assistantWidgetStates_.clear();
        for (auto *action : publicActions_) {
            if (action->objectName().startsWith("view."))
                continue;
            assistantActionStates_[action] = action->isEnabled();
            action->setEnabled(false);
        }
        for (auto *widget : std::initializer_list<QWidget *>{viewport_, tray_, measurements_}) {
            assistantWidgetStates_.emplace_back(widget, widget->isEnabled());
            widget->setEnabled(false);
        }
        // The Sheets page (canvas, page tabs) is fenced like the viewport.
        auto *sheets = workspaceStack_->widget(1);
        assistantWidgetStates_.emplace_back(sheets, sheets->isEnabled());
        sheets->setEnabled(false);
        if (recovery_) {
            assistantRecoveryInterval_ = recovery_->interval();
            recovery_->setInterval(0);
        }
        assistantShown_ = true;
        layoutAssistant();
        if (narrowLayout()) {
            sideTabs_->setCurrentWidget(assistant_);
            openOverlay(PanelKey::Assistant);
        } else if (sideTabs_->indexOf(assistant_) >= 0)
            sideTabs_->setCurrentWidget(assistant_);
        status_->setText("Assistant outcome unknown — use Reconcile before editing or saving.");
    } else {
        for (auto [action, enabled] : assistantActionStates_)
            action->setEnabled(enabled);
        for (auto [widget, enabled] : assistantWidgetStates_)
            widget->setEnabled(enabled);
        assistantActionStates_.clear();
        assistantWidgetStates_.clear();
        if (recovery_)
            recovery_->setInterval(assistantRecoveryInterval_);
        sync();
    }
}
} // namespace sketchy
