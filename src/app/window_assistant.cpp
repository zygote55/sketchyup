#include "app/interface_preferences.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>
namespace sketchy {
void Window::toggleAssistant() {
    assistantShown_ = !assistantShown_;
    layoutAssistant();
    if (assistantShown_) {
        if (assistantSheet_ && assistantSheet_->isVisible())
            assistantSheet_->activateWindow();
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
    // R094.a: at >= 1500 px the Assistant is a splitter column beside the side panel. It stays
    // in place while that layout holds, so its resize handle survives window resizes.
    const bool column = assistantShown_ && width() >= 1500;
    if (!column || sidebarSplitter_->indexOf(assistant_) < 0) {
        // Reparent only the assistant, never the live GL viewport.
        const auto index = sideTabs_->indexOf(assistant_);
        if (index >= 0)
            sideTabs_->removeTab(index);
        if (assistantSheet_ && assistantSheet_->layout())
            assistantSheet_->layout()->removeWidget(assistant_);
        assistant_->setParent(centralWidget());
        assistant_->setMinimumWidth(240);
        assistant_->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    if (assistantSheet_)
        assistantSheet_->hide();
    const auto textPercent = interfaceTextPercent();
    sideTabs_->setVisible(width() >= 800);
    if (!assistantShown_) {
        assistant_->hide();
        layoutSidebar();
        return;
    }
    if (column) {
        if (sidebarSplitter_->indexOf(assistant_) < 0)
            sidebarSplitter_->addWidget(assistant_);
        assistant_->show();
    } else if (width() >= 1100) {
        sideTabs_->addTab(assistant_, "Assistant");
        sideTabs_->setCurrentWidget(assistant_);
        sideTabs_->show();
        assistant_->show();
    } else {
        if (!assistantSheet_) {
            assistantSheet_ = new QDialog(this);
            assistantSheet_->setObjectName("assistantSheet");
            assistantSheet_->setWindowTitle("Assistant");
            new QVBoxLayout(assistantSheet_);
            auto *action = findChild<QAction *>("view.assistant");
            auto *toggle = new QShortcut(action->shortcut(), assistantSheet_);
            toggle->setObjectName("assistantSheetToggle");
            const auto updateToggle = [action, toggle] {
                toggle->setKey(action->shortcut());
                // Plain keys remain viewport-only, including while the composer is in a sheet.
                toggle->setEnabled(action->shortcutContext() == Qt::WindowShortcut);
            };
            connect(action, &QAction::changed, toggle, updateToggle);
            updateToggle();
            connect(toggle, &QShortcut::activated, this, &Window::toggleAssistant);
            connect(assistantSheet_, &QDialog::rejected, this, [this] {
                assistantShown_ = false;
                assistant_->hide();
                focusWorkspace();
            });
        }
        assistantSheet_->layout()->addWidget(assistant_);
        // In a dialog Qt would make the panel's first button the default, so Enter in a field
        // would press it. Keep Enter as in the docked panel.
        for (auto *button : assistant_->findChildren<QPushButton *>())
            button->setAutoDefault(false);
        assistantSheet_->resize(std::min(interfaceExtent(400, textPercent), width() - 32),
                                std::max(420, height() - 64));
        assistantSheet_->show();
        assistant_->show();
        // Keep the existing model viewport available beside the nonmodal sheet.
        sideTabs_->hide();
    }
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
