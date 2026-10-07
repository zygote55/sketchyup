#pragma once
#include <QAction>
#include <QPushButton>
#include <QSettings>
namespace sketchy {
inline int interfaceTextPercent() {
    bool valid = false;
    const auto value =
        QSettings("SketchyUp", "SketchyUp").value("interfaceTextPercent", 100).toInt(&valid);
    return valid && value >= 75 && value <= 200 && value % 25 == 0 ? value : 100;
}
inline int interfaceExtent(int logicalPixels, int percent) {
    return (logicalPixels * percent + 50) / 100;
}
inline void updateCommandSearchLabel(QWidget *window) {
    auto *button = window->findChild<QPushButton *>("commandSearch");
    auto *command = window->findChild<QAction *>("view.commands");
    if (!button || !command)
        return;
    const auto shortcut = command->shortcut().toString(QKeySequence::NativeText);
    const bool compact =
        window->width() < 800 || (interfaceTextPercent() > 125 && window->width() < 1100);
    button->setText(compact || shortcut.isEmpty() ? "Commands" : "Commands  " + shortcut);
    button->setToolTip(shortcut.isEmpty() ? "Search commands"
                                          : "Search commands (" + shortcut + ")");
    button->ensurePolished();
    button->setMinimumWidth(button->sizeHint().width());
}
} // namespace sketchy
