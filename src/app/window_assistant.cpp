#include "app/window.hpp"
#include <QAction>
#include <QDialog>
#include <QHBoxLayout>
#include <QShortcut>
#include <QTabWidget>
#include <QVBoxLayout>
namespace sketchy {
void Window::toggleAssistant() {
    assistantShown_ = !assistantShown_;
    layoutAssistant();
    if (assistantShown_) {
        assistant_->refresh();
        assistant_->focusComposer();
    } else
        viewport_->setFocus();
}
void Window::layoutAssistant() {
    if (!assistant_ || !sideTabs_)
        return;
    // Reparent only the assistant, never the live GL viewport.
    content_->removeWidget(assistant_);
    const auto index = sideTabs_->indexOf(assistant_);
    if (index >= 0)
        sideTabs_->removeTab(index);
    if (assistantSheet_ && assistantSheet_->layout())
        assistantSheet_->layout()->removeWidget(assistant_);
    assistant_->setParent(centralWidget());
    assistant_->setMinimumWidth(240);
    assistant_->setMaximumWidth(QWIDGETSIZE_MAX);
    if (assistantSheet_)
        assistantSheet_->hide();
    sideTabs_->setFixedWidth(248);
    sideTabs_->setVisible(width() >= 800);
    if (!assistantShown_) {
        assistant_->hide();
        return;
    }
    if (width() >= 1500) {
        assistant_->setFixedWidth(340);
        content_->addWidget(assistant_);
        assistant_->show();
    } else if (width() >= 1100) {
        sideTabs_->setFixedWidth(340);
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
                viewport_->setFocus();
            });
        }
        assistantSheet_->layout()->addWidget(assistant_);
        assistantSheet_->resize(std::min(400, width() - 32), std::max(420, height() - 64));
        assistantSheet_->show();
        assistant_->show();
        // Keep the existing model viewport available beside the nonmodal sheet.
        sideTabs_->hide();
    }
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
