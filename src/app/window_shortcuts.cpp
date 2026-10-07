#include "app/interface_preferences.hpp"
#include "app/shortcut_bindings.hpp"
#include "app/window.hpp"
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QKeySequenceEdit>
#include <QLockFile>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
namespace sketchy {
namespace {
constexpr auto settingsKey = "shortcuts/v1";
ShortcutMap defaults(const std::vector<QAction *> &actions) {
    ShortcutMap result;
    for (auto *action : actions)
        result[action->objectName()] = action->property("defaultShortcut").value<QKeySequence>();
    return result;
}
} // namespace
void Window::applyShortcuts(const ShortcutBindings &bindings) {
    for (auto *action : publicActions_) {
        const auto sequence = bindings.effective().value(action->objectName());
        const bool plain =
            !sequence.isEmpty() &&
            !(sequence[0].keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
            !(sequence[0].key() >= Qt::Key_F1 && sequence[0].key() <= Qt::Key_F35);
        const bool viewportOnly =
            plain || action->property("defaultShortcutContext").toInt() == int(Qt::WidgetShortcut);
        removeAction(action);
        viewport_->removeAction(action);
        action->setShortcut(sequence);
        action->setShortcutContext(viewportOnly ? Qt::WidgetShortcut : Qt::WindowShortcut);
        if (viewportOnly)
            viewport_->addAction(action);
        else
            addAction(action);
        const auto base = action->property("shortcutTooltip").toString();
        action->setToolTip(sequence.isEmpty()
                               ? base
                               : base + " (" + sequence.toString(QKeySequence::NativeText) + ")");
    }
    updateCommandSearchLabel(this);
}
void Window::initializeShortcuts() {
    for (auto *action : publicActions_) {
        action->setProperty("defaultShortcutContext", int(action->shortcutContext()));
        action->setProperty("shortcutTooltip", action->toolTip());
    }
    try {
        QSettings settings("SketchyUp", "SketchyUp");
        ShortcutBindings bindings(defaults(publicActions_),
                                  settings.value(settingsKey).toByteArray());
        applyShortcuts(bindings);
        if (!bindings.notices().isEmpty())
            QTimer::singleShot(0, this, [this] {
                status_->setText("Some saved shortcuts conflict. Open Edit → Keyboard shortcuts to "
                                 "resolve them.");
            });
    } catch (const std::exception &error) {
        const auto message = QString::fromUtf8(error.what());
        QTimer::singleShot(
            0, this, [this, message] { status_->setText("Default shortcuts active. " + message); });
    }
}
void Window::shortcutSettings() {
    QSettings settings("SketchyUp", "SketchyUp");
    settings.sync();
    if (settings.status() != QSettings::NoError)
        throw std::runtime_error("Cannot read shortcut preferences");
    const auto original = settings.value(settingsKey).toByteArray();
    ShortcutBindings draft(defaults(publicActions_), original);
    QDialog dialog(this);
    dialog.setObjectName("shortcutDialog");
    dialog.setWindowTitle("Keyboard shortcuts");
    dialog.resize(600, 600);
    QVBoxLayout layout(&dialog);
    auto *hint = new QLabel(
        "Choose a command and a key combination, then Assign. Save applies your changes.");
    hint->setWordWrap(true);
    layout.addWidget(hint);
    auto *search = new QLineEdit;
    search->setObjectName("shortcutSearch");
    search->setAccessibleName("Find a command");
    search->setPlaceholderText("Find a command…");
    layout.addWidget(search);
    auto *list = new QListWidget;
    list->setObjectName("shortcutActions");
    list->setAccessibleName("Command shortcuts");
    layout.addWidget(list);
    for (auto *action : publicActions_) {
        auto *item = new QListWidgetItem(list);
        item->setData(Qt::UserRole, action->objectName());
        item->setData(Qt::UserRole + 1, action->text().remove('&'));
    }
    auto *editor = new QKeySequenceEdit;
    editor->setObjectName("shortcutSequence");
    editor->setAccessibleName("New key combination");
    editor->setMaximumSequenceLength(1);
    layout.addWidget(editor);
    auto *error = new QLabel;
    error->setObjectName("shortcutError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    layout.addWidget(error);
    auto *notices = new QLabel;
    notices->setObjectName("shortcutNotices");
    notices->setTextFormat(Qt::PlainText);
    notices->setWordWrap(true);
    layout.addWidget(notices);
    auto *operations = new QDialogButtonBox;
    auto *assign = operations->addButton("Assign", QDialogButtonBox::ActionRole);
    assign->setObjectName("shortcutAssign");
    auto *replace = operations->addButton("Reassign", QDialogButtonBox::ActionRole);
    replace->setObjectName("shortcutReassign");
    replace->setEnabled(false);
    auto *clear = operations->addButton("Clear", QDialogButtonBox::ActionRole);
    clear->setObjectName("shortcutClear");
    auto *reset = operations->addButton("Reset defaults", QDialogButtonBox::ActionRole);
    reset->setObjectName("shortcutReset");
    layout.addWidget(operations);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout.addWidget(buttons);
    auto refresh = [&] {
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            const auto id = item->data(Qt::UserRole).toString();
            const auto key = draft.effective().value(id).toString(QKeySequence::NativeText);
            item->setText(item->data(Qt::UserRole + 1).toString() + "  —  " +
                          (key.isEmpty() ? "Unassigned" : key));
        }
        notices->setText(draft.notices().join('\n'));
    };
    auto selectedId = [&] {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString{};
    };
    auto showConflict = [&] {
        replace->setEnabled(false);
        error->clear();
        if (selectedId().isEmpty())
            return;
        try {
            const auto conflicts = draft.conflicts(selectedId(), editor->keySequence());
            if (!conflicts.isEmpty()) {
                QStringList names;
                for (auto *action : publicActions_)
                    if (conflicts.contains(action->objectName()))
                        names << action->text().remove('&');
                error->setText("Already used by " + names.join(", ") +
                               ". Reassign clears those bindings; Save applies the changes.");
                replace->setEnabled(true);
            }
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    };
    connect(list, &QListWidget::currentRowChanged, &dialog, [&] {
        editor->setKeySequence(draft.effective().value(selectedId()));
        showConflict();
    });
    connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &text) {
        for (int i = 0; i < list->count(); ++i) {
            auto *item = list->item(i);
            item->setHidden(!item->text().contains(text, Qt::CaseInsensitive));
        }
        if (list->currentItem() && list->currentItem()->isHidden())
            list->setCurrentRow(-1);
    });
    connect(editor, &QKeySequenceEdit::keySequenceChanged, &dialog, showConflict);
    auto change = [&](bool reassign) {
        try {
            draft.assign(selectedId(), editor->keySequence(), reassign);
            refresh();
            showConflict();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    };
    connect(assign, &QPushButton::clicked, &dialog, [&] { change(false); });
    connect(replace, &QPushButton::clicked, &dialog, [&] { change(true); });
    connect(clear, &QPushButton::clicked, &dialog, [&] {
        editor->clear();
        change(false);
    });
    connect(reset, &QPushButton::clicked, &dialog, [&] {
        draft.resetKnown();
        refresh();
        editor->setKeySequence(draft.effective().value(selectedId()));
        showConflict();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            if (!selectedId().isEmpty() &&
                editor->keySequence() != draft.effective().value(selectedId()))
                throw std::runtime_error(
                    "Assign this key combination before saving, or cancel the change");
            // Coordinate our own editors and detect changes made since this dialog opened.
            const auto folder = QFileInfo(settings.fileName()).absolutePath();
            if (!QDir().mkpath(folder))
                throw std::runtime_error("Cannot create the preference directory");
            QLockFile lock(settings.fileName() + ".shortcuts-lock");
            if (!lock.tryLock(0))
                throw std::runtime_error("Another shortcut editor is saving; try again");
            settings.sync();
            if (settings.status() != QSettings::NoError)
                throw std::runtime_error("Cannot read shortcut preferences");
            if (settings.value(settingsKey).toByteArray() != original)
                throw std::runtime_error(
                    "Shortcuts changed elsewhere. Cancel and reopen this editor.");
            settings.setValue(settingsKey, draft.encode());
            settings.sync();
            if (settings.status() != QSettings::NoError)
                throw std::runtime_error(
                    "Could not save shortcuts; current window bindings are unchanged");
            applyShortcuts(draft);
            dialog.accept();
        } catch (const std::exception &failure) {
            error->setText(QString::fromUtf8(failure.what()));
        }
    });
    refresh();
    list->setCurrentRow(0);
    dialog.exec();
    activateWindow();
    viewport_->setFocus();
}
} // namespace sketchy
